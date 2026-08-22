#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/WorldTransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"
#include "Snowstorm/Systems/TransformSystem.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/PhysicsComponents.hpp"
#include "Snowstorm/Physics/PhysicsLayer.hpp"
#include "SnowstormPhysics/JoltPhysics/JoltCharacterController.hpp"
#include "SnowstormPhysics/JoltPhysics/JoltScene.hpp"
#include "SnowstormPhysics/PhysicsJoltModule.hpp"
#include "SnowstormPhysics/PhysicsSystems.hpp"

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
			PhysicsLayerManager::ClearLayers();
			W.GetSingletonManager().RegisterSingleton<JoltScene>(&W);
			auto& sm = W.GetSystemManager();
			sm.RegisterSystemOrdered<PhysicsWriteBackSystem>(SystemPhase::Resolve, -10);
			sm.RegisterSystem<TransformSystem>(SystemPhase::Resolve);
			sm.RegisterSystemOrdered<PhysicsBodySyncSystem>(SystemPhase::Resolve, 10);
			sm.RegisterSystem<PhysicsStepSystem>(SystemPhase::FixedUpdate);
		}
		Entity Box(const char* name, const glm::vec3 pos, const glm::vec3 scale, const EBodyType bodyType, const uint32_t layer = 0)
		{
			Entity e = W.CreateEntity(name);
			auto& tr = e.AddComponent<TransformComponent>();
			tr.Translation = pos;
			tr.Scale = scale;
			e.AddComponent<BoxColliderComponent>().HalfSize = glm::vec3(0.5f); // explicit: the numbers below assume a unit box
			auto& rb = e.AddComponent<RigidBodyComponent>();
			rb.BodyType = bodyType;
			rb.LayerID = layer;
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
		pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, EBodyType::Static);
		Entity cube = pw.Box("Cube", {0, 4, 0}, {1, 1, 1}, EBodyType::Dynamic);
		pw.Step(1); // bodies get created at the end of the first frame (Resolve)
		REQUIRE(cube.HasComponent<PhysicsBodyRuntimeComponent>());
		REQUIRE(pw.W.GetSingleton<JoltScene>().GetBodyCount() == 2);
		pw.Step(240); // 4 s
		return cube.GetComponent<TransformComponent>().Translation;
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
	pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, EBodyType::Static);
	Entity body = pw.Box("Body", {0, 4, 0}, {1, 1, 1}, EBodyType::Dynamic);
	Entity child = pw.W.CreateEntity("Child");
	child.AddComponent<TransformComponent>().Translation = {2, 0, 0};
	child.AddComponent<BoxColliderComponent>().HalfSize = glm::vec3(0.5f);
	pw.W.SetParent(child, body, false);

	pw.Step(1);
	REQUIRE(body.HasComponent<PhysicsBodyRuntimeComponent>());
	REQUIRE_FALSE(child.HasComponent<PhysicsBodyRuntimeComponent>()); // not a body of its own
	REQUIRE(pw.W.GetSingleton<JoltScene>().GetBodyCount() == 2);

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
	pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, EBodyType::Static, 0);
	Entity ghost = pw.Box("Ghost", {0, 2, 0}, {1, 1, 1}, EBodyType::Dynamic, 1);
	PhysicsLayerManager::AddLayer("Ghosts");
	PhysicsLayerManager::SetLayerCollision(0, 1, false); // layer 1 passes through layer 0

	pw.Step(240);
	REQUIRE(ghost.GetComponent<TransformComponent>().Translation.y < -3.0f); // fell through the floor

	PhysicsLayerManager::SetLayerCollision(0, 1, true);
	Entity solid = pw.Box("Solid", {3, 2, 0}, {1, 1, 1}, EBodyType::Dynamic, 1);
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
	REQUIRE(solid.GetComponent<TransformComponent>().Translation.y == Catch::Approx(-1.375f).margin(0.05f));
}

