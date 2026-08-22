#pragma once

#include <cstdint>

namespace Snowstorm
{
	enum class CompareOp : uint8_t
	{
		Never = 0,
		Less,
		Equal,
		LessOrEqual,
		Greater,
		NotEqual,
		GreaterOrEqual,
		Always
	};

	enum class ShaderStage : uint8_t
	{
		None = 0,
		Vertex = 1u << 0,
		Fragment = 1u << 1,
		Compute = 1u << 2,
		AllGraphics = Vertex | Fragment,
		All = Vertex | Fragment | Compute
	};

	enum class PixelFormat : uint8_t
	{
		Unknown = 0,

		// Color (LDR)
		RGBA8_UNorm,
		RGBA8_sRGB,
		BGRA8_UNorm, // Important for Windows Swapchains
		BGRA8_sRGB,

		// Color (HDR float) — needed for IBL env/irradiance/prefilter maps and an HDR scene target (#54)
		RGBA16_SFloat,
		R11G11B10_UFloat,
		RGBA32_SFloat, // full fp32 — the path-tracer accumulation buffer (#153): an fp16 running mean stalls
		               // past a few hundred samples (mean += (x-mean)/n underflows), so a converging reference needs fp32

		// Block-compressed color (BC7): 4x4 texels in 16 bytes, so a quarter the memory of RGBA8 at a
		// quality that is hard to tell apart on albedo. Only ever produced by the texture cook step, and
		// only on a device that reports textureCompressionBC -- the cook falls back to RGBA8 otherwise.
		// Block formats have no "bytes per texel": use BytesForImage() rather than BytesPerPixel().
		BC7_UNorm,
		BC7_sRGB,

		// Depth
		D32_Float,
		D24_UNorm_S8_UInt,
	};

	// A block-compressed format stores 4x4 texel blocks, so its rows are not addressable per texel and its
	// dimensions round up to whole blocks. Everything that sizes an upload or a readback has to ask.
	[[nodiscard]] constexpr bool IsBlockCompressed(const PixelFormat fmt)
	{
		return fmt == PixelFormat::BC7_UNorm || fmt == PixelFormat::BC7_sRGB;
	}

	constexpr ShaderStage operator|(ShaderStage a, ShaderStage b)
	{
		return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	constexpr bool HasStage(ShaderStage value, ShaderStage flag)
	{
		return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
	}
}
