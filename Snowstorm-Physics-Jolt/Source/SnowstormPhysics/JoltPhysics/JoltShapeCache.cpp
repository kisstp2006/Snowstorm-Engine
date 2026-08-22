#include "JoltShapeCache.hpp"

#include <Snowstorm/Core/EnginePaths.hpp>
#include <Snowstorm/Core/Log.hpp>

#include <Jolt/Core/StreamWrapper.h>

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		// Magic + Version guard against a foreign/older file of our own making. JoltVersionId guards against
		// something our own version can't see: Jolt states outright that the cooked shape format is NOT
		// backwards compatible across library versions, and a mismatched blob does not fail loudly -- it
		// restores as wrong geometry. JPH_VERSION_ID packs features/major/minor/patch, so a vcpkg Jolt bump
		// invalidates every blob automatically instead of silently corrupting collision.
		constexpr uint32_t kMagic = 0x53485053; // "SPHS"
		constexpr uint32_t kVersion = 1;

		// JPH_VERSION_ID expands to an expression built on JPH's own `uint64` typedef, so it has to be
		// evaluated where that name resolves. It packs the library version AND the configuration flags
		// (double precision, object-layer width, asserts, ...), each of which changes the binary layout --
		// so flipping any of them invalidates the cache too, not just a version bump.
		using JPH::uint64;
		constexpr uint64_t kJoltVersionId = static_cast<uint64_t>(JPH_VERSION_ID);

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t JoltVersionId = kJoltVersionId;
			uint64_t SourceKey = 0;
		};
	}

	std::filesystem::path JoltShapeCache::GetCachePath(const AssetHandle handle, const uint32_t submeshIndex, const bool convex)
	{
		std::filesystem::path path = EnginePaths::CacheDirectory() / "physics";
		// Submesh and convexity are part of the identity, not of the header: one file per shape keeps the
		// hit path a single open() and lets a stale variant be deleted on its own.
		path /= handle.ToString() + "-" + std::to_string(submeshIndex) + (convex ? "-convex" : "-tri");
		path += ".ssphys";
		return path;
	}

	JPH::RefConst<JPH::Shape> JoltShapeCache::Load(const AssetHandle handle, const uint32_t submeshIndex, const bool convex,
	                                               const uint64_t sourceKey, const JPH::PhysicsMaterial* material)
	{
		const std::filesystem::path path = GetCachePath(handle, submeshIndex, convex);
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return nullptr;
		}

		Header header{};
		in.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!in || header.Magic != kMagic || header.Version != kVersion || header.JoltVersionId != kJoltVersionId ||
		    header.SourceKey != sourceKey)
		{
			return nullptr; // stale or foreign: the caller cooks and overwrites it
		}

		JPH::StreamInWrapper stream(in);
		const JPH::Shape::ShapeResult result = JPH::Shape::sRestoreFromBinaryState(stream);
		if (result.HasError() || !in)
		{
			SS_CORE_WARN("JoltShapeCache: '{}' is unreadable ({}); re-cooking.", path.string(),
			             result.HasError() ? result.GetError().c_str() : "truncated");
			return nullptr;
		}

		// Geometry came back without its material (Jolt keeps materials out of the binary state). Re-attach
		// the collider's authored one -- we always cook with exactly one, so exactly one goes back in.
		const JPH::PhysicsMaterialRefC materialRef = material;
		result.Get()->RestoreMaterialState(&materialRef, 1);
		return result.Get();
	}

	bool JoltShapeCache::Save(const AssetHandle handle, const uint32_t submeshIndex, const bool convex,
	                          const uint64_t sourceKey, const JPH::Shape& shape)
	{
		const std::filesystem::path path = GetCachePath(handle, submeshIndex, convex);

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			SS_CORE_WARN("JoltShapeCache: could not create '{}': {}", path.parent_path().string(), ec.message());
			return false;
		}

		// Write to a temp file and rename: a crash mid-write must not leave a half blob that the next run
		// reads as valid (the header would already be there). Same recipe as the mesh cache.
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				SS_CORE_WARN("JoltShapeCache: could not write '{}'.", temp.string());
				return false;
			}
			Header header{};
			header.SourceKey = sourceKey;
			out.write(reinterpret_cast<const char*>(&header), sizeof(header));

			JPH::StreamOutWrapper stream(out);
			shape.SaveBinaryState(stream);
			if (!out || stream.IsFailed())
			{
				out.close();
				std::filesystem::remove(temp, ec);
				SS_CORE_WARN("JoltShapeCache: failed while writing '{}'.", temp.string());
				return false;
			}
		}

		std::filesystem::rename(temp, path, ec);
		if (ec)
		{
			std::filesystem::remove(temp, ec);
			SS_CORE_WARN("JoltShapeCache: could not commit '{}': {}", path.string(), ec.message());
			return false;
		}
		return true;
	}
}
