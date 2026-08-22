#include "Texture.hpp"

#include "RendererAPI.hpp"
#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Assets/TextureCompressor.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	Ref<Texture> Texture::Create(const TextureDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL textures are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanTexture>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 textures are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	namespace
	{
		// Full mip count for a 2D texture: floor(log2(max(w,h))) + 1.
		uint32_t MipCountFor(const uint32_t w, const uint32_t h)
		{
			return 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(w, h)))));
		}

		// Box-downsample one RGBA8 level to the next (half size, min 1). Averages each 2x2 block; clamps at
		// odd/edge extents by reusing the last row/col. Simple + gamma-naive (averages in whatever space the
		// bytes are — matches what vkCmdBlitImage's linear filter did before, so no visual change vs. the old
		// GPU mip-gen). Good enough for a thesis engine; a gamma-correct/Kaiser filter is a later polish.
		std::vector<uint8_t> DownsampleRGBA8(const std::vector<uint8_t>& src, const uint32_t sw, const uint32_t sh,
		                                     const uint32_t dw, const uint32_t dh)
		{
			std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh * 4);
			for (uint32_t y = 0; y < dh; ++y)
			{
				const uint32_t sy0 = std::min(y * 2u, sh - 1);
				const uint32_t sy1 = std::min(sy0 + 1u, sh - 1);
				for (uint32_t x = 0; x < dw; ++x)
				{
					const uint32_t sx0 = std::min(x * 2u, sw - 1);
					const uint32_t sx1 = std::min(sx0 + 1u, sw - 1);
					for (uint32_t c = 0; c < 4; ++c)
					{
						const uint32_t a = src[(static_cast<size_t>(sy0) * sw + sx0) * 4 + c];
						const uint32_t b = src[(static_cast<size_t>(sy0) * sw + sx1) * 4 + c];
						const uint32_t d = src[(static_cast<size_t>(sy1) * sw + sx0) * 4 + c];
						const uint32_t e = src[(static_cast<size_t>(sy1) * sw + sx1) * 4 + c];
						dst[(static_cast<size_t>(y) * dw + x) * 4 + c] = static_cast<uint8_t>((a + b + d + e + 2) / 4);
					}
				}
			}
			return dst;
		}
	}

	std::optional<CookedTexture> Texture::DecodeCPU(const std::filesystem::path& filePath, const AssetHandle handle,
	                                                const uint64_t sourceWriteTime, const bool generateMips,
	                                                const bool compress, const bool srgb, bool* outWasCooked)
	{
		if (outWasCooked)
		{
			*outWasCooked = false; // set on the cold path below
		}

		// CPU-only, worker-safe. Fast path: the cooked .sstex blob (no stb decode, no mip-gen, no BC7
		// encode). The blob is per color space: an uncompressed cook would serve both views, but a BC7 one
		// bakes sRGB into the FORMAT, so sRGB and linear are separate blobs and the key includes it. Only
		// cache handle-backed assets — handle 0 (inline/handle-less textures) would all collide on one
		// "0.sstex", so skip the cache for those.
		const bool useCache = (handle.Value() != 0);
		if (useCache)
		{
			if (auto blob = TextureCacheIO::Load(handle, sourceWriteTime, srgb))
			{
				return blob;
			}
		}

		if (outWasCooked)
		{
			*outWasCooked = true; // decode + mips (+ BC7): the slow path, seconds rather than milliseconds
		}

		int w = 0, h = 0, channels = 0;
		// Force 4 channels so the upload is predictable (RGBA8).
		stbi_uc* pixels = stbi_load(filePath.string().c_str(), &w, &h, &channels, 4);
		if (!pixels)
		{
			SS_CORE_ERROR("Failed to decode texture image: {}", filePath.string());
			return std::nullopt;
		}

		CookedTexture cooked;
		cooked.Width = static_cast<uint32_t>(w);
		cooked.Height = static_cast<uint32_t>(h);

		// Generate the FULL mip chain now, on this worker thread. Baking mips at cook time means the runtime
		// upload is a pure staging->image copy per level (no vkCmdBlitImage) — so the whole upload can run on
		// a transfer-only queue without touching the graphics queue. (Old path blitted mips on the graphics
		// queue at upload time, stalling the frame.)
		const uint32_t mipCount = generateMips ? MipCountFor(cooked.Width, cooked.Height) : 1u;
		cooked.Levels.resize(mipCount);
		cooked.Levels[0].assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);

		uint32_t pw = cooked.Width, ph = cooked.Height;
		for (uint32_t i = 1; i < mipCount; ++i)
		{
			const uint32_t nw = std::max(1u, pw / 2u);
			const uint32_t nh = std::max(1u, ph / 2u);
			cooked.Levels[i] = DownsampleRGBA8(cooked.Levels[i - 1], pw, ph, nw, nh);
			pw = nw;
			ph = nh;
		}

		// BC7 last, over the finished mip chain: compressing the base and then downsampling BLOCKS would
		// decode-and-requantize every level, compounding the error. Each mip is encoded from its own raw
		// texels instead.
		if (compress && TextureCompressor::IsAvailable())
		{
			TextureCompressor::CompressToBC7(cooked, srgb);
		}

		if (useCache)
		{
			(void)TextureCacheIO::Save(handle, sourceWriteTime, srgb, cooked); // decode+mip once; next load reads the blob
		}
		return cooked;
	}

	Ref<Texture> Texture::CreateFromPixels(const CookedTexture& cooked, const bool srgb, const std::string& debugName)
	{
		SS_CORE_ASSERT(!cooked.Levels.empty() && cooked.Width > 0 && cooked.Height > 0, "CreateFromPixels: empty pixels");

		TextureDesc desc{};
		desc.Dimension = TextureDimension::Texture2D;
		desc.Width = cooked.Width;
		desc.Height = cooked.Height;
		// Mips are precomputed in the cook (cooked.Levels), so the image just needs that many levels and a
		// pure copy per level — no TransferSrc/blit needed (that was for GPU-side mip generation).
		desc.MipLevels = cooked.MipLevels();
		desc.ArrayLayers = 1;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
		// Color intent decides the sampled color space: albedo/emissive are authored in sRGB and must be
		// decoded to linear on sample (srgb=true); normal/metallic-roughness/AO are data maps whose values
		// are NOT gamma-encoded and must be read verbatim (srgb=false). Sampling a normal map as sRGB skews
		// every channel and breaks lighting — the caller picks the flag per slot (see GetTextureView).
		//
		// A COMPRESSED blob already decided this: a block format carries its color space, and the blob was
		// cooked for one intent (which is why the cache path is keyed by it). Its format wins -- describing
		// BC7 bytes as RGBA8 makes the upload size the copy by 4 bytes per texel and overrun its staging
		// buffer, which is exactly what happened when this line ran unconditionally.
		desc.Format = IsBlockCompressed(cooked.Format) ? cooked.Format
		                                               : (srgb ? PixelFormat::RGBA8_sRGB : PixelFormat::RGBA8_UNorm);
		desc.DebugName = debugName;

		auto texture = Texture::Create(desc);
		texture->SetMipData(cooked.Levels);
		return texture;
	}

	Ref<Texture> Texture::Create(const std::filesystem::path& filePath, bool srgb)
	{
		// Synchronous convenience path (kept for non-async callers): decode on the calling thread, upload.
		// The async loader instead calls DecodeCPU on a worker and CreateFromPixels on the main thread.
		const uint64_t sourceTime = GetFileWriteTimeU64(filePath);
		auto cooked = DecodeCPU(filePath, AssetHandle{0}, sourceTime);
		SS_CORE_ASSERT(cooked, "Failed to load texture image: {}", filePath.string());
		if (!cooked)
		{
			return nullptr;
		}
		return CreateFromPixels(*cooked, srgb, filePath.filename().string());
	}

	Ref<TextureView> TextureView::Create(const Ref<Texture>& texture, const TextureViewDesc& desc)
	{
		SS_CORE_ASSERT(texture, "TextureView::Create called with null texture");

		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL texture views are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanTextureView>(texture, desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 texture views are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
