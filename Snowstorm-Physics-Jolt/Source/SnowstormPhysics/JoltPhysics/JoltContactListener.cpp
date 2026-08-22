#include "JoltContactListener.hpp"

#include "JoltScene.hpp"
#include "JoltUtils.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

namespace Snowstorm
{
	void JoltContactListener::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/)
	{
		const bool trigger = body1.IsSensor() || body2.IsSensor();
		m_Scene.OnContactEvent(trigger ? ContactType::TriggerBegin : ContactType::CollisionBegin, body1.GetID(), body2.GetID(),
		                       JoltUtils::FromJoltVector(manifold.GetWorldSpaceContactPointOn1(0)), JoltUtils::FromJoltVector(manifold.mWorldSpaceNormal));
	}

	void JoltContactListener::OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&)
	{
		m_Scene.CountPersistedContact();
	}

	void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& pair)
	{
		// The bodies may already be gone: only their IDs are safe here. The scene resolves them through its
		// own body map and reports the end of the pair (trigger or collision is decided there).
		m_Scene.OnContactEvent(ContactType::CollisionEnd, pair.GetBody1ID(), pair.GetBody2ID(), glm::vec3(0.0f), glm::vec3(0.0f));
	}
}
