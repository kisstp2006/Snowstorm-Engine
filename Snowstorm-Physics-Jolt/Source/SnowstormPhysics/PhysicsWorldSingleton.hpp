#pragma once

#include <Snowstorm/Core/Base.hpp>
#include <Snowstorm/ECS/Singleton.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <entt/entt.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace Snowstorm
{
	class World;

	// Object layer encoding: bit 8 = moving (dynamic/kinematic) vs static — that is the broad-phase split
	// Jolt wants (static bodies never test against each other) — and the low 5 bits are the user-facing
	// collision layer (0..31), a row of the 32x32 collision matrix (Unity Layer Collision Matrix).
	namespace PhysicsLayers
	{
		constexpr JPH::ObjectLayer kMovingBit = 1u << 8;
		constexpr JPH::ObjectLayer kLayerMask = 31u;
		constexpr JPH::BroadPhaseLayer kBroadPhaseStatic{0};
		constexpr JPH::BroadPhaseLayer kBroadPhaseMoving{1};

		inline JPH::ObjectLayer Make(const uint32_t collisionLayer, const bool moving)
		{
			return static_cast<JPH::ObjectLayer>((collisionLayer & kLayerMask) | (moving ? kMovingBit : 0u));
		}
		inline uint32_t LayerOf(const JPH::ObjectLayer layer) { return layer & kLayerMask; }
		inline bool IsMoving(const JPH::ObjectLayer layer) { return (layer & kMovingBit) != 0; }
	}

	// One Jolt PhysicsSystem per World (ezEngine ezJoltWorldModule, Unity's per-scene PhysicsScene). Owns
	// the allocator, the layer interfaces, the contact listener (which forwards to the World's
	// ScriptEventQueue from Jolt's worker threads) and the collision matrix. Stepped by PhysicsStepSystem.
	class PhysicsWorldSingleton final : public Singleton
	{
	public:
		explicit PhysicsWorldSingleton(World* world);
		~PhysicsWorldSingleton() override;

		[[nodiscard]] JPH::PhysicsSystem& System() { return *m_System; }
		[[nodiscard]] JPH::BodyInterface& Bodies() { return m_System->GetBodyInterface(); }
		[[nodiscard]] JPH::TempAllocator& TempAllocator() { return *m_TempAllocator; }
		[[nodiscard]] World& GetWorld() const { return *m_World; }

		// Collision matrix: Layers[a] bit b set = layer a collides with layer b (symmetric, default all on).
		[[nodiscard]] bool LayersCollide(uint32_t a, uint32_t b) const;
		void SetLayersCollide(uint32_t a, uint32_t b, bool collide);

		// Body -> entity map for the contact listener (OnContactRemoved only sees body IDs, and the bodies may
		// be gone). Maintained by PhysicsBodySyncSystem on the main thread, read during the step.
		void BindBody(JPH::BodyID body, entt::entity entity);
		void UnbindBody(JPH::BodyID body);
		[[nodiscard]] entt::entity EntityOf(JPH::BodyID body) const;
		// Visit every bound body; return false from `fn` to unbind it (the caller has already removed it).
		void ForEachBoundBody(const std::function<bool(JPH::BodyID, entt::entity)>& fn);

		// Diagnostics for the Performance panel / headless logs.
		[[nodiscard]] uint32_t BodyCount() const { return m_System->GetNumBodies(); }
		[[nodiscard]] uint32_t ActiveBodyCount() const { return m_System->GetNumActiveBodies(JPH::EBodyType::RigidBody); }
		[[nodiscard]] uint32_t ContactCountLastStep() const { return m_ContactsLastStep.load(); }
		void ResetStepStats() { m_ContactsLastStep = 0; }

	private:
		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			[[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			[[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(const JPH::ObjectLayer layer) const override
			{
				return PhysicsLayers::IsMoving(layer) ? PhysicsLayers::kBroadPhaseMoving : PhysicsLayers::kBroadPhaseStatic;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			[[nodiscard]] const char* GetBroadPhaseLayerName(const JPH::BroadPhaseLayer layer) const override
			{
				return layer == PhysicsLayers::kBroadPhaseMoving ? "Moving" : "Static";
			}
#endif
		};

		class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			[[nodiscard]] bool ShouldCollide(const JPH::ObjectLayer layer, const JPH::BroadPhaseLayer broadPhase) const override
			{
				// Static never tests against static; everything else does.
				return PhysicsLayers::IsMoving(layer) || broadPhase == PhysicsLayers::kBroadPhaseMoving;
			}
		};

		class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			explicit ObjectPairFilter(const PhysicsWorldSingleton& owner)
			    : m_Owner(owner)
			{
			}
			[[nodiscard]] bool ShouldCollide(const JPH::ObjectLayer a, const JPH::ObjectLayer b) const override
			{
				if (!PhysicsLayers::IsMoving(a) && !PhysicsLayers::IsMoving(b))
				{
					return false;
				}
				return m_Owner.LayersCollide(PhysicsLayers::LayerOf(a), PhysicsLayers::LayerOf(b));
			}

		private:
			const PhysicsWorldSingleton& m_Owner;
		};

		class ContactForwarder final : public JPH::ContactListener
		{
		public:
			explicit ContactForwarder(PhysicsWorldSingleton& owner)
			    : m_Owner(owner)
			{
			}
			void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override;
			void OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) override
			{
				m_Owner.m_ContactsLastStep.fetch_add(1, std::memory_order_relaxed);
			}
			void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

		private:
			PhysicsWorldSingleton& m_Owner;
		};

		World* m_World = nullptr;
		std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;
		std::unique_ptr<JPH::PhysicsSystem> m_System;
		BroadPhaseLayers m_BroadPhaseLayers;
		ObjectVsBroadPhase m_ObjectVsBroadPhase;
		ObjectPairFilter m_PairFilter;
		ContactForwarder m_Contacts;
		std::array<uint32_t, 32> m_CollisionMatrix{};
		std::unordered_map<uint32_t, entt::entity> m_BodyToEntity;
		std::atomic<uint32_t> m_ContactsLastStep{0};
	};
}
