#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Snowstorm
{
	// Cooked (decode-once) texture pixels: the RGBA8 buffer stb produces from a .png/.jpg, cached as a raw
	// blob so startup/import skips re-decoding every image. Second cook cache after meshes (#84); the source
	// image is the input, this is the GPU-ready pixel artifact keyed by asset handle.
	//
	// NOT keyed by srgb: the decoded bytes are identical regardless of color space — srgb only selects the
	// Vulkan image FORMAT at upload time (sRGB vs UNORM), not the pixel data. So one blob serves both the
	// albedo (sRGB) and data-map (linear) views of the same source.
	struct CookedTexture
	{
		uint32_t Width = 0; // base (mip 0) dimensions
		uint32_t Height = 0;

		// Full mip chain, level 0 = base. Precomputed at cook time (CPU box-downsample) so the runtime
		// upload is a pure staging->image COPY per level — no vkCmdBlitImage, which lets the whole upload
		// run on a transfer-only queue (blit requires a graphics queue). Levels[i] is tightly packed RGBA8
		// of size max(1,W>>i) * max(1,H>>i) * 4. A single-level texture (e.g. the 1x1 defaults) has one entry.
		std::vector<std::vector<uint8_t>> Levels;

		// What Levels actually holds. RGBA8_UNorm means raw texels, and the SAME blob then serves both an
		// sRGB and a linear view (the color space is applied at upload). A BC7 format does NOT: a block
		// format bakes the color space in, so a compressed blob is tied to the intent it was cooked for.
		PixelFormat Format = PixelFormat::RGBA8_UNorm;

		[[nodiscard]] uint32_t MipLevels() const { return static_cast<uint32_t>(Levels.size()); }
	};

	class TextureCacheIO
	{
	public:
		// Engine/cache/texture/<handle>.sstex
		// The blob is keyed by COLOR INTENT as well as by handle: a BC7 blob bakes the color space into its
		// format, so an albedo (sRGB) and a data (linear) view of the same file cannot share one. An
		// uncompressed blob could, but keying both the same way keeps one rule instead of two.
		static std::filesystem::path GetCachePath(AssetHandle handle, bool srgb);

		// Load the cooked pixels if present AND matching sourceWriteTime (else nullopt -> caller re-decodes).
		static std::optional<CookedTexture> Load(AssetHandle handle, uint64_t sourceWriteTime, bool srgb);

		// Write cooked pixels (creates dirs; atomic temp-then-rename). Returns false on failure.
		static bool Save(AssetHandle handle, uint64_t sourceWriteTime, bool srgb, const CookedTexture& tex);
	};
}
