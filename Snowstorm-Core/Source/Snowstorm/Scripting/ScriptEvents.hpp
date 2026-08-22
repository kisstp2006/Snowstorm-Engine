#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <mutex>
#include <vector>

namespace Snowstorm
{
	struct ContactInfo
	{
		glm::vec3 Point{0.0f};
		glm::vec3 Normal{0.0f, 1.0f, 0.0f};
		float Impulse = 0.0f;
	};

	struct ScriptEvent
	{
		enum class Kind : uint8_t
		{
			CollisionEnter,
			CollisionExit,
			TriggerEnter,
			TriggerExit,
		};
		Kind Type = Kind::CollisionEnter;
		entt::entity A = entt::null; // the script-bearing entity receives the callback with B as `other`
		entt::entity B = entt::null;
		ContactInfo Contact;
	};

	// World-scoped queue between the simulation producers (the physics module's contact listener runs on
	// worker threads) and ScriptSystem, which drains it on the main thread in the Logic phase and calls
	// the script hooks. Producers push from any thread; only ScriptSystem drains.
	class ScriptEventQueue final : public Singleton
	{
	public:
		void Push(const ScriptEvent& ev)
		{
			std::lock_guard lock(m_Mutex);
			m_Events.push_back(ev);
		}

		void Drain(std::vector<ScriptEvent>& out)
		{
			std::lock_guard lock(m_Mutex);
			out.insert(out.end(), m_Events.begin(), m_Events.end());
			m_Events.clear();
		}

	private:
		std::mutex m_Mutex;
		std::vector<ScriptEvent> m_Events;
	};
}
