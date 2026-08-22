#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/ContactListener.h>

namespace Snowstorm
{
	class JoltScene;

	// Jolt contact callbacks (worker threads) -> JoltScene::OnContactEvent (Hazel JoltContactListener).
	// Sensors report Trigger events, everything else Collision events; removal only knows body IDs.
	class JoltContactListener final : public JPH::ContactListener
	{
	public:
		explicit JoltContactListener(JoltScene& scene)
		    : m_Scene(scene)
		{
		}

		void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override;
		void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override;
		void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

	private:
		JoltScene& m_Scene;
	};
}
