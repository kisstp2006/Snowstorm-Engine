#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"
#include "Snowstorm/World/Entity.hpp"

namespace Snowstorm
{
	// Base class of a native script (Unity MonoBehaviour, Unreal Actor/Component tick hooks). A
	// ScriptComponent names a registered subclass; ScriptSystem instantiates it when the simulation enters
	// Play, drives the hooks below, and destroys it on Stop or when the entity dies. Scripts run on the
	// main thread only; physics callbacks are delivered from the ScriptEventQueue, never from a worker.
	class ScriptableEntity : public NonCopyable
	{
	public:
		~ScriptableEntity() override = default;

		[[nodiscard]] Entity GetEntity() const { return m_Entity; }
		[[nodiscard]] World& GetWorld() const { return *m_Entity.GetWorld(); }

		template <typename T>
		[[nodiscard]] const T& GetComponent() const
		{
			return m_Entity.GetComponent<T>();
		}

		template <typename T>
		T& WriteComponent()
		{
			return m_Entity.WriteComponent<T>();
		}

		template <typename T>
		[[nodiscard]] bool HasComponent() const
		{
			return m_Entity.HasComponent<T>();
		}

	protected:
		// Lifecycle (Unity order): OnCreate right after instantiation, OnStart before the first OnUpdate,
		// OnUpdate every Logic tick, OnFixedUpdate every fixed simulation step, OnDestroy exactly once.
		virtual void OnCreate() {}
		virtual void OnStart() {}
		virtual void OnUpdate(Timestep /*ts*/) {}
		virtual void OnFixedUpdate(float /*fixedDt*/) {}
		virtual void OnDestroy() {}

		// Physics events (delivered on the main thread, Logic phase).
		virtual void OnCollisionEnter(Entity /*other*/, const ContactInfo& /*contact*/) {}
		virtual void OnCollisionExit(Entity /*other*/) {}
		virtual void OnTriggerEnter(Entity /*other*/) {}
		virtual void OnTriggerExit(Entity /*other*/) {}

	private:
		Entity m_Entity;

		friend class ScriptSystem;
		friend class ScriptFixedSystem;
		friend class World;
	};
}
