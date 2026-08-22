#pragma once

#include "JoltPhysics/JoltBody.hpp"
#include "JoltPhysics/JoltCharacterController.hpp"

#include <Snowstorm/Core/Base.hpp>
#include <Snowstorm/ECS/System.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Snowstorm
{
	// Runtime twin of the physics components: the live JoltBody plus the previous fixed-step pose the
	// write-back interpolates from. Never serialized / copied / shown.
	struct PhysicsBodyRuntimeComponent
	{
		Ref<JoltBody> Body;
		bool Activated = false; // bodies authored in Edit mode start asleep; woken on the first simulated step
		glm::vec3 PrevPosition{0.0f};
		glm::quat PrevRotation{1.0f, 0.0f, 0.0f, 0.0f};
		bool HasPrev = false;
	};

	// Runtime twin of CharacterControllerComponent: the live character. This is also the SCRIPT-FACING
	// handle -- a script does GetComponent<CharacterControllerRuntimeComponent>().Controller->Move(...),
	// the same way it reaches a rigid body through PhysicsBodyRuntimeComponent. Never serialized/copied.
	struct CharacterControllerRuntimeComponent
	{
		Ref<JoltCharacterController> Controller;
	};

	// Resolve phase, AFTER TransformSystem (order +10): authored RigidBody/collider components -> JoltScene
	// bodies. Rebuilds a body when its authored hash moved, follows an edited transform on static /
	// kinematic bodies (and on dynamic ones while not simulating, so Play starts from the edit), removes
	// bodies whose entity or components are gone. Runs in Edit mode too, for debug draw.
	class PhysicsBodySyncSystem final : public System
	{
	public:
		explicit PhysicsBodySyncSystem(const WorldRef world)
		    : System(world)
		{
		}
		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};

	// FixedUpdate phase: previous-pose snapshot, kinematic targets, wake-ups, then JoltScene::Simulate on
	// the engine's job pool (single-threaded Jolt job system without an Application).
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

	// Resolve phase, BEFORE TransformSystem (order -10): dynamic bodies' poses (interpolated by the
	// accumulator's alpha when PhysicsSettings::InterpolateBodies) -> the entity's LOCAL transform.
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

	// PreRender phase: physics.debug_draw -> every body's wireframe into DebugDrawSingleton.
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
