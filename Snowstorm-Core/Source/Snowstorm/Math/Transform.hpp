#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace Snowstorm
{
	// Orientation helpers shared by TransformComponent, the camera controllers, the gizmo and the
	// serializer's Euler->quaternion migration. The engine's Euler convention (kept for the inspector and
	// the legacy v1 scene format) is Y(yaw) -> X(pitch) -> Z(roll) applied as Ry * Rx * Rz, stored as
	// vec3(pitch, yaw, roll) in radians — the same order TransformComponent composed before rotations
	// became quaternions, so every v1 scene decodes bit-for-bit.

	// vec3(pitch, yaw, roll) radians -> quaternion, YXZ order.
	[[nodiscard]] inline glm::quat QuatFromEulerRadians(const glm::vec3& pitchYawRoll)
	{
		return glm::quat_cast(glm::eulerAngleYXZ(pitchYawRoll.y, pitchYawRoll.x, pitchYawRoll.z));
	}

	// quaternion -> vec3(pitch, yaw, roll) radians, YXZ order (inverse of QuatFromEulerRadians).
	[[nodiscard]] inline glm::vec3 EulerRadiansFromQuat(const glm::quat& q)
	{
		float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
		glm::extractEulerAngleYXZ(glm::mat4_cast(q), yaw, pitch, roll);
		return {pitch, yaw, roll};
	}

	[[nodiscard]] inline glm::quat QuatFromEulerDegrees(const glm::vec3& pitchYawRollDeg)
	{
		return QuatFromEulerRadians(glm::radians(pitchYawRollDeg));
	}

	[[nodiscard]] inline glm::vec3 EulerDegreesFromQuat(const glm::quat& q)
	{
		return glm::degrees(EulerRadiansFromQuat(q));
	}

	// Level-horizon look orientation from pitch/yaw (roll = 0): what the fly camera, orbit path and
	// framing code produce.
	[[nodiscard]] inline glm::quat QuatFromPitchYaw(const float pitch, const float yaw)
	{
		return QuatFromEulerRadians({pitch, yaw, 0.0f});
	}

	// The engine looks down -Z in local space (cameras and spot lights alike).
	[[nodiscard]] inline glm::vec3 ForwardFromQuat(const glm::quat& q)
	{
		return q * glm::vec3(0.0f, 0.0f, -1.0f);
	}

	// Orientation that looks along `forward` with a level horizon (pitch/yaw only).
	[[nodiscard]] inline glm::quat QuatLookingAlong(const glm::vec3& forward)
	{
		const glm::vec3 f = glm::normalize(forward);
		const float yaw = std::atan2(-f.x, -f.z);
		const float pitch = std::asin(glm::clamp(f.y, -1.0f, 1.0f));
		return QuatFromPitchYaw(pitch, yaw);
	}

	// Split a TRS matrix back into its parts (shear/perspective discarded). Used when converting a
	// world-space edit (gizmo, reparent-keep-world) back into a local TransformComponent.
	inline bool DecomposeTRS(const glm::mat4& m, glm::vec3& outTranslation, glm::quat& outRotation, glm::vec3& outScale)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		if (!glm::decompose(m, outScale, outRotation, outTranslation, skew, perspective))
		{
			return false;
		}
		outRotation = glm::normalize(outRotation);
		return true;
	}
}
