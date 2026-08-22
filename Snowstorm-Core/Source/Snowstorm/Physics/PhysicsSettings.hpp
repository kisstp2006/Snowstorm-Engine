#pragma once

#include "Snowstorm/Physics/ColliderMaterial.hpp"
#include "Snowstorm/Physics/PhysicsTypes.hpp"

#include <glm/vec3.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Engine-wide simulation settings (Hazel PhysicsSettings). The fixed timestep itself is the engine's
	// FixedUpdate phase (sim.fixed_hz) — scripts' OnFixedUpdate and the physics step share it.
	struct PhysicsSettings
	{
		glm::vec3 Gravity{0.0f, -9.81f, 0.0f};
		uint32_t PositionSolverIterations = 2;
		uint32_t VelocitySolverIterations = 10;
		uint32_t MaxBodies = 5700;
		// Snowstorm extension: render dynamic bodies interpolated between the last two fixed steps.
		bool InterpolateBodies = true;
	};
}
