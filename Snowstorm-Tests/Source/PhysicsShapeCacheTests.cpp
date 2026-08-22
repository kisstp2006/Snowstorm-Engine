#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "SnowstormPhysics/JoltPhysics/JoltMaterial.hpp"
#include "SnowstormPhysics/JoltPhysics/JoltShapeCache.hpp"
#include "SnowstormPhysics/PhysicsJoltModule.hpp"

#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <filesystem>
#include <fstream>

using namespace Snowstorm;

namespace
{
	// A handle nothing else uses, so a run can't collide with a real project's cache entries.
	const AssetHandle kTestHandle{0x5350485353544553ull}; // UUID has a runtime ctor, so not constexpr
	constexpr uint32_t kSubmesh = 0;
	constexpr uint64_t kSourceKey = 0x0123456789ABCDEFull;

	// Removes the blob before AND after each case: a leftover from a failed run must not make the next one
	// pass by reading a stale file.
	struct ScopedCacheFile
	{
		explicit ScopedCacheFile(const bool convex)
		    : Path(JoltShapeCache::GetCachePath(kTestHandle, kSubmesh, convex))
		{
			Remove();
		}
		~ScopedCacheFile() { Remove(); }
		void Remove() const
		{
			std::error_code ec;
			std::filesystem::remove(Path, ec);
		}
		std::filesystem::path Path;
	};

	JPH::RefConst<JPH::Shape> BuildConvexHull(const JPH::PhysicsMaterial* material)
	{
		JPH::Array<JPH::Vec3> points;
		for (int i = 0; i < 8; ++i)
		{
			points.emplace_back((i & 1) ? 1.0f : -1.0f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 2.0f : -2.0f);
		}
		const JPH::Shape::ShapeResult result =
		    JPH::ConvexHullShapeSettings(points.data(), static_cast<int>(points.size()), JPH::cDefaultConvexRadius, material).Create();
		REQUIRE_FALSE(result.HasError());
		return result.Get();
	}

	JPH::RefConst<JPH::Shape> BuildTriangleMesh(const JPH::PhysicsMaterial* material)
	{
		JPH::VertexList vertices{{-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}};
		JPH::IndexedTriangleList triangles;
		triangles.emplace_back(0u, 1u, 2u, 0u);
		triangles.emplace_back(0u, 2u, 3u, 0u);
		JPH::PhysicsMaterialList materials;
		materials.push_back(material);
		const JPH::Shape::ShapeResult result =
		    JPH::MeshShapeSettings(std::move(vertices), std::move(triangles), std::move(materials)).Create();
		REQUIRE_FALSE(result.HasError());
		return result.Get();
	}
}

TEST_CASE("The cooked shape cache round-trips a convex hull, material included", "[physics][shapecache]")
{
	PhysicsJoltModule::EnsureJoltInitialized();
	const ScopedCacheFile file(/*convex=*/true);

	const JPH::Ref<JoltMaterial> material = new JoltMaterial(ColliderMaterial{0.7f, 0.3f});
	const JPH::RefConst<JPH::Shape> cooked = BuildConvexHull(material);

	REQUIRE(JoltShapeCache::Save(kTestHandle, kSubmesh, true, kSourceKey, *cooked));
	REQUIRE(std::filesystem::exists(file.Path));

	const JPH::RefConst<JPH::Shape> loaded =
	    JoltShapeCache::Load(kTestHandle, kSubmesh, true, kSourceKey, material);
	REQUIRE(loaded != nullptr);
	REQUIRE(loaded->GetSubType() == cooked->GetSubType());

	// Same geometry: bounds and volume survive the round trip.
	REQUIRE(loaded->GetLocalBounds().mMin.IsClose(cooked->GetLocalBounds().mMin));
	REQUIRE(loaded->GetLocalBounds().mMax.IsClose(cooked->GetLocalBounds().mMax));
	REQUIRE(loaded->GetVolume() == Catch::Approx(cooked->GetVolume()).epsilon(0.001));

	// The material is NOT part of the blob (Jolt keeps it out of the binary state) -- the cache re-attaches
	// the one the caller passed in, which is what lets two colliders share a blob with different friction.
	const auto* restored = static_cast<const JoltMaterial*>(loaded->GetMaterial(JPH::SubShapeID()));
	REQUIRE(restored != nullptr);
	REQUIRE(restored->Friction == Catch::Approx(0.7f));
	REQUIRE(restored->Restitution == Catch::Approx(0.3f));
}

TEST_CASE("The cooked shape cache round-trips a triangle mesh", "[physics][shapecache]")
{
	PhysicsJoltModule::EnsureJoltInitialized();
	const ScopedCacheFile file(/*convex=*/false);

	const JPH::Ref<JoltMaterial> material = new JoltMaterial(ColliderMaterial{});
	const JPH::RefConst<JPH::Shape> cooked = BuildTriangleMesh(material);

	REQUIRE(JoltShapeCache::Save(kTestHandle, kSubmesh, false, kSourceKey, *cooked));
	const JPH::RefConst<JPH::Shape> loaded =
	    JoltShapeCache::Load(kTestHandle, kSubmesh, false, kSourceKey, material);

	REQUIRE(loaded != nullptr);
	REQUIRE(loaded->GetSubType() == JPH::EShapeSubType::Mesh);
	REQUIRE(loaded->GetLocalBounds().mMin.IsClose(cooked->GetLocalBounds().mMin));
	REQUIRE(loaded->GetLocalBounds().mMax.IsClose(cooked->GetLocalBounds().mMax));
}

TEST_CASE("The cooked shape cache misses instead of returning wrong geometry", "[physics][shapecache]")
{
	PhysicsJoltModule::EnsureJoltInitialized();
	const ScopedCacheFile file(/*convex=*/true);

	const JPH::Ref<JoltMaterial> material = new JoltMaterial(ColliderMaterial{});
	REQUIRE(JoltShapeCache::Save(kTestHandle, kSubmesh, true, kSourceKey, *BuildConvexHull(material)));

	SECTION("A changed source asset (or import setting) misses")
	{
		REQUIRE(JoltShapeCache::Load(kTestHandle, kSubmesh, true, kSourceKey + 1, material) == nullptr);
	}

	SECTION("A blob written by a different Jolt build misses")
	{
		// Jolt's cooked format is explicitly NOT compatible across library versions/configurations, and a
		// mismatched blob restores as WRONG geometry rather than failing -- so the version stamp is the only
		// thing standing between a vcpkg Jolt bump and silently broken collision. Corrupt it and expect a miss.
		std::fstream patch(file.Path, std::ios::binary | std::ios::in | std::ios::out);
		REQUIRE(patch.is_open());
		patch.seekp(8); // sizeof(Magic) + sizeof(Version)
		const uint64_t foreignVersion = 0xDEADBEEFDEADBEEFull;
		patch.write(reinterpret_cast<const char*>(&foreignVersion), sizeof(foreignVersion));
		patch.close();

		REQUIRE(JoltShapeCache::Load(kTestHandle, kSubmesh, true, kSourceKey, material) == nullptr);
	}

	SECTION("A truncated blob misses")
	{
		const auto size = std::filesystem::file_size(file.Path);
		std::filesystem::resize_file(file.Path, size / 2);
		REQUIRE(JoltShapeCache::Load(kTestHandle, kSubmesh, true, kSourceKey, material) == nullptr);
	}

	SECTION("A missing file misses")
	{
		file.Remove();
		REQUIRE(JoltShapeCache::Load(kTestHandle, kSubmesh, true, kSourceKey, material) == nullptr);
	}
}
