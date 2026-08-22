#pragma once

#include "JoltBody.hpp"
#include "JoltContactListener.hpp"
#include "JoltLayerInterface.hpp"

#include <Snowstorm/Core/Base.hpp>
#include <Snowstorm/ECS/Singleton.hpp>
#include <Snowstorm/Physics/PhysicsTypes.hpp>
#include <Snowstorm/Physics/SceneQueries.hpp>
#include <Snowstorm/Utility/UUID.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Snowstorm
{
	class World;
	class DebugDrawSingleton;

	// The physics scene of one World (Hazel JoltScene, registered as the World's physics singleton): the
	// Jolt PhysicsSystem, its layer interfaces and contact listener, the bodies keyed by entity UUID, the
	// contact-event buffer handed to the script layer after each step, and the scene queries.
	class JoltScene final : public Singleton
	{
	public:
		explicit JoltScene(World* world);
		~JoltScene() override;

		//// Simulation ////
		// One fixed step (dt from the engine's FixedUpdate phase): kinematic targets are applied by the
		// caller beforehand; contact events reach ScriptEventQueue on return.
		void Simulate(float fixedDt, JPH::JobSystem& jobSystem);

		[[nodiscard]] glm::vec3 GetGravity() const;
		void SetGravity(const glm::vec3& gravity);

		//// Bodies ////
		Ref<JoltBody> CreateBody(Entity entity, bool activate);
		void DestroyBody(Entity entity);
		void DestroyBodyByEntityID(UUID entityID);
		[[nodiscard]] Ref<JoltBody> GetBody(Entity entity) const;
		[[nodiscard]] Ref<JoltBody> GetBodyByEntityID(UUID entityID) const;
		[[nodiscard]] Entity GetEntityByBodyID(JPH::BodyID bodyID) const;
		void SetBodyType(Entity entity, EBodyType bodyType);
		[[nodiscard]] const std::unordered_map<UUID, Ref<JoltBody>>& GetBodies() const { return m_RigidBodies; }

		void Teleport(Entity entity, const glm::vec3& targetPosition, const glm::quat& targetRotation, bool force = false);

		//// Geometry queries ////
		bool CastRay(const RayCastInfo& rayCastInfo, SceneQueryHit& outHit) const;

		//// Diagnostics ////
		void DrawDebug(DebugDrawSingleton& out);
		[[nodiscard]] uint32_t GetBodyCount() const { return m_System->GetNumBodies(); }
		[[nodiscard]] uint32_t GetActiveBodyCount() const { return m_System->GetNumActiveBodies(JPH::EBodyType::RigidBody); }
		[[nodiscard]] uint32_t GetContactCountLastStep() const { return m_ContactsLastStep; }

		[[nodiscard]] JPH::PhysicsSystem& GetJoltSystem() { return *m_System; }
		[[nodiscard]] World& GetEntityWorld() const { return *m_EntityWorld; }

		// Called by the contact listener from Jolt's worker threads.
		void OnContactEvent(ContactType type, JPH::BodyID bodyA, JPH::BodyID bodyB, const glm::vec3& point, const glm::vec3& normal);
		void CountPersistedContact() { m_PersistedContacts.fetch_add(1, std::memory_order_relaxed); }

	private:
		void FlushContactEvents();

		World* m_EntityWorld = nullptr;
		std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;
		std::unique_ptr<JPH::PhysicsSystem> m_System;
		JoltBroadPhaseLayerInterface m_BroadPhaseLayerInterface;
		JoltObjectVsBroadPhaseLayerFilter m_ObjectVsBroadPhaseFilter;
		JoltObjectLayerPairFilter m_ObjectLayerPairFilter;
		JoltContactListener m_ContactListener;

		std::unordered_map<UUID, Ref<JoltBody>> m_RigidBodies;
		std::unordered_map<uint32_t, UUID> m_BodyToEntity; // BodyID -> entity UUID (safe after a body is gone)

		struct ContactEvent
		{
			ContactType Type = ContactType::None;
			JPH::BodyID BodyA;
			JPH::BodyID BodyB;
			glm::vec3 Point{0.0f};
			glm::vec3 Normal{0.0f};
		};
		std::mutex m_ContactEventsMutex;
		std::vector<ContactEvent> m_ContactEvents;
		std::atomic<uint32_t> m_PersistedContacts{0};
		uint32_t m_ContactsLastStep = 0;
	};
}
