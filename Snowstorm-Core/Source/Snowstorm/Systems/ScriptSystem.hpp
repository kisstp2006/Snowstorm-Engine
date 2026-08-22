#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace Snowstorm
{
	// Owns the native script lifecycle (Unity MonoBehaviour order): on the Edit->Play transition every
	// ScriptComponent gets a ScriptRuntimeComponent with an instance from ScriptRegistry and OnCreate;
	// OnStart precedes the first OnUpdate; physics events from ScriptEventQueue are delivered before the
	// tick; Play->Edit (or a Stop) runs OnDestroy and drops the instances. Runs in Edit mode too so it can
	// SEE the transitions — the ticking itself is gated on SimulationStateSingleton (a packaged runtime
	// has no simulation state and is always playing).
	class ScriptSystem final : public System
	{
	public:
		explicit ScriptSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return true; }

	private:
		[[nodiscard]] bool IsPlaying() const;
		void InstantiateMissing();
		void DeliverEvents();
		void DestroyAll();

		bool m_WasPlaying = false;
		std::vector<ScriptEvent> m_EventScratch;
		std::unordered_set<std::string> m_WarnedClasses; // unknown ClassName: warn once per name
	};

	// Fixed-step half of the script tick: OnFixedUpdate for every live instance. Registered in the
	// FixedUpdate phase (the SystemManager drives that phase through its accumulator).
	class ScriptFixedSystem final : public System
	{
	public:
		explicit ScriptFixedSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		[[nodiscard]] bool RunsInEditMode() const override { return false; }
	};
}
