#include "PhysicsWorldSingleton.hpp"

#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Scripting/ScriptEvents.hpp>
#include <Snowstorm/World/World.hpp>

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>

namespace Snowstorm
{
	namespace
	{
		// Sized for the thesis scenes (Jolt HelloWorld scale x4); a larger level bumps these.
		constexpr JPH::uint kMaxBodies = 4096;
		constexpr JPH::uint kNumBodyMutexes = 0; // default
		constexpr JPH::uint kMaxBodyPairs = 4096;
		constexpr JPH::uint kMaxContactConstraints = 2048;
		constexpr size_t kTempAllocatorBytes = 16 * 1024 * 1024;
	}

	PhysicsWorldSingleton::PhysicsWorldSingleton(World* world)
	    : m_World(world), m_PairFilter(*this), m_Contacts(*this)
	{
		m_CollisionMatrix.fill(0xFFFFFFFFu); // every layer collides with every layer by default
		m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(static_cast<JPH::uint>(kTempAllocatorBytes));
		m_System = std::make_unique<JPH::PhysicsSystem>();
		m_System->Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints, m_BroadPhaseLayers, m_ObjectVsBroadPhase, m_PairFilter);
		m_System->SetContactListener(&m_Contacts);
	}

	PhysicsWorldSingleton::~PhysicsWorldSingleton()
	{
		if (m_System)
		{
			m_System->SetContactListener(nullptr);
		}
	}

	bool PhysicsWorldSingleton::LayersCollide(const uint32_t a, const uint32_t b) const
	{
		return (m_CollisionMatrix[a & 31u] & (1u << (b & 31u))) != 0;
	}

	void PhysicsWorldSingleton::SetLayersCollide(const uint32_t a, const uint32_t b, const bool collide)
	{
		const uint32_t ia = a & 31u, ib = b & 31u;
		if (collide)
		{
			m_CollisionMatrix[ia] |= 1u << ib;
			m_CollisionMatrix[ib] |= 1u << ia;
		}
		else
		{
			m_CollisionMatrix[ia] &= ~(1u << ib);
			m_CollisionMatrix[ib] &= ~(1u << ia);
		}
	}

	void PhysicsWorldSingleton::BindBody(const JPH::BodyID body, const entt::entity entity)
	{
		m_BodyToEntity[body.GetIndexAndSequenceNumber()] = entity;
	}

	void PhysicsWorldSingleton::UnbindBody(const JPH::BodyID body)
	{
		m_BodyToEntity.erase(body.GetIndexAndSequenceNumber());
	}

	void PhysicsWorldSingleton::ForEachBoundBody(const std::function<bool(JPH::BodyID, entt::entity)>& fn)
	{
		for (auto it = m_BodyToEntity.begin(); it != m_BodyToEntity.end();)
		{
			if (!fn(JPH::BodyID(it->first), it->second))
			{
				it = m_BodyToEntity.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	entt::entity PhysicsWorldSingleton::EntityOf(const JPH::BodyID body) const
	{
		const auto it = m_BodyToEntity.find(body.GetIndexAndSequenceNumber());
		return it == m_BodyToEntity.end() ? entt::null : it->second;
	}

	// ---- contact forwarding (Jolt worker threads) ----

	void PhysicsWorldSingleton::ContactForwarder::OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& /*settings*/)
	{
		m_Owner.m_ContactsLastStep.fetch_add(1, std::memory_order_relaxed);
		if (!m_Owner.m_World->HasSingleton<ScriptEventQueue>())
		{
			return;
		}
		auto& queue = m_Owner.m_World->GetSingleton<ScriptEventQueue>();

		const bool trigger = a.IsSensor() || b.IsSensor();
		ScriptEvent ev;
		ev.Type = trigger ? ScriptEvent::Kind::TriggerEnter : ScriptEvent::Kind::CollisionEnter;
		const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
		ev.Contact.Point = {static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ())};
		ev.Contact.Normal = {manifold.mWorldSpaceNormal.GetX(), manifold.mWorldSpaceNormal.GetY(), manifold.mWorldSpaceNormal.GetZ()};
		ev.Contact.Impulse = manifold.mPenetrationDepth;

		// Both sides get the callback with the other as `other` (Unity delivers OnCollisionEnter to both).
		ev.A = m_Owner.EntityOf(a.GetID());
		ev.B = m_Owner.EntityOf(b.GetID());
		if (ev.A != entt::null)
		{
			queue.Push(ev);
		}
		if (ev.B != entt::null)
		{
			std::swap(ev.A, ev.B);
			ev.Contact.Normal = -ev.Contact.Normal;
			queue.Push(ev);
		}
	}

	void PhysicsWorldSingleton::ContactForwarder::OnContactRemoved(const JPH::SubShapeIDPair& pair)
	{
		if (!m_Owner.m_World->HasSingleton<ScriptEventQueue>())
		{
			return;
		}
		auto& queue = m_Owner.m_World->GetSingleton<ScriptEventQueue>();
		const entt::entity ea = m_Owner.EntityOf(pair.GetBody1ID());
		const entt::entity eb = m_Owner.EntityOf(pair.GetBody2ID());
		// We can't read the bodies here (they may already be gone), so report CollisionExit; a trigger
		// script that needs the distinction tracks its own enter/exit pairs.
		ScriptEvent ev;
		ev.Type = ScriptEvent::Kind::CollisionExit;
		ev.A = ea;
		ev.B = eb;
		if (ea != entt::null)
		{
			queue.Push(ev);
		}
		if (eb != entt::null)
		{
			std::swap(ev.A, ev.B);
			queue.Push(ev);
		}
	}
}
