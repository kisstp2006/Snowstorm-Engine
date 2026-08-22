#pragma once

#include <Snowstorm/ECS/System.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace Snowstorm
{
	class PhysicsWorldSingleton;
	struct RigidBodyComponent;

	// Resolve phase, AFTER TransformSystem (order +10): authored RigidBody/collider components -> live Jolt
	// bodies (PhysicsBodyRuntimeComponent). A body is (re)built when the authored hash changes (shape,
	// settings, world scale); static/kinematic bodies follow an edited transform; bodies whose entity or
	// component is gone are removed. Runs in Edit mode too so collider debug draw works while authoring.
	class PhysicsBodySyncSystem final : public System
	{
	public:
		explicit PhysicsBodySyncSystem(const WorldRef world)
		    : System(world)
		{
		}
		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return true; }

	private:
		JPH::RefConst<JPH::Shape> BuildShape(entt::entity body, glm::vec3& outScale, uint64_t& hash) const;
		void SyncEntity(PhysicsWorldSingleton& physics, entt::entity e, const RigidBodyComponent* rb, bool simulating);
		void RemoveStale(PhysicsWorldSingleton& physics) const;
	};

	// FixedUpdate phase: kinematic targets from the (authored) transforms, previous-pose snapshot for
	// interpolation, then PhysicsSystem::Update on the engine's job pool. Contacts reach ScriptEventQueue
	// from the listener during the step.
	class PhysicsStepSystem final : public System
	{
	public:
		explicit PhysicsStepSystem(const WorldRef world)
		    : System(world)
		{
		}
		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return false; }

	private:
		uint32_t m_StepsSinceLog = 0;
	};

	// Resolve phase, BEFORE TransformSystem (order -10): dynamic bodies' poses (interpolated between the
	// last two fixed steps by the accumulator's alpha when Interpolate is on) -> the entity's LOCAL
	// TransformComponent through the parent's inverse, marking it changed so culling/TLAS/velocity react.
	class PhysicsWriteBackSystem final : public System
	{
	public:
		explicit PhysicsWriteBackSystem(const WorldRef world)
		    : System(world)
		{
		}
		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return false; }
	};

	// PreRender phase: when physics.debug_draw is on, pushes every body's shape wireframe into the World's
	// DebugDrawSingleton (the editor viewport draws it); clears the list otherwise.
	class PhysicsDebugDrawSystem final : public System
	{
	public:
		explicit PhysicsDebugDrawSystem(const WorldRef world)
		    : System(world)
		{
		}
		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
