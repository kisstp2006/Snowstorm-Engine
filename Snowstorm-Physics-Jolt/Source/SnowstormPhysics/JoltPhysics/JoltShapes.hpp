#pragma once

#include <Snowstorm/World/Entity.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <cstdint>

namespace Snowstorm
{
	// Builds a body's Jolt shape from the entity's collider components (Hazel JoltShapes): Box / Sphere /
	// Capsule / Mesh colliders on the entity, plus on its child entities that have no RigidBody of their
	// own (CompoundColliderComponent::IncludeStaticChildColliders semantics), folded into one
	// StaticCompoundShape in body space; the body's world scale is baked with a ScaledShape.
	//
	// `outAuthoredHash` fingerprints every input (collider fields, materials, relative transforms, scale)
	// so the body is rebuilt only when something it was built from changed.
	namespace JoltShapes
	{
		JPH::RefConst<JPH::Shape> BuildBodyShape(Entity body, bool bodyIsStatic, uint64_t& outAuthoredHash);
		// The same fingerprint without building anything: the per-frame "did anything change" check.
		uint64_t ComputeAuthoredHash(Entity body);
	}
}
