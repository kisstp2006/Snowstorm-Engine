#pragma once

#include <glm/vec3.hpp>

namespace Snowstorm
{
	// Runtime-only state for CameraControllerSystem.
	struct CameraControllerRuntimeComponent
	{
		bool WasRightClickHeld = false;
		bool Initialized = false;

		// Target pitch/yaw (radians) the mouse drives directly; the TransformComponent's
		// rotation is eased toward these for smooth look. Initialized from the transform.
		// Look angles (radians). The controller owns these and writes the derived quaternion into
		// TransformComponent; Pitch/Yaw ease toward TargetPitch/TargetYaw.
		float Pitch = 0.0f;
		float Yaw = 0.0f;
		float TargetPitch = 0.0f;
		float TargetYaw = 0.0f;

		// Smoothed world-space move velocity (units/sec) for accel/decel.
		glm::vec3 MoveVelocity{0.0f};

		// Scroll-dolly glide velocity (units/sec along forward). Each scroll notch adds an impulse; the
		// system integrates it and decays it to zero, so zoom coasts instead of teleporting per notch.
		float ZoomVelocity = 0.0f;
	};
}
