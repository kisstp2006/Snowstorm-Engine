#include "ScriptSystem.hpp"

#include "Snowstorm/Components/ScriptComponent.hpp"
#include "Snowstorm/Components/ScriptRuntimeComponent.hpp"
#include "Snowstorm/Scripting/ScriptRegistry.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

namespace Snowstorm
{
	bool ScriptSystem::IsPlaying() const
	{
		if (m_World->HasSingleton<SimulationStateSingleton>())
		{
			return m_World->GetSingleton<SimulationStateSingleton>().IsPlaying();
		}
		return true; // packaged runtime: no edit mode
	}

	void ScriptSystem::InstantiateMissing()
	{
		auto& reg = m_World->GetRegistry();
		std::vector<entt::entity> fresh;
		for (const auto view = reg.view<ScriptComponent>(); const entt::entity e : view)
		{
			if (reg.any_of<ScriptRuntimeComponent>(e))
			{
				continue;
			}
			const std::string& className = reg.Read<ScriptComponent>(e).ClassName;
			if (className.empty())
			{
				continue;
			}
			Scope<ScriptableEntity> instance = ScriptRegistry::Instantiate(className);
			if (!instance)
			{
				if (m_WarnedClasses.insert(className).second)
				{
					SS_CORE_WARN("ScriptSystem: no script class '{}' is registered (ScriptComponent ignored).", className);
				}
				continue;
			}
			instance->m_Entity = Entity{e, m_World};
			auto& rt = reg.emplace<ScriptRuntimeComponent>(e);
			rt.Instance = std::move(instance);
			fresh.push_back(e);
		}
		// OnCreate after every instance of this batch exists, so scripts may look each other up.
		for (const entt::entity e : fresh)
		{
			reg.get<ScriptRuntimeComponent>(e).Instance->OnCreate();
		}
	}

	void ScriptSystem::DestroyAll()
	{
		auto& reg = m_World->GetRegistry();
		std::vector<entt::entity> doomed;
		for (const auto view = reg.view<ScriptRuntimeComponent>(); const entt::entity e : view)
		{
			doomed.push_back(e);
		}
		for (const entt::entity e : doomed)
		{
			if (auto& rt = reg.get<ScriptRuntimeComponent>(e); rt.Instance)
			{
				rt.Instance->OnDestroy();
			}
			reg.remove<ScriptRuntimeComponent>(e);
		}
	}

	void ScriptSystem::DeliverEvents()
	{
		if (!m_World->HasSingleton<ScriptEventQueue>())
		{
			return;
		}
		m_EventScratch.clear();
		m_World->GetSingleton<ScriptEventQueue>().Drain(m_EventScratch);
		auto& reg = m_World->GetRegistry();
		for (const ScriptEvent& ev : m_EventScratch)
		{
			if (!reg.valid(ev.A) || !reg.any_of<ScriptRuntimeComponent>(ev.A))
			{
				continue;
			}
			ScriptableEntity* script = reg.get<ScriptRuntimeComponent>(ev.A).Instance.get();
			if (!script)
			{
				continue;
			}
			const Entity other{reg.valid(ev.B) ? ev.B : entt::null, m_World};
			switch (ev.Type)
			{
			case ScriptEvent::Kind::CollisionEnter:
				script->OnCollisionEnter(other, ev.Contact);
				break;
			case ScriptEvent::Kind::CollisionExit:
				script->OnCollisionExit(other);
				break;
			case ScriptEvent::Kind::TriggerEnter:
				script->OnTriggerEnter(other);
				break;
			case ScriptEvent::Kind::TriggerExit:
				script->OnTriggerExit(other);
				break;
			}
		}
	}

	void ScriptSystem::Execute(const Timestep ts)
	{
		const bool playing = IsPlaying();
		if (!playing)
		{
			if (m_WasPlaying)
			{
				DestroyAll(); // Play -> Edit: tear down (entities that survive the stop lose their scripts)
			}
			m_WasPlaying = false;
			return;
		}
		m_WasPlaying = true;

		// New ScriptComponents (Play entered, or added during Play) get their instance + OnCreate.
		InstantiateMissing();

		// A ScriptComponent removed during Play takes its instance with it.
		auto& reg = m_World->GetRegistry();
		for (const entt::entity e : FiniView<ScriptComponent>())
		{
			if (reg.valid(e) && reg.any_of<ScriptRuntimeComponent>(e))
			{
				if (auto& rt = reg.get<ScriptRuntimeComponent>(e); rt.Instance)
				{
					rt.Instance->OnDestroy();
				}
				reg.remove<ScriptRuntimeComponent>(e);
			}
		}

		DeliverEvents();

		for (const auto view = reg.view<ScriptRuntimeComponent>(); const entt::entity e : view)
		{
			auto& rt = reg.get<ScriptRuntimeComponent>(e);
			if (!rt.Instance)
			{
				continue;
			}
			if (!rt.Started)
			{
				rt.Started = true;
				rt.Instance->OnStart();
			}
			rt.Instance->OnUpdate(ts);
		}
	}

	void ScriptFixedSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		for (const auto view = reg.view<ScriptRuntimeComponent>(); const entt::entity e : view)
		{
			if (auto& rt = reg.get<ScriptRuntimeComponent>(e); rt.Instance && rt.Started)
			{
				rt.Instance->OnFixedUpdate(ts.GetSeconds());
			}
		}
	}
}
