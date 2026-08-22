#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <glm/gtc/constants.hpp>

#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/Math/Transform.hpp"

#include <cstdint>
#include <vector>

using namespace Snowstorm;

namespace
{
	uint64_t Splitmix(uint64_t& state)
	{
		state += 0x9E3779B97F4A7C15ull;
		uint64_t z = state;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	float RandFloat(uint64_t& state, const float lo, const float hi)
	{
		const float unit = static_cast<float>(Splitmix(state) >> 40) / static_cast<float>(1u << 24);
		return lo + unit * (hi - lo);
	}
}

// The data-parallel RotatorSystem is only correct if running AdvanceRotation across worker threads
// produces the EXACT same result as the serial loop. AdvanceRotation writes only its own entity's
// transform (no shared state), so the outputs must be bit-identical regardless of thread scheduling —
// this is the headless proof of the "ecs.parallel is a pure perf toggle, not a behavior change" claim.
TEST_CASE("AdvanceRotation is identical serial vs parallel (data-parallel correctness)", "[ecs][jobsystem]")
{
	constexpr size_t count = 20000;
	constexpr float dt = 1.0f / 60.0f;

	std::vector<TransformComponent> serialTr(count);
	std::vector<RotatorComponent> rot(count);

	uint64_t rng = 20260708u;
	for (size_t i = 0; i < count; ++i)
	{
		serialTr[i].Rotation = QuatFromEulerRadians({RandFloat(rng, 0.0f, 6.28f), RandFloat(rng, 0.0f, 6.28f), RandFloat(rng, 0.0f, 6.28f)});
		rot[i].Axis = {RandFloat(rng, -1.0f, 1.0f), 1.0f, RandFloat(rng, -1.0f, 1.0f)};
		rot[i].SpeedDegPerSec = RandFloat(rng, 15.0f, 90.0f);
	}

	// Identical starting transforms for the parallel copy.
	std::vector<TransformComponent> parallelTr = serialTr;

	// Advance several frames so any per-frame drift would accumulate and diverge if there were a race.
	for (int frame = 0; frame < 10; ++frame)
	{
		for (size_t i = 0; i < count; ++i)
		{
			AdvanceRotation(serialTr[i], rot[i], dt);
		}

		JobSystem jobs;
		jobs.ParallelFor(
		    count,
		    [&](const size_t begin, const size_t end)
		    {
			    for (size_t i = begin; i < end; ++i)
			    {
				    AdvanceRotation(parallelTr[i], rot[i], dt);
			    }
		    },
		    256);
	}

	for (size_t i = 0; i < count; ++i)
	{
		// Bit-exact: same inputs, same math, distinct memory -> thread scheduling cannot change the result.
		REQUIRE(serialTr[i].Rotation.x == parallelTr[i].Rotation.x);
		REQUIRE(serialTr[i].Rotation.y == parallelTr[i].Rotation.y);
		REQUIRE(serialTr[i].Rotation.z == parallelTr[i].Rotation.z);
		REQUIRE(serialTr[i].Rotation.w == parallelTr[i].Rotation.w);
	}
}

// A zero-axis or zero-speed rotator is a no-op: AdvanceRotation must leave the transform untouched
// (the early-out the system relies on to skip idle rotators cheaply).
TEST_CASE("AdvanceRotation leaves transform unchanged for degenerate rotators", "[ecs]")
{
	TransformComponent tr;
	tr.Rotation = QuatFromEulerRadians({0.3f, 0.6f, 0.9f});
	const glm::quat before = tr.Rotation;

	RotatorComponent zeroAxis;
	zeroAxis.Axis = {0.0f, 0.0f, 0.0f};
	zeroAxis.SpeedDegPerSec = 45.0f;
	AdvanceRotation(tr, zeroAxis, 1.0f / 60.0f);
	REQUIRE(tr.Rotation == before);

	RotatorComponent zeroSpeed;
	zeroSpeed.Axis = {0.0f, 1.0f, 0.0f};
	zeroSpeed.SpeedDegPerSec = 0.0f;
	AdvanceRotation(tr, zeroSpeed, 1.0f / 60.0f);
	REQUIRE(tr.Rotation == before);
}

// Euler <-> quaternion helpers are exact inverses for the YXZ convention the inspector and the camera
// controllers use, and a 90-degree yaw turns -Z forward into -X.
TEST_CASE("Euler/quaternion helpers round-trip (YXZ)", "[math]")
{
	const glm::vec3 euler{0.4f, -1.1f, 0.25f}; // pitch, yaw, roll (radians), away from gimbal lock
	const glm::vec3 back = EulerRadiansFromQuat(QuatFromEulerRadians(euler));
	REQUIRE(back.x == Catch::Approx(euler.x).margin(1e-5f));
	REQUIRE(back.y == Catch::Approx(euler.y).margin(1e-5f));
	REQUIRE(back.z == Catch::Approx(euler.z).margin(1e-5f));

	const glm::vec3 fwd = ForwardFromQuat(QuatFromPitchYaw(0.0f, glm::half_pi<float>()));
	REQUIRE(fwd.x == Catch::Approx(-1.0f).margin(1e-5f));
	REQUIRE(fwd.z == Catch::Approx(0.0f).margin(1e-5f));

	const glm::quat look = QuatLookingAlong({-1.0f, 0.0f, 0.0f});
	const glm::vec3 lookFwd = ForwardFromQuat(look);
	REQUIRE(lookFwd.x == Catch::Approx(-1.0f).margin(1e-5f));
}
