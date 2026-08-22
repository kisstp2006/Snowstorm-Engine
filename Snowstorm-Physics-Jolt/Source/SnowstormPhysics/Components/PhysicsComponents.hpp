#pragma once

#include <Snowstorm/Assets/AssetTypes.hpp>

#include <glm/vec3.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Authored physics components (Unity Rigidbody + Collider family, Godot RigidBody3D + CollisionShape3D).
	// One RigidBodyComponent per simulated entity; any number of *ColliderComponents on it (compound) and
	// on its children (their local transforms fold into the body's compound shape). A collider without a
	// RigidBody anywhere up the hierarchy is a static body of its own.

	enum class MotionType : uint8_t
	{
		Static,    // never moves (level geometry)
		Kinematic, // moved by its transform, pushes dynamic bodies
		Dynamic,   // simulated
	};

	struct RigidBodyComponent
	{
		MotionType Motion = MotionType::Dynamic;
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityFactor = 1.0f;
		bool IsTrigger = false;       // sensor: overlaps report OnTrigger*, no collision response
		uint32_t CollisionLayer = 0u; // 0..31: which row of the collision matrix this body is in
		bool Interpolate = true;      // render between fixed steps (smooth at any frame rate)
	};

	struct BoxColliderComponent
	{
		glm::vec3 HalfExtents{1.0f}; // a 2x2x2 box: matches the engine's unit cube mesh (cube.obj spans -1..1)
		glm::vec3 Offset{0.0f};
	};

	struct SphereColliderComponent
	{
		float Radius = 1.0f;
		glm::vec3 Offset{0.0f};
	};

	struct CapsuleColliderComponent
	{
		float Radius = 0.25f;
		float HalfHeight = 0.5f; // of the cylindrical part
		glm::vec3 Offset{0.0f};
	};

	struct MeshColliderComponent
	{
		AssetHandle Mesh{0}; // 0 = use the entity's own MeshComponent
		bool Convex = false; // convex hull (needed for dynamic bodies) vs exact triangle mesh (static only)
	};
}
