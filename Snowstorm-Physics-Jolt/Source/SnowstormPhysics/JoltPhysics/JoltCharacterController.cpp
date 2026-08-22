#include "JoltCharacterController.hpp"

#include "JoltLayerInterface.hpp"
#include "JoltScene.hpp"
#include "JoltShapes.hpp"
#include "JoltUtils.hpp"

#include <Snowstorm/Components/PhysicsComponents.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Math/Transform.hpp>
#include <Snowstorm/World/World.hpp>

#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/gtc/constants.hpp>

namespace Snowstorm
{
	JoltCharacterController::JoltCharacterController(JoltScene& scene, const Entity entity)
	    : m_Scene(&scene), m_Entity(entity)
	{
		const CharacterControllerComponent* cc = entity.TryGetComponent<CharacterControllerComponent>();
		if (!cc)
		{
			return;
		}

		// Same shape build as a rigid body (colliders on the entity + its RigidBody-less children, world
		// scale baked), so a character is authored with the ordinary collider components.
		m_Shape = JoltShapes::BuildBodyShape(entity, /*bodyIsStatic=*/false, m_AuthoredHash);
		if (!m_Shape)
		{
			SS_CORE_WARN("JoltCharacterController: an entity has a CharacterControllerComponent but no collider; "
			             "add a Capsule/Box/Sphere collider.");
			return;
		}

		glm::vec3 position, scale;
		glm::quat rotation;
		DecomposeTRS(entity.GetWorld()->ComputeWorldMatrix(entity), position, rotation, scale);

		m_GravityEnabled = !cc->DisableGravity;
		m_ControlMovementInAir = cc->ControlMovementInAir;
		m_ControlRotationInAir = cc->ControlRotationInAir;
		m_StepOffset = cc->StepOffset;
		m_LayerID = cc->LayerID;

		JPH::CharacterVirtualSettings settings;
		settings.mShape = m_Shape;
		settings.mMaxSlopeAngle = glm::radians(glm::clamp(cc->SlopeLimitDeg, 0.0f, 89.0f));
		// Contacts whose normal points more sideways than this plane don't count as "ground" -- without it
		// a character brushing a wall reports grounded and can jump off it.
		settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -0.05f);
		settings.mMass = 1.0f;

		m_Controller = new JPH::CharacterVirtual(&settings, JoltUtils::ToJoltVector(position),
		                                         JoltUtils::ToJoltQuat(rotation), &scene.GetJoltSystem());
	}

	JoltCharacterController::~JoltCharacterController()
	{
		Release();
	}

	void JoltCharacterController::Release()
	{
		m_Controller = nullptr; // JPH::Ref -- dropping the last reference destroys the character
		m_Shape = nullptr;
	}

	void JoltCharacterController::SetSlopeLimit(const float slopeLimitDeg)
	{
		if (m_Controller)
		{
			m_Controller->SetMaxSlopeAngle(glm::radians(glm::clamp(slopeLimitDeg, 0.0f, 89.0f)));
		}
	}

	bool JoltCharacterController::IsGrounded() const
	{
		return m_Controller && m_Controller->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
	}

	glm::vec3 JoltCharacterController::GetTranslation() const
	{
		return m_Controller ? JoltUtils::FromJoltVector(m_Controller->GetPosition()) : glm::vec3(0.0f);
	}

	glm::quat JoltCharacterController::GetRotation() const
	{
		return m_Controller ? JoltUtils::FromJoltQuat(m_Controller->GetRotation()) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}

	void JoltCharacterController::SetTranslation(const glm::vec3& translation)
	{
		if (m_Controller)
		{
			m_Controller->SetPosition(JoltUtils::ToJoltVector(translation));
		}
	}

	void JoltCharacterController::SetRotation(const glm::quat& rotation)
	{
		if (m_Controller)
		{
			m_Controller->SetRotation(JoltUtils::ToJoltQuat(rotation));
		}
	}

	glm::vec3 JoltCharacterController::GetLinearVelocity() const
	{
		return m_Controller ? JoltUtils::FromJoltVector(m_Controller->GetLinearVelocity()) : glm::vec3(0.0f);
	}

	void JoltCharacterController::SetLinearVelocity(const glm::vec3& velocity)
	{
		if (m_Controller)
		{
			m_Controller->SetLinearVelocity(JoltUtils::ToJoltVector(velocity));
		}
	}

	void JoltCharacterController::Move(const glm::vec3& displacement)
	{
		m_Displacement += displacement;
	}

	void JoltCharacterController::Rotate(const glm::quat& rotation)
	{
		m_QueuedRotation = m_HasQueuedRotation ? glm::normalize(rotation * m_QueuedRotation) : rotation;
		m_HasQueuedRotation = true;
	}

	void JoltCharacterController::Jump(const float jumpPower)
	{
		m_JumpPower = jumpPower;
	}

	void JoltCharacterController::Simulate(const float fixedDt)
	{
		if (!m_Controller || fixedDt <= 0.0f)
		{
			return;
		}

		JPH::PhysicsSystem& system = m_Scene->GetJoltSystem();
		const JPH::Vec3 gravity = m_GravityEnabled ? system.GetGravity() : JPH::Vec3::sZero();
		const bool grounded = IsGrounded();

		// Keep the vertical velocity Jolt gave us (gravity accumulation / jump arc) and replace the
		// horizontal part with what the script asked for. Turning a displacement into a velocity is what
		// makes Move() frame-rate independent: the same call every fixed step is a constant speed.
		JPH::Vec3 velocity = m_Controller->GetLinearVelocity();
		if (grounded || m_ControlMovementInAir)
		{
			const JPH::Vec3 desired = JoltUtils::ToJoltVector(m_Displacement) / fixedDt;
			velocity = JPH::Vec3(desired.GetX(), velocity.GetY(), desired.GetZ());
		}

		if (grounded)
		{
			// Standing on something: stop accumulating downward speed, or the character builds up a huge
			// (harmless but wrong) velocity while idle and shoots off the first ledge it walks over.
			if (velocity.GetY() < 0.0f)
			{
				velocity.SetY(0.0f);
			}
			if (m_JumpPower > 0.0f)
			{
				velocity.SetY(m_JumpPower);
			}
		}
		m_JumpPower = 0.0f;

		velocity += gravity * fixedDt;
		m_Controller->SetLinearVelocity(velocity);

		if (m_HasQueuedRotation && (grounded || m_ControlRotationInAir))
		{
			m_Controller->SetRotation(JoltUtils::ToJoltQuat(m_QueuedRotation));
		}
		m_HasQueuedRotation = false;

		// ExtendedUpdate (rather than Update) is what walks stairs and sticks the character to the floor
		// on the way down a slope; both need to know which way is up and how tall a step may be.
		JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
		updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, glm::max(m_StepOffset, 0.0f), 0.0f);
		updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -glm::max(m_StepOffset, 0.0f), 0.0f);

		const JPH::ObjectLayer objectLayer = JoltLayers::ToObjectLayer(m_LayerID, /*moving=*/true);
		m_Controller->ExtendedUpdate(fixedDt, gravity, updateSettings,
		                             system.GetDefaultBroadPhaseLayerFilter(objectLayer),
		                             system.GetDefaultLayerFilter(objectLayer), {}, {},
		                             m_Scene->GetTempAllocator());

		m_Displacement = glm::vec3(0.0f);
	}
}
