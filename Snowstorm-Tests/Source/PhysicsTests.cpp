#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/WorldTransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"
#include "Snowstorm/Systems/TransformSystem.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

#include "SnowstormPhysics/Components/PhysicsBodyRuntimeComponent.hpp"
#include "SnowstormPhysics/Components/PhysicsComponents.hpp"
#include "SnowstormPhysics/PhysicsJoltModule.hpp"
#include "SnowstormPhysics/PhysicsSystems.hpp"
#include "SnowstormPhysics/PhysicsWorldSingleton.hpp"

using namespace Snowstorm;

namespace
{
	// A headless physics World: no Application, so the step runs Jolt single-threaded. Systems are
	// registered in the same phases/orders the module uses.
	struct PhysicsWorld
	{
		World W{WorldType::Game};
		PhysicsWorld()
		{
			PhysicsJoltModule::EnsureJoltInitialized();
			CVars::EcsParallel.Set(false);
			W.GetSingletonManager().RegisterSingleton<ScriptEventQueue>();
			W.GetSingletonManager().RegisterSingleton<PhysicsWorldSingleton>(&W);
			auto& sm = W.GetSystemManager();
			sm.RegisterSystemOrdered<PhysicsWriteBackSystem>(SystemPhase::Resolve, -10);
			sm.RegisterSystem<TransformSystem>(SystemPhase::Resolve);
			sm.RegisterSystemOrdered<PhysicsBodySyncSystem>(SystemPhase::Resolve, 10);
			sm.RegisterSystem<PhysicsStepSystem>(SystemPhase::FixedUpdate);
		}
		Entity Box(const char* name, const glm::vec3 pos, const glm::vec3 scale, const MotionType motion, const uint32_t layer = 0)
		{
			Entity e = W.CreateEntity(name);
			auto& tr = e.AddComponent<TransformComponent>();
			tr.Position = pos;
			tr.Scale = scale;
			e.AddComponent<BoxColliderComponent>();
			auto& rb = e.AddComponent<RigidBodyComponent>();
			rb.Motion = motion;
			rb.CollisionLayer = layer;
			return e;
		}
		void Step(const int frames)
		{
			for (int i = 0; i < frames; ++i)
			{
				W.OnUpdate(Timestep{1.0f / 60.0f});
			}
		}
	};
}

TEST_CASE("A dynamic box falls onto a static floor and comes to rest, deterministically", "[physics]")
{
	const int prevHz = CVars::SimFixedHz.Get();
	CVars::SimFixedHz.Set(60);

	auto run = []
	{
		PhysicsWorld pw;
		pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, MotionType::Static);
		Entity cube = pw.Box("Cube", {0, 4, 0}, {1, 1, 1}, MotionType::Dynamic);
		pw.Step(1); // bodies get created at the end of the first frame (Resolve)
		REQUIRE(cube.HasComponent<PhysicsBodyRuntimeComponent>());
		REQUIRE(pw.W.GetSingleton<PhysicsWorldSingleton>().BodyCount() == 2);
		pw.Step(240); // 4 s
		return cube.GetComponent<TransformComponent>().Position;
	};

	const glm::vec3 a = run();
	// Floor top = -2 + 0.125 (half extent 0.5 * scale 0.25); cube half extent 0.5 -> rests at -1.375,
	// minus Jolt's penetration slop (2 cm: bodies settle slightly interpenetrated by design).
	REQUIRE(a.y == Catch::Approx(-1.375f).margin(0.03f));
	REQUIRE(std::abs(a.x) < 1e-3f);
	REQUIRE(std::abs(a.z) < 1e-3f);

	const glm::vec3 b = run();
	REQUIRE(a == b); // bit-identical: Jolt is deterministic for the same inputs

	CVars::SimFixedHz.Set(prevHz);
}

TEST_CASE("A child collider folds into the parent body's compound shape and follows it", "[physics][hierarchy]")
{
	PhysicsWorld pw;
	pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, MotionType::Static);
	Entity body = pw.Box("Body", {0, 4, 0}, {1, 1, 1}, MotionType::Dynamic);
	Entity child = pw.W.CreateEntity("Child");
	child.AddComponent<TransformComponent>().Position = {2, 0, 0};
	child.AddComponent<BoxColliderComponent>();
	pw.W.SetParent(child, body, false);

	pw.Step(1);
	REQUIRE(body.HasComponent<PhysicsBodyRuntimeComponent>());
	REQUIRE_FALSE(child.HasComponent<PhysicsBodyRuntimeComponent>()); // not a body of its own
	REQUIRE(pw.W.GetSingleton<PhysicsWorldSingleton>().BodyCount() == 2);

	pw.Step(240);
	// The child rides along: it stays 2 units from the body (the hierarchy propagates the simulated pose).
	const glm::vec3 bodyPos = glm::vec3(pw.W.ComputeWorldMatrix(body)[3]);
	const glm::vec3 childPos = glm::vec3(pw.W.ComputeWorldMatrix(child)[3]);
	REQUIRE(glm::distance(bodyPos, childPos) == Catch::Approx(2.0f).margin(1e-3f));
	REQUIRE(bodyPos.y < 0.0f); // it fell
}

TEST_CASE("The collision matrix gates contacts and the script event queue receives CollisionEnter", "[physics]")
{
	PhysicsWorld pw;
	auto& physics = pw.W.GetSingleton<PhysicsWorldSingleton>();
	pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, MotionType::Static, 0);
	Entity ghost = pw.Box("Ghost", {0, 2, 0}, {1, 1, 1}, MotionType::Dynamic, 1);
	physics.SetLayersCollide(0, 1, false); // layer 1 passes through layer 0

	pw.Step(240);
	REQUIRE(ghost.GetComponent<TransformComponent>().Position.y < -3.0f); // fell through the floor

	physics.SetLayersCollide(0, 1, true);
	Entity solid = pw.Box("Solid", {3, 2, 0}, {1, 1, 1}, MotionType::Dynamic, 1);
	pw.Step(240);
	std::vector<ScriptEvent> events;
	pw.W.GetSingleton<ScriptEventQueue>().Drain(events);
	bool solidHitFloor = false;
	for (const ScriptEvent& ev : events)
	{
		if (ev.Type == ScriptEvent::Kind::CollisionEnter && ev.A == solid.Handle())
		{
			solidHitFloor = true;
		}
	}
	REQUIRE(solidHitFloor);
	REQUIRE(solid.GetComponent<TransformComponent>().Position.y == Catch::Approx(-1.375f).margin(0.05f));
}
