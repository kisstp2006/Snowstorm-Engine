#pragma once

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Snowstorm
{
	// Pure per-entity rotation advance: rotate `tr` about `rot.Axis` by `rot.SpeedDegPerSec * dt`. Data
	// in -> data out, no ECS/registry/threading — so it's the single source of truth shared by
	// RotatorSystem (the real loop) and the parallel-ECS benchmark (which times it in isolation), and it
	// is directly unit-testable. Keeping it a free function is the "pure testable core" the engine's
	// debuggability guidance calls for. Returns without touching `tr` when there's no axis/speed.
	inline void AdvanceRotation(TransformComponent& tr, const RotatorComponent& rot, const float dt)
	{
		const float lenSq = glm::dot(rot.Axis, rot.Axis);
		if (lenSq < 1e-12f || rot.SpeedDegPerSec == 0.0f)
		{
			return; // no axis or no speed -> nothing to do
		}

		const glm::vec3 axis = rot.Axis / glm::sqrt(lenSq);
		const float deltaAngle = glm::radians(rot.SpeedDegPerSec) * dt;

		// Incremental rotation about the LOCAL axis: post-multiply. Normalize to keep the quaternion unit
		// length over thousands of frames.
		tr.Rotation = glm::normalize(tr.Rotation * glm::angleAxis(deltaAngle, axis));
	}
}
