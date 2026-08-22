#pragma once

#include "Snowstorm/Assets/TextureCache.hpp"

namespace Snowstorm
{
	// BC7 block compression for cooked textures (Unity's per-platform compression, Unreal's TC_Default).
	//
	// BC7 packs 4x4 texels into 16 bytes -- a quarter of RGBA8 -- at a quality that is hard to tell apart on
	// albedo, which is what makes it the default in every production engine. The saving is not only VRAM:
	// less memory moved per sample is less bandwidth, and on a ray-traced frame the BVH is competing for the
	// same budget.
	//
	// Runs at COOK time, on the same JobSystem worker that decoded the image, and the result goes into the
	// .sstex blob -- so a texture is compressed once per source, not once per run. The encoder itself is
	// single-threaded on purpose: the parallelism belongs one level up, where many textures cook at once.
	// (An encoder that spawns its own pool inside every job just makes the workers fight each other.)
	namespace TextureCompressor
	{
		// True when this build can encode at all. Always true today -- bc7enc is plain C with no platform
		// dependencies -- but kept as a seam so a platform without an encoder degrades to the uncompressed
		// cook, which is a quality/size difference rather than a failure.
		[[nodiscard]] bool IsAvailable();

		// Compresses every mip of `cooked` in place to BC7 and sets its Format. A no-op returning false when
		// encoding isn't available, the texture is already compressed, or it is too small to be worth a block
		// format -- below one 4x4 block the padding costs more than the compression saves.
		//
		// `srgb` picks BC7_sRGB vs BC7_UNorm. It has to be known at COOK time, unlike the uncompressed path
		// where one blob served both views: a block format bakes the color space into the image format, so
		// an albedo map and a data map cannot share a blob any more.
		bool CompressToBC7(CookedTexture& cooked, bool srgb);
	}
}
