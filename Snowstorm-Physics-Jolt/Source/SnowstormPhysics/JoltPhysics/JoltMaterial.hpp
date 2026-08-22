#pragma once

#include <Snowstorm/Physics/ColliderMaterial.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

namespace Snowstorm
{
	// A collider's surface response as a Jolt PhysicsMaterial (Hazel JoltMaterial): Jolt keeps friction /
	// restitution per BODY, so per-shape values are carried on the shape's material and combined by the
	// scene's combine functions (sub-shape aware, like PhysX/Hazel).
	class JoltMaterial final : public JPH::PhysicsMaterial
	{
	public:
		explicit JoltMaterial(const ColliderMaterial& material)
		    : Friction(material.Friction), Restitution(material.Restitution)
		{
		}

		[[nodiscard]] const char* GetDebugName() const override { return "Snowstorm::JoltMaterial"; }

		float Friction = 0.5f;
		float Restitution = 0.15f;

		static float CombineFriction(const JPH::Body& body1, const JPH::SubShapeID& sub1, const JPH::Body& body2, const JPH::SubShapeID& sub2);
		static float CombineRestitution(const JPH::Body& body1, const JPH::SubShapeID& sub1, const JPH::Body& body2, const JPH::SubShapeID& sub2);
	};
}
