#pragma once

#include <Snowstorm/Assets/AssetTypes.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <cstdint>
#include <filesystem>

namespace Snowstorm
{
	// Cooked collision-shape cache (Hazel MeshColliderCache + JoltCookingFactory), the physics twin of the
	// mesh/texture cook caches: building a MeshShape means building a BVH over every triangle, and a
	// ConvexHullShape means running a hull solver -- both far more expensive than reading the result back.
	// One blob per (mesh asset, submesh, convex-or-triangles) under Engine/cache/physics.
	//
	// The blob holds GEOMETRY ONLY. Jolt keeps materials out of the binary state on purpose (see
	// Shape::SaveBinaryState), so the collider's authored ColliderMaterial is re-attached on load -- which
	// is what lets two entities share one cached shape with different friction/restitution.
	namespace JoltShapeCache
	{
		// `sourceKey` is the asset's content hash ^ import-settings hash (AssetRegistry::SourceKey), the
		// same freshness key the mesh/texture caches use: editing the source OR its import settings misses.
		[[nodiscard]] std::filesystem::path GetCachePath(AssetHandle handle, uint32_t submeshIndex, bool convex);

		// Returns null on any miss: no file, foreign/older format, a different Jolt build, a stale source
		// key, or a truncated blob. The caller then cooks and calls Save.
		[[nodiscard]] JPH::RefConst<JPH::Shape> Load(AssetHandle handle, uint32_t submeshIndex, bool convex,
		                                             uint64_t sourceKey, const JPH::PhysicsMaterial* material);

		// Best-effort: a failed write is logged and otherwise ignored (the shape is already built, and the
		// next run simply cooks again).
		bool Save(AssetHandle handle, uint32_t submeshIndex, bool convex, uint64_t sourceKey, const JPH::Shape& shape);
	}
}
