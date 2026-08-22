#pragma once

#include <Snowstorm/Core/Base.hpp>
#include <Snowstorm/World/Entity.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace Snowstorm
{
	class JoltScene;

	// One kinematic character bound to an entity (Hazel JoltCharacterController), backed by
	// JPH::CharacterVirtual: a shape that is swept through the world every step and slides along what it
	// hits, instead of a body the solver pushes. That is what every engine's character controller is
	// (Unity CharacterController, Unreal CharacterMovementComponent, Godot CharacterBody3D) and why a
	// player doesn't tip over, sink into ramps, or get shoved by a stack of crates.
	//
	// The movement model is "queue, then step": a script calls Move()/Rotate()/Jump() during the frame,
	// and the queued displacement is consumed by the next fixed step. Nothing here re-wraps Jolt; scripts
	// hold this object (via CharacterControllerRuntimeComponent) and call it directly.
	//
	// Deliberately NOT here yet: contact callbacks (JPH::CharacterContactListener). CharacterVirtual has
	// no BodyID, so its contacts can't go through JoltScene's BodyID-keyed contact routing without giving
	// characters their own event path -- and nothing consumes it yet. Grounded state covers the common case.
	class JoltCharacterController final
	{
	public:
		JoltCharacterController(JoltScene& scene, Entity entity);
		~JoltCharacterController();

		JoltCharacterController(const JoltCharacterController&) = delete;
		JoltCharacterController& operator=(const JoltCharacterController&) = delete;

		// Drop the Jolt character now (the scene is going away, or the entity stopped being a character).
		// Safe to call twice; a released controller answers IsValid() == false and every call is a no-op.
		void Release();

		[[nodiscard]] bool IsValid() const { return m_Controller != nullptr; }
		[[nodiscard]] Entity GetEntity() const { return m_Entity; }
		// Fingerprint of the authored inputs it was built from (collider set + transform scale), so the
		// sync system rebuilds it only when something it was built from actually changed. Same contract as
		// JoltBody::GetAuthoredHash.
		[[nodiscard]] uint64_t GetAuthoredHash() const { return m_AuthoredHash; }

		//// Configuration ////
		void SetGravityEnabled(bool enableGravity) { m_GravityEnabled = enableGravity; }
		[[nodiscard]] bool IsGravityEnabled() const { return m_GravityEnabled; }
		void SetSlopeLimit(float slopeLimitDeg);
		void SetStepOffset(float stepOffset) { m_StepOffset = stepOffset; }
		void SetControlMovementInAir(const bool control) { m_ControlMovementInAir = control; }
		[[nodiscard]] bool CanControlMovementInAir() const { return m_ControlMovementInAir; }
		void SetControlRotationInAir(const bool control) { m_ControlRotationInAir = control; }
		[[nodiscard]] bool CanControlRotationInAir() const { return m_ControlRotationInAir; }
		void SetCollisionLayer(uint32_t layerID) { m_LayerID = layerID; }

		//// State ////
		[[nodiscard]] bool IsGrounded() const;

		[[nodiscard]] glm::vec3 GetTranslation() const;
		[[nodiscard]] glm::quat GetRotation() const;
		// Teleport: places the character immediately, ignoring collision along the way.
		void SetTranslation(const glm::vec3& translation);
		void SetRotation(const glm::quat& rotation);

		[[nodiscard]] glm::vec3 GetLinearVelocity() const;
		void SetLinearVelocity(const glm::vec3& velocity);

		//// Input (consumed by the next fixed step) ////
		// Desired movement for the coming step, in world units. Repeated calls in one frame accumulate.
		void Move(const glm::vec3& displacement);
		void Rotate(const glm::quat& rotation);
		// Upward velocity applied on the next step, if the character is standing on something.
		void Jump(float jumpPower);

		//// Simulation ////
		// One fixed step: turn the queued input into a velocity, integrate gravity, sweep the character.
		void Simulate(float fixedDt);

	private:
		JoltScene* m_Scene = nullptr;
		Entity m_Entity;
		JPH::Ref<JPH::CharacterVirtual> m_Controller;
		JPH::RefConst<JPH::Shape> m_Shape;
		uint64_t m_AuthoredHash = 0;
		uint32_t m_LayerID = 0;

		bool m_GravityEnabled = true;
		bool m_ControlMovementInAir = false;
		bool m_ControlRotationInAir = false;
		float m_StepOffset = 0.3f;

		// Queued input, cleared by Simulate.
		glm::vec3 m_Displacement{0.0f};
		glm::quat m_QueuedRotation{1.0f, 0.0f, 0.0f, 0.0f};
		bool m_HasQueuedRotation = false;
		float m_JumpPower = 0.0f;
	};
}
