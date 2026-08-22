#include "JoltLayerInterface.hpp"

#include <Snowstorm/Physics/PhysicsLayer.hpp>

namespace Snowstorm
{
	JPH::BroadPhaseLayer JoltBroadPhaseLayerInterface::GetBroadPhaseLayer(const JPH::ObjectLayer layer) const
	{
		return JoltLayers::IsMoving(layer) ? JoltLayers::kMoving : JoltLayers::kStatic;
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	const char* JoltBroadPhaseLayerInterface::GetBroadPhaseLayerName(const JPH::BroadPhaseLayer layer) const
	{
		return layer == JoltLayers::kMoving ? "Moving" : "Static";
	}
#endif

	bool JoltObjectVsBroadPhaseLayerFilter::ShouldCollide(const JPH::ObjectLayer layer, const JPH::BroadPhaseLayer broadPhase) const
	{
		// Static never tests against the static tree; everything else does.
		return JoltLayers::IsMoving(layer) || broadPhase == JoltLayers::kMoving;
	}

	bool JoltObjectLayerPairFilter::ShouldCollide(const JPH::ObjectLayer a, const JPH::ObjectLayer b) const
	{
		if (!JoltLayers::IsMoving(a) && !JoltLayers::IsMoving(b))
		{
			return false;
		}
		return PhysicsLayerManager::ShouldCollide(JoltLayers::LayerIDOf(a), JoltLayers::LayerIDOf(b));
	}
}