TEST_CASE("Bodies authored in Edit mode all wake up when Play starts", "[physics]")
{
	PhysicsWorld pw;
	pw.W.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>(); // Edit mode
	pw.Box("Floor", {0, -2, 0}, {10, 0.25f, 10}, EBodyType::Static);
	Entity a = pw.Box("A", {0, 4, 0}, {1, 1, 1}, EBodyType::Dynamic);
	Entity b = pw.Box("B", {3, 4, 0}, {1, 1, 1}, EBodyType::Dynamic);
	pw.Step(30); // Edit mode: bodies exist (debug draw) but nothing moves
	REQUIRE(a.HasComponent<PhysicsBodyRuntimeComponent>());
	REQUIRE(a.GetComponent<TransformComponent>().Translation.y == Catch::Approx(4.0f));

	pw.W.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	pw.Step(240);
	// Both fell — not just the one that happened to be touched last.
	REQUIRE(a.GetComponent<TransformComponent>().Translation.y < 0.0f);
	REQUIRE(b.GetComponent<TransformComponent>().Translation.y < 0.0f);
}

TEST_CASE("A character controller lands on a floor and is moved by Move(), not by the solver", "[physics][character]")
{
	const int prevHz = CVars::SimFixedHz.Get();
	CVars::SimFixedHz.Set(60);

	PhysicsWorld world;
	world.W.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	world.W.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	world.Box("Floor", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f}, EBodyType::Static);

	// A capsule character dropped above the floor. No RigidBodyComponent: the character IS the movement.
	Entity player = world.W.CreateEntity("Player");
	auto& tr = player.AddComponent<TransformComponent>();
	tr.Translation = {0.0f, 3.0f, 0.0f};
	auto& capsule = player.AddComponent<CapsuleColliderComponent>();
	capsule.Radius = 0.3f;
	capsule.HalfHeight = 0.6f;
	capsule.Offset = {0.0f, 0.9f, 0.0f}; // capsule sits ON the entity origin, so the origin is at its feet
	player.AddComponent<CharacterControllerComponent>();

	world.Step(2); // sync systems build the character
	REQUIRE(player.HasComponent<CharacterControllerRuntimeComponent>());
	const Ref<JoltCharacterController> controller = player.GetComponent<CharacterControllerRuntimeComponent>().Controller;
	REQUIRE(controller);
	REQUIRE(controller->IsValid());

	// A character is NOT a rigid body: it must not also show up as one (an implicit static body built
	// from its colliders would be an immovable clone of the player standing in the player's way).
	REQUIRE_FALSE(player.HasComponent<PhysicsBodyRuntimeComponent>());

	SECTION("Gravity lands it on the floor and it stays there")
	{
		world.Step(120);
		REQUIRE(controller->IsGrounded());
		const float y = player.GetComponent<TransformComponent>().Translation.y;
		REQUIRE(y == Catch::Approx(0.0f).margin(0.06f)); // feet on the floor

		world.Step(120); // and it does not keep sinking or jitter away
		REQUIRE(controller->IsGrounded());
		REQUIRE(player.GetComponent<TransformComponent>().Translation.y == Catch::Approx(y).margin(0.02f));
	}

	SECTION("Move() displaces it along the floor")
	{
		world.Step(120); // land first
		const glm::vec3 start = player.GetComponent<TransformComponent>().Translation;

		// 3 m/s along +X for 60 fixed steps (one second) -> ~3 m, with nothing in the way.
		for (int i = 0; i < 60; ++i)
		{
			controller->Move({3.0f / 60.0f, 0.0f, 0.0f});
			world.Step(1);
		}
		const glm::vec3 end = player.GetComponent<TransformComponent>().Translation;
		REQUIRE(end.x - start.x == Catch::Approx(3.0f).margin(0.35f));
		REQUIRE(end.z == Catch::Approx(start.z).margin(0.01f));
		REQUIRE(controller->IsGrounded());
	}

	SECTION("A wall stops it instead of letting it pass through")
	{
		world.Step(120);
		world.Box("Wall", {1.5f, 1.0f, 0.0f}, {0.5f, 4.0f, 8.0f}, EBodyType::Static);
		world.Step(2);

		for (int i = 0; i < 90; ++i)
		{
			controller->Move({4.0f / 60.0f, 0.0f, 0.0f});
			world.Step(1);
		}
		// Walked into the wall (x = 1.25 is its near face) and stopped short of it, not through it.
		REQUIRE(player.GetComponent<TransformComponent>().Translation.x < 1.25f);
	}

	SECTION("Jump() leaves the ground and gravity brings it back")
	{
		world.Step(120);
		const float restY = player.GetComponent<TransformComponent>().Translation.y;

		controller->Jump(5.0f);
		world.Step(10);
		REQUIRE_FALSE(controller->IsGrounded());
		REQUIRE(player.GetComponent<TransformComponent>().Translation.y > restY + 0.2f);

		world.Step(180);
		REQUIRE(controller->IsGrounded());
		REQUIRE(player.GetComponent<TransformComponent>().Translation.y == Catch::Approx(restY).margin(0.06f));
	}

	CVars::SimFixedHz.Set(prevHz);
}

