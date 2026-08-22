#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Physics/ColliderMaterial.hpp"
#include "Snowstorm/Physics/PhysicsTypes.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Authored physics components (Hazel Components.h: RigidBodyComponent + the collider family). Live in
	// Core so scenes serialize without the physics module; the Jolt module turns them into bodies.

	struct RigidBodyComponent
	{
		EBodyType BodyType = EBodyType::Static;
		uint32_t LayerID = 0; // PhysicsLayerManager layer
		bool EnableDynamicTypeChange = false;

		float Mass = 1.0f;
		float LinearDrag = 0.01f;
		float AngularDrag = 0.05f;
		bool DisableGravity = false;
		bool IsTrigger = false;
		ECollisionDetectionType CollisionDetection = ECollisionDetectionType::Discrete;

		glm::vec3 InitialLinearVelocity{0.0f};
		glm::vec3 InitialAngularVelocity{0.0f};

		float MaxLinearVelocity = 500.0f;
		float MaxAngularVelocity = 50.0f;

		uint32_t LockedAxes = 0; // EActorAxis bits (stored as a mask so the inspector/JSON treat it as flags)
	};

	// A kinematic character (Hazel CharacterControllerComponent). NOT a rigid body: the solver never
	// pushes it -- a script moves it explicitly (Move/Jump) and it slides along geometry, walks up steps
	// and refuses slopes steeper than SlopeLimitDeg. Same model as Unity's CharacterController, Unreal's
	// CharacterMovementComponent and Godot's CharacterBody3D, and backed by JPH::CharacterVirtual.
	// The entity's collider components define its shape (a capsule, normally); a RigidBodyComponent on the
	// same entity is redundant -- the character owns the movement.
	struct CharacterControllerComponent
	{
		float SlopeLimitDeg = 45.0f; // steepest slope it can still walk up
		float StepOffset = 0.3f;     // step height it climbs without jumping
		uint32_t LayerID = 0;        // PhysicsLayerManager layer
		bool DisableGravity = false;
		bool ControlMovementInAir = false; // can Move() steer while airborne
		bool ControlRotationInAir = false; // can Rotate() turn while airborne
	};

	// Groups child entities' colliders into this entity's body. Without it, child colliders of a
	// RigidBody entity still fold in when IncludeStaticChildColliders semantics apply (the default walk);
	// with it, the listed entities are compounded explicitly.
	struct CompoundColliderComponent
	{
		bool IncludeStaticChildColliders = true;
		bool IsImmutable = true;
		std::vector<UUID> CompoundedColliderEntities;
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfSize{0.5f};
		glm::vec3 Offset{0.0f};
		ColliderMaterial Material;
	};

	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Offset{0.0f};
		ColliderMaterial Material;
	};

	struct CapsuleColliderComponent
	{
		float Radius = 0.5f;
		float HalfHeight = 0.5f;
		glm::vec3 Offset{0.0f};
		ColliderMaterial Material;
	};

	struct MeshColliderComponent
	{
		AssetHandle ColliderAsset{0}; // mesh asset to cook from; 0 = this entity's MeshComponent
		uint32_t SubmeshIndex = 0;
		bool UseSharedShape = false;
		ColliderMaterial Material;
		ECollisionComplexity CollisionComplexity = ECollisionComplexity::Default;
	};
}
