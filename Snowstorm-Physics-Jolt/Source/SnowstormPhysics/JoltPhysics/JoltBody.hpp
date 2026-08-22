#pragma once

#include <Snowstorm/Core/Base.hpp>
#include <Snowstorm/Physics/PhysicsTypes.hpp>
#include <Snowstorm/World/Entity.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace JPH
{
	class BodyInterface;
}

namespace Snowstorm
{
	class JoltScene;

	// One rigid body bound to an entity (Hazel JoltBody): built by JoltScene::CreateBody from the
	// entity's RigidBodyComponent + colliders (JoltShapes), then driven through the Jolt BodyInterface.
	// Scripts and the editor talk to this; nothing above it re-wraps Jolt.
	class JoltBody final
	{
	public:
		JoltBody(JoltScene& scene, Entity entity, bool activate);
		~JoltBody();

		[[nodiscard]] Entity GetEntity() const { return m_Entity; }
		[[nodiscard]] JPH::BodyID GetBodyID() const { return m_BodyID; }
		[[nodiscard]] bool IsValid() const { return !m_BodyID.IsInvalid(); }
		[[nodiscard]] uint64_t GetAuthoredHash() const { return m_AuthoredHash; }

		// Remove the body from the simulation now (the scene is going away or the entity stopped being a
		// body). Safe to call twice; a released body answers IsValid() == false and every call is a no-op.
		void Release();

		void SetCollisionLayer(uint32_t layerID);

		[[nodiscard]] bool IsStatic() const;
		[[nodiscard]] bool IsDynamic() const;
		[[nodiscard]] bool IsKinematic() const;

		void MoveKinematic(const glm::vec3& targetPosition, const glm::quat& targetRotation, float deltaSeconds);

		[[nodiscard]] bool GetGravityEnabled() const;
		void SetGravityEnabled(bool isEnabled);

		void AddForce(const glm::vec3& force, EForceMode forceMode = EForceMode::Force, bool forceWake = true);
		void AddForce(const glm::vec3& force, const glm::vec3& location, EForceMode forceMode = EForceMode::Force, bool forceWake = true);
		void AddTorque(const glm::vec3& torque, bool forceWake = true);

		void ChangeTriggerState(bool isTrigger);
		[[nodiscard]] bool IsTrigger() const;

		[[nodiscard]] float GetMass() const;
		void SetMass(float mass);
		void SetLinearDrag(float linearDrag);
		void SetAngularDrag(float angularDrag);

		[[nodiscard]] glm::vec3 GetLinearVelocity() const;
		void SetLinearVelocity(const glm::vec3& velocity);
		[[nodiscard]] glm::vec3 GetAngularVelocity() const;
		void SetAngularVelocity(const glm::vec3& velocity);
		[[nodiscard]] float GetMaxLinearVelocity() const;
		void SetMaxLinearVelocity(float maxVelocity);
		[[nodiscard]] float GetMaxAngularVelocity() const;
		void SetMaxAngularVelocity(float maxVelocity);

		[[nodiscard]] bool IsSleeping() const;
		void SetSleepState(bool sleep);

		void SetCollisionDetectionMode(ECollisionDetectionType mode);

		[[nodiscard]] glm::vec3 GetTranslation() const;
		[[nodiscard]] glm::quat GetRotation() const;
		// Static/kinematic bodies (the editor gizmo, Teleport): move without simulating.
		void SetTranslation(const glm::vec3& translation);
		void SetRotation(const glm::quat& rotation);
		void SetTransform(const glm::vec3& translation, const glm::quat& rotation, bool activate);

		void OnAxisLockUpdated(bool forceWake);

	private:
		[[nodiscard]] JPH::BodyInterface& Bodies() const;

		JoltScene& m_Scene;
		Entity m_Entity;
		JPH::BodyID m_BodyID;
		JPH::RefConst<JPH::Shape> m_Shape;
		uint64_t m_AuthoredHash = 0;
		uint32_t m_LockedAxes = 0;
	};
}
