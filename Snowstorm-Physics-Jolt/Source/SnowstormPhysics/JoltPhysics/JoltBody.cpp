#include "JoltBody.hpp"

#include "JoltLayerInterface.hpp"
#include "JoltScene.hpp"
#include "JoltShapes.hpp"
#include "JoltUtils.hpp"

#include <Snowstorm/Components/PhysicsComponents.hpp>
#include <Snowstorm/Components/WorldTransformComponent.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/Math/Transform.hpp>
#include <Snowstorm/Physics/PhysicsSystem.hpp>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace Snowstorm
{
	namespace
	{
		JPH::EAllowedDOFs ToAllowedDOFs(const uint32_t lockedAxes)
		{
			// Our EActorAxis bits are the LOCKED axes; Jolt wants the ALLOWED ones, same bit layout.
			const auto allowed = static_cast<JPH::uint8>(0b111111u & ~lockedAxes);
			return allowed == 0 ? JPH::EAllowedDOFs::All : static_cast<JPH::EAllowedDOFs>(allowed);
		}
	}

	JoltBody::JoltBody(JoltScene& scene, const Entity entity, const bool activate)
	    : m_Scene(scene), m_Entity(entity)
	{
		const RigidBodyComponent* rb = entity.TryGetComponent<RigidBodyComponent>();
		const EBodyType bodyType = rb ? rb->BodyType : EBodyType::Static; // collider-only entity = static body
		const bool isStatic = bodyType == EBodyType::Static;

		m_Shape = JoltShapes::BuildBodyShape(entity, isStatic, m_AuthoredHash);
		if (!m_Shape)
		{
			return; // nothing to collide with yet (RigidBody without colliders)
		}

		glm::vec3 pos, scale;
		glm::quat rot;
		DecomposeTRS(entity.GetWorld()->ComputeWorldMatrix(entity), pos, rot, scale);

		const uint32_t layerID = rb ? rb->LayerID : 0u;
		JPH::BodyCreationSettings settings(m_Shape, JoltUtils::ToJoltVector(pos), JoltUtils::ToJoltQuat(rot),
		                                   JoltUtils::ToJoltMotionType(bodyType), JoltLayers::ToObjectLayer(layerID, !isStatic));
		settings.mUserData = static_cast<JPH::uint64>(entity.Handle());
		if (rb)
		{
			settings.mLinearDamping = rb->LinearDrag;
			settings.mAngularDamping = rb->AngularDrag;
			settings.mGravityFactor = rb->DisableGravity ? 0.0f : 1.0f;
			settings.mIsSensor = rb->IsTrigger;
			settings.mMotionQuality = JoltUtils::ToJoltMotionQuality(rb->CollisionDetection);
			settings.mMaxLinearVelocity = rb->MaxLinearVelocity;
			settings.mMaxAngularVelocity = rb->MaxAngularVelocity;
			settings.mAllowDynamicOrKinematic = rb->EnableDynamicTypeChange;
			settings.mLinearVelocity = JoltUtils::ToJoltVector(rb->InitialLinearVelocity);
			settings.mAngularVelocity = JoltUtils::ToJoltVector(rb->InitialAngularVelocity);
			m_LockedAxes = rb->LockedAxes;
			if (bodyType == EBodyType::Dynamic)
			{
				settings.mAllowedDOFs = ToAllowedDOFs(rb->LockedAxes);
				settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				settings.mMassPropertiesOverride.mMass = std::max(rb->Mass, 0.001f);
			}
		}

		m_BodyID = Bodies().CreateAndAddBody(settings, (!isStatic && activate) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		if (m_BodyID.IsInvalid())
		{
			SS_CORE_ERROR("JoltBody: creation failed for entity {} (raise PhysicsSettings::MaxBodies?).", static_cast<uint32_t>(entity.Handle()));
		}
	}

	JoltBody::~JoltBody()
	{
		Release();
	}

	void JoltBody::Release()
	{
		if (m_BodyID.IsInvalid())
		{
			return;
		}
		Bodies().RemoveBody(m_BodyID);
		Bodies().DestroyBody(m_BodyID);
		m_BodyID = JPH::BodyID();
	}

	JPH::BodyInterface& JoltBody::Bodies() const
	{
		return m_Scene.GetJoltSystem().GetBodyInterface();
	}

	void JoltBody::SetCollisionLayer(const uint32_t layerID)
	{
		Bodies().SetObjectLayer(m_BodyID, JoltLayers::ToObjectLayer(layerID, !IsStatic()));
	}

	bool JoltBody::IsStatic() const { return Bodies().GetMotionType(m_BodyID) == JPH::EMotionType::Static; }
	bool JoltBody::IsDynamic() const { return Bodies().GetMotionType(m_BodyID) == JPH::EMotionType::Dynamic; }
	bool JoltBody::IsKinematic() const { return Bodies().GetMotionType(m_BodyID) == JPH::EMotionType::Kinematic; }

	void JoltBody::MoveKinematic(const glm::vec3& targetPosition, const glm::quat& targetRotation, const float deltaSeconds)
	{
		if (!IsKinematic())
		{
			SS_CORE_WARN("JoltBody::MoveKinematic on a non-kinematic body (entity {}).", static_cast<uint32_t>(m_Entity.Handle()));
			return;
		}
		Bodies().MoveKinematic(m_BodyID, JoltUtils::ToJoltVector(targetPosition), JoltUtils::ToJoltQuat(targetRotation), deltaSeconds);
	}

	bool JoltBody::GetGravityEnabled() const { return Bodies().GetGravityFactor(m_BodyID) > 0.0f; }
	void JoltBody::SetGravityEnabled(const bool isEnabled) { Bodies().SetGravityFactor(m_BodyID, isEnabled ? 1.0f : 0.0f); }

	void JoltBody::AddForce(const glm::vec3& force, const EForceMode forceMode, const bool forceWake)
	{
		const JPH::EActivation act = forceWake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		switch (forceMode)
		{
		case EForceMode::Force:
			Bodies().AddForce(m_BodyID, JoltUtils::ToJoltVector(force), act);
			break;
		case EForceMode::Impulse:
			Bodies().AddImpulse(m_BodyID, JoltUtils::ToJoltVector(force));
			break;
		case EForceMode::VelocityChange:
			Bodies().AddLinearVelocity(m_BodyID, JoltUtils::ToJoltVector(force));
			break;
		case EForceMode::Acceleration:
			Bodies().AddForce(m_BodyID, JoltUtils::ToJoltVector(force * GetMass()), act);
			break;
		}
	}

	void JoltBody::AddForce(const glm::vec3& force, const glm::vec3& location, const EForceMode forceMode, const bool forceWake)
	{
		const JPH::EActivation act = forceWake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
		switch (forceMode)
		{
		case EForceMode::Impulse:
			Bodies().AddImpulse(m_BodyID, JoltUtils::ToJoltVector(force), JoltUtils::ToJoltVector(location));
			break;
		case EForceMode::Acceleration:
			Bodies().AddForce(m_BodyID, JoltUtils::ToJoltVector(force * GetMass()), JoltUtils::ToJoltVector(location), act);
			break;
		default:
			Bodies().AddForce(m_BodyID, JoltUtils::ToJoltVector(force), JoltUtils::ToJoltVector(location), act);
			break;
		}
	}

	void JoltBody::AddTorque(const glm::vec3& torque, const bool forceWake)
	{
		Bodies().AddTorque(m_BodyID, JoltUtils::ToJoltVector(torque), forceWake ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	void JoltBody::ChangeTriggerState(const bool isTrigger) { Bodies().SetIsSensor(m_BodyID, isTrigger); }
	bool JoltBody::IsTrigger() const { return Bodies().IsSensor(m_BodyID); }

	float JoltBody::GetMass() const
	{
		JPH::BodyLockRead lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
		{
			return 0.0f;
		}
		const float invMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
		return invMass > 0.0f ? 1.0f / invMass : 0.0f;
	}

	void JoltBody::SetMass(const float mass)
	{
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
		{
			return;
		}
		JPH::MassProperties props = lock.GetBody().GetShape()->GetMassProperties();
		props.ScaleToMass(std::max(mass, 0.001f));
		lock.GetBody().GetMotionProperties()->SetMassProperties(ToAllowedDOFs(m_LockedAxes), props);
	}

	void JoltBody::SetLinearDrag(const float linearDrag)
	{
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (lock.Succeeded() && !lock.GetBody().IsStatic())
			lock.GetBody().GetMotionProperties()->SetLinearDamping(std::max(linearDrag, 0.0f));
	}

	void JoltBody::SetAngularDrag(const float angularDrag)
	{
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (lock.Succeeded() && !lock.GetBody().IsStatic())
			lock.GetBody().GetMotionProperties()->SetAngularDamping(std::max(angularDrag, 0.0f));
	}

	glm::vec3 JoltBody::GetLinearVelocity() const { return JoltUtils::FromJoltVector(Bodies().GetLinearVelocity(m_BodyID)); }
	void JoltBody::SetLinearVelocity(const glm::vec3& velocity) { Bodies().SetLinearVelocity(m_BodyID, JoltUtils::ToJoltVector(velocity)); }
	glm::vec3 JoltBody::GetAngularVelocity() const { return JoltUtils::FromJoltVector(Bodies().GetAngularVelocity(m_BodyID)); }
	void JoltBody::SetAngularVelocity(const glm::vec3& velocity) { Bodies().SetAngularVelocity(m_BodyID, JoltUtils::ToJoltVector(velocity)); }

	float JoltBody::GetMaxLinearVelocity() const
	{
		JPH::BodyLockRead lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		return (lock.Succeeded() && !lock.GetBody().IsStatic()) ? lock.GetBody().GetMotionProperties()->GetMaxLinearVelocity() : 0.0f;
	}
	void JoltBody::SetMaxLinearVelocity(const float maxVelocity)
	{
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (lock.Succeeded() && !lock.GetBody().IsStatic())
			lock.GetBody().GetMotionProperties()->SetMaxLinearVelocity(std::max(maxVelocity, 0.0f));
	}
	float JoltBody::GetMaxAngularVelocity() const
	{
		JPH::BodyLockRead lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		return (lock.Succeeded() && !lock.GetBody().IsStatic()) ? lock.GetBody().GetMotionProperties()->GetMaxAngularVelocity() : 0.0f;
	}
	void JoltBody::SetMaxAngularVelocity(const float maxVelocity)
	{
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (lock.Succeeded() && !lock.GetBody().IsStatic())
			lock.GetBody().GetMotionProperties()->SetMaxAngularVelocity(std::max(maxVelocity, 0.0f));
	}

	bool JoltBody::IsSleeping() const { return !Bodies().IsActive(m_BodyID); }
	void JoltBody::SetSleepState(const bool sleep)
	{
		if (sleep)
			Bodies().DeactivateBody(m_BodyID);
		else
			Bodies().ActivateBody(m_BodyID);
	}

	void JoltBody::SetCollisionDetectionMode(const ECollisionDetectionType mode) { Bodies().SetMotionQuality(m_BodyID, JoltUtils::ToJoltMotionQuality(mode)); }

	glm::vec3 JoltBody::GetTranslation() const { return JoltUtils::FromJoltVector(Bodies().GetPosition(m_BodyID)); }
	glm::quat JoltBody::GetRotation() const { return JoltUtils::FromJoltQuat(Bodies().GetRotation(m_BodyID)); }
	void JoltBody::SetTranslation(const glm::vec3& translation) { Bodies().SetPosition(m_BodyID, JoltUtils::ToJoltVector(translation), JPH::EActivation::Activate); }
	void JoltBody::SetRotation(const glm::quat& rotation) { Bodies().SetRotation(m_BodyID, JoltUtils::ToJoltQuat(rotation), JPH::EActivation::Activate); }
	void JoltBody::SetTransform(const glm::vec3& translation, const glm::quat& rotation, const bool activate)
	{
		Bodies().SetPositionAndRotationWhenChanged(m_BodyID, JoltUtils::ToJoltVector(translation), JoltUtils::ToJoltQuat(rotation), activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
	}

	void JoltBody::OnAxisLockUpdated(const bool forceWake)
	{
		if (const RigidBodyComponent* rb = m_Entity.TryGetComponent<RigidBodyComponent>())
		{
			m_LockedAxes = rb->LockedAxes;
		}
		JPH::BodyLockWrite lock(m_Scene.GetJoltSystem().GetBodyLockInterface(), m_BodyID);
		if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
		{
			return;
		}
		JPH::MotionProperties* props = lock.GetBody().GetMotionProperties();
		JPH::MassProperties mass = lock.GetBody().GetShape()->GetMassProperties();
		mass.ScaleToMass(std::max(GetMass(), 0.001f));
		props->SetMassProperties(ToAllowedDOFs(m_LockedAxes), mass);
		if (forceWake)
		{
			Bodies().ActivateBody(m_BodyID);
		}
	}
}
