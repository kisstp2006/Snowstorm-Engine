#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Components/ScriptComponent.hpp"
#include "Snowstorm/Components/ScriptRuntimeComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"
#include "Snowstorm/Scripting/ScriptRegistry.hpp"
#include "Snowstorm/Scripting/ScriptableEntity.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <string>
#include <vector>

using namespace Snowstorm;

namespace
{
	// Records the lifecycle into a shared log so the order can be asserted after the instance is gone.
	std::vector<std::string> g_Log;

	class ProbeScript final : public ScriptableEntity
	{
	protected:
		void OnCreate() override { g_Log.emplace_back("create"); }
		void OnStart() override { g_Log.emplace_back("start"); }
		void OnUpdate(Timestep) override { g_Log.emplace_back("update"); }
		void OnFixedUpdate(float) override { g_Log.emplace_back("fixed"); }
		void OnDestroy() override { g_Log.emplace_back("destroy"); }
		void OnCollisionEnter(Entity other, const ContactInfo& c) override
		{
			g_Log.emplace_back("collision:" + std::to_string(static_cast<uint32_t>(other.Handle())) + ":" + std::to_string(static_cast<int>(c.Impulse)));
		}
	};

	struct Registered
	{
		Registered()
		{
			if (!ScriptRegistry::Has("ProbeScript"))
			{
				SS_REGISTER_SCRIPT(ProbeScript);
			}
		}
	};

}

TEST_CASE("ScriptRegistry instantiates registered classes by name and rejects unknown ones", "[script]")
{
	Registered reg;
	REQUIRE(ScriptRegistry::Has("ProbeScript"));
	REQUIRE(ScriptRegistry::Instantiate("ProbeScript"));
	REQUIRE_FALSE(ScriptRegistry::Instantiate("NoSuchScript"));
	REQUIRE_FALSE(ScriptRegistry::Names().empty());
}

TEST_CASE("Scripts live only while the simulation plays: create/start/update, then destroy on stop", "[script]")
{
	Registered reg;
	g_Log.clear();
	World world(WorldType::Utility);
	world.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	world.GetSingletonManager().RegisterSingleton<ScriptEventQueue>();
	ScriptSystem system(&world);

	Entity e = world.CreateEntity("Scripted");
	e.AddComponent<TransformComponent>();
	e.AddComponent<ScriptComponent>().ClassName = "ProbeScript";

	// Edit mode: nothing happens.
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.empty());
	REQUIRE_FALSE(e.HasComponent<ScriptRuntimeComponent>());

	// Play: instance + OnCreate, OnStart before the first OnUpdate.
	world.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	system.Execute(Timestep{0.016f});
	REQUIRE(e.HasComponent<ScriptRuntimeComponent>());
	REQUIRE(g_Log == std::vector<std::string>{"create", "start", "update"});
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.back() == "update");
	REQUIRE(g_Log.size() == 4);

	// Stop: OnDestroy exactly once, runtime component gone, authored component intact.
	world.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Edit;
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.back() == "destroy");
	REQUIRE_FALSE(e.HasComponent<ScriptRuntimeComponent>());
	REQUIRE(e.GetComponent<ScriptComponent>().ClassName == "ProbeScript");
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.size() == 5); // no second destroy
}

TEST_CASE("An entity destroyed mid-play and a scene clear both deliver OnDestroy", "[script]")
{
	Registered reg;
	g_Log.clear();
	World world(WorldType::Utility);
	world.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	world.GetSingletonManager().RegisterSingleton<ScriptEventQueue>();
	world.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	ScriptSystem system(&world);

	Entity a = world.CreateEntity("A");
	a.AddComponent<ScriptComponent>().ClassName = "ProbeScript";
	Entity b = world.CreateEntity("B");
	b.AddComponent<ScriptComponent>().ClassName = "ProbeScript";
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.size() == 6); // 2x (create, start, update)

	g_Log.clear();
	world.DestroyEntity(a);
	world.FlushDestroyQueue();
	REQUIRE(g_Log == std::vector<std::string>{"destroy"});
	REQUIRE_FALSE(a.IsValid());

	g_Log.clear();
	world.ClearSceneEntities();
	REQUIRE(g_Log == std::vector<std::string>{"destroy"});
}

TEST_CASE("Physics events reach the script on the next tick", "[script]")
{
	Registered reg;
	g_Log.clear();
	World world(WorldType::Utility);
	world.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	world.GetSingletonManager().RegisterSingleton<ScriptEventQueue>();
	world.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	ScriptSystem system(&world);

	Entity a = world.CreateEntity("A");
	a.AddComponent<ScriptComponent>().ClassName = "ProbeScript";
	Entity other = world.CreateEntity("Other");
	system.Execute(Timestep{0.016f});

	ScriptEvent ev;
	ev.Type = ScriptEvent::Kind::CollisionEnter;
	ev.A = a.Handle();
	ev.B = other.Handle();
	ev.Contact.Impulse = 7.0f;
	world.GetSingleton<ScriptEventQueue>().Push(ev);
	g_Log.clear();
	system.Execute(Timestep{0.016f});
	REQUIRE(g_Log.size() == 2);
	REQUIRE(g_Log[0] == "collision:" + std::to_string(static_cast<uint32_t>(other.Handle())) + ":7");
	REQUIRE(g_Log[1] == "update");
}

TEST_CASE("The FixedUpdate phase steps at the fixed rate and clamps stalls", "[script][ecs]")
{
	Registered reg;
	g_Log.clear();
	World world(WorldType::Utility);
	world.GetSingletonManager().RegisterSingleton<SimulationStateSingleton>();
	world.GetSingletonManager().RegisterSingleton<ScriptEventQueue>();
	world.GetSingleton<SimulationStateSingleton>().Current = SimulationStateSingleton::Mode::Play;
	auto& sm = world.GetSystemManager();
	sm.RegisterSystem<ScriptSystem>(SystemPhase::Logic);
	sm.RegisterSystem<ScriptFixedSystem>(SystemPhase::FixedUpdate);
	Entity a = world.CreateEntity("A");
	a.AddComponent<ScriptComponent>().ClassName = "ProbeScript";

	const int prevHz = CVars::SimFixedHz.Get();
	CVars::SimFixedHz.Set(60);

	auto fixedCount = [] { size_t n = 0; for (const auto& s : g_Log) n += (s == "fixed"); return n; };

	world.OnUpdate(Timestep{1.0f / 60.0f}); // first frame: Logic creates the instance; FixedUpdate runs once
	REQUIRE(fixedCount() == 1);
	world.OnUpdate(Timestep{1.0f / 120.0f}); // half a step banked -> no fixed tick
	REQUIRE(fixedCount() == 1);
	world.OnUpdate(Timestep{1.0f / 120.0f}); // completes the step
	REQUIRE(fixedCount() == 2);
	world.OnUpdate(Timestep{1.0f}); // a 1 s stall is clamped to the max steps per frame
	REQUIRE(fixedCount() == 2 + 4);
	REQUIRE(sm.FixedAlpha() >= 0.0f);
	REQUIRE(sm.FixedAlpha() < 1.0f);

	CVars::SimFixedHz.Set(prevHz);
}
