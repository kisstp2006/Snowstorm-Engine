#include "TextureCompressor.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <bc7enc.h>

#include <algorithm>
#include <cstring>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kBlockSize = 4;
		constexpr uint32_t kBytesPerBlock = 16; // BC7: 4x4 texels in 128 bits

		// Below one full block there is nothing to gain: a 2x2 mip still costs a whole 16-byte block, which
		// is more than its 16 bytes of RGBA8. The tail of every mip chain looks like this.
		bool WorthCompressing(const uint32_t width, const uint32_t height)
		{
			return width >= kBlockSize && height >= kBlockSize;
		}

		// bc7enc's parameters, resolved once. Level 0 is its fastest setting; the encoder searches fewer
		// block-mode partitions, which is the difference between a texture cook measured in seconds and one
		// measured in minutes. Quality loss versus an exhaustive search is small and, on albedo, invisible
		// next to the loss from block compression itself.
		const bc7enc_compress_block_params& BlockParams()
		{
			static const bc7enc_compress_block_params params = []
			{
				bc7enc_compress_block_init(); // builds the encoder's lookup tables; must precede any encode
				bc7enc_compress_block_params p{};
				bc7enc_compress_block_params_init(&p);
				p.m_max_partitions_mode = 0;
				p.m_uber_level = 0;
				return p;
			}();
			return params;
		}
	}

	bool TextureCompressor::IsAvailable()
	{
		return true; // bc7enc is plain C with no platform dependencies
	}

	bool TextureCompressor::CompressToBC7(CookedTexture& cooked, const bool srgb)
	{
		if (cooked.Levels.empty() || IsBlockCompressed(cooked.Format))
		{
			return false;
		}
		if (!WorthCompressing(cooked.Width, cooked.Height))
		{
			return false;
		}

		const bc7enc_compress_block_params& params = BlockParams();

		std::vector<std::vector<uint8_t>> compressed;
		compressed.reserve(cooked.Levels.size());

		uint32_t width = cooked.Width;
		uint32_t height = cooked.Height;
		for (const std::vector<uint8_t>& level : cooked.Levels)
		{
			// A mip chain cannot mix formats, so the first mip too small for a block ENDS the chain rather
			// than staying uncompressed. That is the standard shape of a BC7 chain (it stops at 4x4), not a
			// compromise -- and it is why the image's mip count comes from the compressed chain, never the
			// original one.
			if (!WorthCompressing(width, height))
			{
				break;
			}

			// Whole blocks, rounding UP: a 6x6 mip is 2x2 blocks and occupies 64 bytes, with the right and
			// bottom edges padded by repeating the last texel. Getting this wrong is what made the upload
			// overrun its staging buffer -- the GPU sizes the copy from the same ceil(), so the CPU side has
			// to agree exactly.
			const uint32_t blocksX = (width + kBlockSize - 1) / kBlockSize;
			const uint32_t blocksY = (height + kBlockSize - 1) / kBlockSize;

			std::vector<uint8_t> out(static_cast<size_t>(blocksX) * blocksY * kBytesPerBlock);
			for (uint32_t by = 0; by < blocksY; ++by)
			{
				for (uint32_t bx = 0; bx < blocksX; ++bx)
				{
					// Gather the block's 16 texels. Edge blocks of a non-multiple-of-4 mip clamp to the last
					// real texel rather than reading past the row, which also keeps the padded texels close
					// to their neighbours so the encoder doesn't waste bits on an invented edge.
					uint8_t texels[kBlockSize * kBlockSize * 4];
					for (uint32_t y = 0; y < kBlockSize; ++y)
					{
						const uint32_t sy = std::min(by * kBlockSize + y, height - 1);
						for (uint32_t x = 0; x < kBlockSize; ++x)
						{
							const uint32_t sx = std::min(bx * kBlockSize + x, width - 1);
							const size_t src = (static_cast<size_t>(sy) * width + sx) * 4;
							std::memcpy(&texels[(y * kBlockSize + x) * 4], &level[src], 4);
						}
					}

					const size_t dst = (static_cast<size_t>(by) * blocksX + bx) * kBytesPerBlock;
					bc7enc_compress_block(&out[dst], texels, &params);
				}
			}

			compressed.push_back(std::move(out));
			width = std::max(1u, width / 2u);
			height = std::max(1u, height / 2u);
		}

		if (compressed.empty())
		{
			return false;
		}

		cooked.Levels = std::move(compressed);
		// The color space rides in the FORMAT for a block texture: bc7enc encodes the bytes it is given, so
		// sRGB-encoded input plus a _SRGB format is what makes the GPU decode it back to linear on sample.
		cooked.Format = srgb ? PixelFormat::BC7_sRGB : PixelFormat::BC7_UNorm;
		return true;
	}
}