TEST_CASE("Scene queries hit the bodies they should and skip the excluded ones", "[physics][queries]")
{
	PhysicsWorld world;
	world.W.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	const Entity floor = world.Box("Floor", {0.0f, -0.5f, 0.0f}, {20.0f, 1.0f, 20.0f}, EBodyType::Static);
	const Entity wall = world.Box("Wall", {5.0f, 1.0f, 0.0f}, {1.0f, 4.0f, 8.0f}, EBodyType::Static);
	world.Step(2);

	const Snowstorm::UUID floorId = floor.GetComponent<IDComponent>().Id;
	const Snowstorm::UUID wallId = wall.GetComponent<IDComponent>().Id;
	auto& scene = world.W.GetSingleton<JoltScene>();

	SECTION("A ray finds the wall, and reports the distance to it")
	{
		RayCastInfo ray;
		ray.Origin = {0.0f, 1.0f, 0.0f};
		ray.Direction = {1.0f, 0.0f, 0.0f};
		ray.MaxDistance = 20.0f;

		SceneQueryHit hit;
		REQUIRE(scene.CastRay(ray, hit));
		REQUIRE(hit.HitEntity == wallId);
		REQUIRE(hit.Distance == Catch::Approx(4.5f).margin(0.05f)); // wall spans x = 4.5 .. 5.5
	}

	SECTION("Excluding the wall lets the ray miss everything")
	{
		RayCastInfo ray;
		ray.Origin = {0.0f, 1.0f, 0.0f};
		ray.Direction = {1.0f, 0.0f, 0.0f};
		ray.MaxDistance = 20.0f;
		ray.ExcludedEntities = {wallId};

		SceneQueryHit hit;
		REQUIRE_FALSE(scene.CastRay(ray, hit));
	}

	SECTION("A sphere cast stops earlier than a ray, because it has a radius")
	{
		SphereCastInfo sphere;
		sphere.Radius = 0.5f;
		sphere.Origin = {0.0f, 1.0f, 0.0f};
		sphere.Direction = {1.0f, 0.0f, 0.0f};
		sphere.MaxDistance = 20.0f;

		SceneQueryHit hit;
		REQUIRE(scene.CastShape(sphere, hit));
		REQUIRE(hit.HitEntity == wallId);
		REQUIRE(hit.Distance == Catch::Approx(4.0f).margin(0.05f)); // 4.5 minus the radius
	}

	SECTION("An overlap reports what a box is standing in, and nothing where it is empty")
	{
		BoxCastInfo box;
		box.HalfExtent = {0.4f, 0.4f, 0.4f};
		box.Origin = {0.0f, 0.2f, 0.0f}; // straddling the floor's top face

		std::vector<SceneQueryHit> hits;
		REQUIRE(scene.OverlapShape(box, hits) == 1);
		REQUIRE(hits[0].HitEntity == floorId);

		box.Origin = {0.0f, 6.0f, 0.0f}; // mid-air
		REQUIRE(scene.OverlapShape(box, hits) == 0);
		REQUIRE(hits.empty());
	}
}
