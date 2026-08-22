#pragma once

#include <Snowstorm/Physics/PhysicsTypes.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Body/MotionQuality.h>
#include <Jolt/Physics/Body/MotionType.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Snowstorm::JoltUtils
{
	// The unavoidable glm <-> Jolt math glue (Hazel JoltUtils). Nothing else is converted.
	inline JPH::Vec3 ToJoltVector(const glm::vec3& v) { return {v.x, v.y, v.z}; }
	inline JPH::Quat ToJoltQuat(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
	inline glm::vec3 FromJoltVector(const JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
	inline glm::quat FromJoltQuat(const JPH::Quat q) { return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()}; }

	inline JPH::EMotionType ToJoltMotionType(const EBodyType type)
	{
		switch (type)
		{
		case EBodyType::Dynamic:
			return JPH::EMotionType::Dynamic;
		case EBodyType::Kinematic:
			return JPH::EMotionType::Kinematic;
		default:
			return JPH::EMotionType::Static;
		}
	}

	inline JPH::EMotionQuality ToJoltMotionQuality(const ECollisionDetectionType type)
	{
		return type == ECollisionDetectionType::Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
	}
}
