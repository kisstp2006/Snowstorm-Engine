#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Runtime twin of RigidBodyComponent (+ the colliders that fed its compound shape): the live Jolt body.
	// Owned by PhysicsBodySyncSystem; never serialized, copied, or shown. AuthoredHash is what the body was
	// built from — a different hash means the shape / body settings are rebuilt.
	struct PhysicsBodyRuntimeComponent
	{
		JPH::BodyID Body;
		JPH::RefConst<JPH::Shape> Shape;
		uint64_t AuthoredHash = 0;
		bool Dynamic = false;
		bool Activated = false; // woken once the simulation runs (bodies made in Edit mode start asleep)

		// Previous fixed-step pose, for render interpolation between steps (RigidBodyComponent::Interpolate).
		glm::vec3 PrevPosition{0.0f};
		glm::quat PrevRotation{1.0f, 0.0f, 0.0f, 0.0f};
		bool HasPrev = false;
	};
}
