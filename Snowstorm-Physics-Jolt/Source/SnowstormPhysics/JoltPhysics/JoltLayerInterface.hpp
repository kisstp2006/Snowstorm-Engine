#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace Snowstorm
{
	// Jolt layer plumbing over PhysicsLayerManager (Hazel JoltLayerInterface). The object layer IS the
	// PhysicsLayerManager LayerID, plus a "moving" bit the broad phase uses to keep static bodies from
	// testing against each other. Two broad-phase layers: Static and Moving.
	namespace JoltLayers
	{
		constexpr JPH::ObjectLayer kMovingBit = 1u << 8;
		constexpr JPH::ObjectLayer kLayerMask = 0xFFu;
		constexpr JPH::BroadPhaseLayer kStatic{0};
		constexpr JPH::BroadPhaseLayer kMoving{1};

		inline JPH::ObjectLayer ToObjectLayer(const uint32_t layerID, const bool moving)
		{
			return static_cast<JPH::ObjectLayer>((layerID & kLayerMask) | (moving ? kMovingBit : 0u));
		}
		inline uint32_t LayerIDOf(const JPH::ObjectLayer layer) { return layer & kLayerMask; }
		inline bool IsMoving(const JPH::ObjectLayer layer) { return (layer & kMovingBit) != 0; }
	}

	class JoltBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
	{
	public:
		[[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
		[[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		[[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
	};

	class JoltObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
	{
	public:
		[[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override;
	};

	class JoltObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
	{
	public:
		[[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override;
	};
}
