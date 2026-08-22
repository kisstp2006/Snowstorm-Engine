#include "PhysicsSystems.hpp"

#include "JoltPhysics/JoltJobSystem.hpp"
#include "JoltPhysics/JoltScene.hpp"
#include "JoltPhysics/JoltShapes.hpp"
#include "PhysicsCVars.hpp"

#include <Snowstorm/Components/ComponentRegistry.hpp>
#include <Snowstorm/Components/HierarchyComponent.hpp>
#include <Snowstorm/Components/PhysicsComponents.hpp>
#include <Snowstorm/Components/TransformComponent.hpp>
#include <Snowstorm/Components/WorldTransformComponent.hpp>
#include <Snowstorm/Core/Application.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Debug/DebugDrawSingleton.hpp>
#include <Snowstorm/ECS/SystemManager.hpp>
#include <Snowstorm/Math/Transform.hpp>
#include <Snowstorm/Physics/PhysicsSystem.hpp>
#include <Snowstorm/World/SimulationStateSingleton.hpp>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>

#include <rttr/registration.h>

#include <vector>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<PhysicsBodyRuntimeComponent>("Snowstorm::PhysicsBodyRuntimeComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterPhysicsBodyRuntime
		{
			AutoRegisterPhysicsBodyRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<PhysicsBodyRuntimeComponent>(opts);
			}
		};
		const AutoRegisterPhysicsBodyRuntime g_autoRegisterPhysicsBodyRuntime;

		bool HasCollider(TrackedRegistry& reg, const entt::entity e)
		{
			return reg.any_of<BoxColliderComponent>(e) || reg.any_of<SphereColliderComponent>(e) ||
			       reg.any_of<CapsuleColliderComponent>(e) || reg.any_of<MeshColliderComponent>(e);
		}

		// Nearest ancestor (inclusive) with a RigidBodyComponent: the body a collider belongs to.
		bool HasBodyAbove(TrackedRegistry& reg, const entt::entity e)
		{
			const auto* h = reg.try_get_const<HierarchyComponent>(e);
			for (entt::entity cur = h ? h->Parent : entt::null; cur != entt::null;)
			{
				if (reg.any_of<RigidBodyComponent>(cur))
				{
					return true;
				}
				const auto* ph = reg.try_get_const<HierarchyComponent>(cur);
				cur = ph ? ph->Parent : entt::null;
			}
			return false;
		}

		// A body entity: carries a RigidBody, or is a collider with no body above it (implicit static).
		bool IsBodyEntity(TrackedRegistry& reg, const entt::entity e)
		{
			return reg.any_of<WorldTransformComponent>(e) &&
			       (reg.any_of<RigidBodyComponent>(e) || (HasCollider(reg, e) && !HasBodyAbove(reg, e)));
		}

		bool IsSimulating(const World& world)
		{
			return !world.HasSingleton<SimulationStateSingleton>() || world.GetSingleton<SimulationStateSingleton>().IsPlaying();
		}
	}

	// =================================================================================================
	// PhysicsBodySyncSystem
	// =================================================================================================

	void PhysicsBodySyncSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<JoltScene>())
		{
			return;
		}
		auto& scene = SingletonView<JoltScene>();
		auto& reg = m_World->GetRegistry();
		const bool simulating = IsSimulating(*m_World);

		// 1. Bodies whose entity died or stopped being a body: drop them (the JoltBody destructor removes
		//    the Jolt body; the UUID-keyed map is the Hazel bookkeeping).
		std::vector<UUID> dead;
		for (const auto& [uuid, body] : scene.GetBodies())
		{
			const Entity e = body->GetEntity();
			if (!e.IsValid() || !IsBodyEntity(reg, e.Handle()))
			{
				if (e.IsValid() && reg.any_of<PhysicsBodyRuntimeComponent>(e.Handle()))
				{
					reg.remove<PhysicsBodyRuntimeComponent>(e.Handle());
				}
				dead.push_back(uuid);
			}
		}
		for (const UUID uuid : dead)
		{
			scene.DestroyBodyByEntityID(uuid);
		}

		// 2. Every body entity: create, rebuild on authored change, or follow an edited transform.
		std::vector<entt::entity> bodyEntities;
		for (const auto view = reg.view<RigidBodyComponent, WorldTransformComponent>(); const entt::entity e : view)
		{
			bodyEntities.push_back(e);
		}
		auto addImplicitStatic = [&](const entt::entity e)
		{
			if (!reg.any_of<RigidBodyComponent>(e) && IsBodyEntity(reg, e))
			{
				bodyEntities.push_back(e);
			}
		};
		for (const auto v = reg.view<BoxColliderComponent>(); const entt::entity e : v)
			addImplicitStatic(e);
		for (const auto v = reg.view<SphereColliderComponent>(); const entt::entity e : v)
			addImplicitStatic(e);
		for (const auto v = reg.view<CapsuleColliderComponent>(); const entt::entity e : v)
			addImplicitStatic(e);
		for (const auto v = reg.view<MeshColliderComponent>(); const entt::entity e : v)
			addImplicitStatic(e);

		for (const entt::entity e : bodyEntities)
		{
			const Entity entity{e, m_World};
			Ref<JoltBody> body = scene.GetBody(entity);
			const uint64_t hash = JoltShapes::ComputeAuthoredHash(entity);

			if (body && body->GetAuthoredHash() == hash)
			{
				// Unchanged authored data. Static/kinematic bodies follow an edited transform; a dynamic one
				// only while the simulation is NOT running (Edit mode), so Play starts from the edited pose.
				if ((!body->IsDynamic() || !simulating) && reg.WasChanged<WorldTransformComponent>(e))
				{
					glm::vec3 pos, scale;
					glm::quat rot;
					DecomposeTRS(reg.Read<WorldTransformComponent>(e).LocalToWorld, pos, rot, scale);
					body->SetTransform(pos, rot, simulating);
				}
				continue;
			}

			body = scene.CreateBody(entity, simulating);
			if (!body)
			{
				if (reg.any_of<PhysicsBodyRuntimeComponent>(e))
				{
					reg.remove<PhysicsBodyRuntimeComponent>(e);
				}
				continue; // a RigidBody with no collider anywhere yet
			}
			auto& rt = reg.Ensure<PhysicsBodyRuntimeComponent>(e);
			rt.Body = body;
			rt.Activated = simulating && !body->IsStatic();
			rt.HasPrev = false;
		}
	}

	// =================================================================================================
	// PhysicsStepSystem
	// =================================================================================================

	void PhysicsStepSystem::Execute(const Timestep ts)
	{
		if (!m_World->HasSingleton<JoltScene>())
		{
			return;
		}
		auto& scene = SingletonView<JoltScene>();
		auto& reg = m_World->GetRegistry();
		const float dt = ts.GetSeconds();

		for (const auto view = reg.view<PhysicsBodyRuntimeComponent>(); const entt::entity e : view)
		{
			auto& rt = reg.get<PhysicsBodyRuntimeComponent>(e);
			if (!rt.Body || !rt.Body->IsValid())
			{
				continue;
			}
			if (rt.Body->IsDynamic())
			{
				if (!rt.Activated)
				{
					rt.Body->SetSleepState(false); // authored asleep in Edit mode: wake on the first simulated step
					rt.Activated = true;
				}
				rt.PrevPosition = rt.Body->GetTranslation();
				rt.PrevRotation = rt.Body->GetRotation();
				rt.HasPrev = true;
			}
			else if (rt.Body->IsKinematic() && reg.any_of<WorldTransformComponent>(e))
			{
				glm::vec3 pos, scale;
				glm::quat rot;
				DecomposeTRS(reg.Read<WorldTransformComponent>(e).LocalToWorld, pos, rot, scale);
				rt.Body->MoveKinematic(pos, rot, dt);
			}
		}

		// The engine's job pool through the JoltJobSystem service; single-threaded without an Application
		// (unit tests, offline tools) — Jolt is deterministic either way.
		static JPH::JobSystemSingleThreaded s_SingleThreaded(JPH::cMaxPhysicsJobs);
		JPH::JobSystem* jobs = &s_SingleThreaded;
		if (Application::Exists() && Application::Get().GetServiceManager().ServiceRegistered<JoltJobSystem>())
		{
			jobs = &ServiceView<JoltJobSystem>();
		}
		scene.Simulate(dt, *jobs);

		if (PhysicsCVars::LogStats.Get() && ++m_StepsSinceLog >= 60)
		{
			m_StepsSinceLog = 0;
			SS_CORE_INFO("Physics: bodies={} active={} contacts={} dt={:.4f}", scene.GetBodyCount(), scene.GetActiveBodyCount(), scene.GetContactCountLastStep(), dt);
		}
	}

	// =================================================================================================
	// PhysicsWriteBackSystem
	// =================================================================================================

	void PhysicsWriteBackSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<JoltScene>())
		{
			return;
		}
		auto& reg = m_World->GetRegistry();
		const float alpha = m_World->GetSystemManager().FixedAlpha();
		const bool interpolate = PhysicsSystem::GetSettings().InterpolateBodies;

		for (const auto view = reg.view<PhysicsBodyRuntimeComponent, TransformComponent>(); const entt::entity e : view)
		{
			const auto& rt = reg.Read<PhysicsBodyRuntimeComponent>(e);
			if (!rt.Body || !rt.Body->IsValid() || !rt.Body->IsDynamic() || !rt.HasPrev)
			{
				continue;
			}
			glm::vec3 worldPos = rt.Body->GetTranslation();
			glm::quat worldRot = rt.Body->GetRotation();
			if (interpolate)
			{
				worldPos = glm::mix(rt.PrevPosition, worldPos, alpha);
				worldRot = glm::slerp(rt.PrevRotation, worldRot, alpha);
			}

			// World pose -> the entity's LOCAL transform (through the parent's inverse), keeping scale.
			const glm::mat4 world = glm::translate(glm::mat4(1.0f), worldPos) * glm::mat4_cast(worldRot);
			const Entity parent = m_World->GetParent(Entity{e, m_World});
			const glm::mat4 local = parent ? glm::inverse(m_World->ComputeWorldMatrix(parent)) * world : world;
			glm::vec3 lp, ls;
			glm::quat lr;
			if (!DecomposeTRS(local, lp, lr, ls))
			{
				continue;
			}
			const TransformComponent& cur = reg.Read<TransformComponent>(e);
			if (glm::distance(cur.Position, lp) < 1e-6f && std::abs(glm::dot(cur.Rotation, lr)) > 1.0f - 1e-7f)
			{
				continue; // at rest: don't dirty the transform (keeps culling/TLAS quiet)
			}
			reg.patch<TransformComponent>(e, [&](TransformComponent& tr)
			                              {
				tr.Position = lp;
				tr.Rotation = lr; });
		}
	}

	// =================================================================================================
	// PhysicsDebugDrawSystem
	// =================================================================================================

	void PhysicsDebugDrawSystem::Execute(Timestep)
	{
		if (!m_World->HasSingleton<DebugDrawSingleton>())
		{
			return;
		}
		auto& out = SingletonView<DebugDrawSingleton>();
		out.Clear(); // the editor drew last frame's lines already (UI phase precedes PreRender)
		if (PhysicsCVars::DebugDraw.Get() && m_World->HasSingleton<JoltScene>())
		{
			SingletonView<JoltScene>().DrawDebug(out);
		}
	}
}
