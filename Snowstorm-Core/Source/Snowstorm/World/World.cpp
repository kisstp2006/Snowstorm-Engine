#include "World.hpp"

#include "Snowstorm/Components/HierarchyComponent.hpp"
#include "Snowstorm/Components/ScriptRuntimeComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Math/Transform.hpp"

#include "Entity.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Events/Event.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/ReflectionGeometrySingleton.hpp"
#include "Snowstorm/Systems/SceneAccelerationSingleton.hpp"
#include "Snowstorm/Systems/TlasInstanceMapSingleton.hpp"
#include "Snowstorm/World/EditorHooksSingleton.hpp"

namespace Snowstorm
{
	World::World(const WorldType type)
	    : m_Type(type)
	{
		m_SystemManager = CreateScope<SystemManager>(this);
		m_SingletonManager = CreateScope<SingletonManager>();

		// World-scoped, per-scene state only. The renderer + shader/mesh libraries are device-lifetime and
		// now live in the application's ServiceManager (see CoreModule), shared across all Worlds.
		m_SingletonManager->RegisterSingleton<InputStateSingleton>();

		// Editor-integration seam (#162): Core-run systems invoke these callbacks; the editor installs them.
		// Always registered (callbacks default to null = no-op), so Core needs no #ifdef and the runtime is
		// unaffected. This replaces Core's former dependency on the editor's command/selection/history types,
		// which now live in Snowstorm-Editor. See EditorHooksSingleton.
		m_SingletonManager->RegisterSingleton<EditorHooksSingleton>();

		m_SingletonManager->RegisterSingleton<AssetManagerSingleton>(this);

		// TLAS instance index -> entity table for RT editor picking (#118 follow-up). Written by
		// TlasBuildSystem, read by the editor's pick path. Core-scoped so both the editor and a headless
		// runtime carry it (harmless when unused: it just stays empty).
		m_SingletonManager->RegisterSingleton<TlasInstanceMapSingleton>();

		// Per-instance geometry/material table for RT reflections (#118 follow-up). Written by TlasBuildSystem
		// alongside the TLAS, read by RendererService to feed the reflection trace. Empty unless RT reflections
		// are active.
		m_SingletonManager->RegisterSingleton<ReflectionGeometrySingleton>();
		m_SingletonManager->RegisterSingleton<SceneAccelerationSingleton>();

		// Create the bridge to the Application event bus. In a headless context (unit tests) there is no
		// Application, so skip the bridge — input simply never fires, which is fine for logic-only tests.
		if (Application::Exists())
		{
			auto& bus = Application::Get().GetEventBus();
			auto& input = m_SingletonManager->GetSingleton<InputStateSingleton>();
			m_InputEventBridge = CreateScope<InputEventBridge>(bus, input);
		}

		// Every module the application is assembled from contributes its singletons/systems to every World
		// (CoreModule: the engine systems; EditorModule: UI, on Editor worlds only). Tests construct Worlds
		// with no Application, and get no systems — as before.
		if (Application::Exists())
		{
			Application::Get().GetModules().RegisterWorld(*this);
		}
	}

	World::~World() = default;

	Entity World::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID{}, name);
	}

	Entity World::CreateEntityWithUUID(const UUID uuid, const std::string& name)
	{
		Entity entity = {m_SystemManager->GetRegistry().create(), this};

		auto& id = entity.AddComponent<IDComponent>();
		id.Id = uuid;

		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		return entity;
	}

	Entity World::FindEntityByUUID(const UUID uuid) const
	{
		auto& reg = m_SystemManager->GetRegistry();
		for (const auto view = reg.view<IDComponent>(); const entt::entity e : view)
		{
			if (reg.Read<IDComponent>(e).Id == uuid)
			{
				return Entity{e, const_cast<World*>(this)};
			}
		}
		return Entity{entt::null, const_cast<World*>(this)};
	}

	void World::DestroyEntity(const Entity entity)
	{
		if (!entity)
		{
			return;
		}
		// Destroying a parent destroys its subtree (Unity/Godot semantics); queue children first so the
		// flush unlinks leaves before their parents go away.
		ForEachChild(entity, [this](const Entity child)
		             { DestroyEntity(child); });
		m_PendingDestroy.push_back(entity.Handle());
	}

	void World::FlushDestroyQueue()
	{
		if (m_PendingDestroy.empty())
		{
			return;
		}

		auto& reg = m_SystemManager->GetRegistry();
		for (const entt::entity e : m_PendingDestroy)
		{
			if (reg.valid(e))
			{
				NotifyScriptDestroyed(e);
				UnlinkFromParent(e); // a surviving parent must not keep a dangling child link
				reg.destroy(e);
			}
		}
		m_PendingDestroy.clear();
	}

	void World::NotifyScriptDestroyed(const entt::entity e) const
	{
		// An entity never dies without its script's OnDestroy (Unity: OnDestroy on scene unload too).
		auto& reg = m_SystemManager->GetRegistry();
		if (auto* rt = reg.any_of<ScriptRuntimeComponent>(e) ? &reg.get<ScriptRuntimeComponent>(e) : nullptr; rt && rt->Instance)
		{
			rt->Instance->OnDestroy();
			rt->Instance.reset();
		}
	}

	void World::NotifyAllScriptsDestroyed() const
	{
		auto& reg = m_SystemManager->GetRegistry();
		for (const auto view = reg.view<ScriptRuntimeComponent>(); const entt::entity e : view)
		{
			NotifyScriptDestroyed(e);
		}
	}

	// ---------------------------------------------------------------------
	// Transform hierarchy
	// ---------------------------------------------------------------------

	void World::UnlinkFromParent(const entt::entity child) const
	{
		auto& reg = m_SystemManager->GetRegistry();
		auto* h = reg.try_get<HierarchyComponent>(child);
		if (!h || h->Parent == entt::null)
		{
			return;
		}
		auto& hc = reg.get<HierarchyComponent>(child);
		if (reg.valid(hc.Parent))
		{
			auto& parent = reg.Write<HierarchyComponent>(hc.Parent);
			if (parent.FirstChild == child)
			{
				parent.FirstChild = hc.NextSibling;
			}
		}
		if (hc.PrevSibling != entt::null && reg.valid(hc.PrevSibling))
		{
			reg.Write<HierarchyComponent>(hc.PrevSibling).NextSibling = hc.NextSibling;
		}
		if (hc.NextSibling != entt::null && reg.valid(hc.NextSibling))
		{
			reg.Write<HierarchyComponent>(hc.NextSibling).PrevSibling = hc.PrevSibling;
		}
		hc.Parent = entt::null;
		hc.NextSibling = entt::null;
		hc.PrevSibling = entt::null;
		reg.MarkChanged<HierarchyComponent>(child);
	}

	void World::SetDepthRecursive(const entt::entity root, const uint32_t depth) const
	{
		auto& reg = m_SystemManager->GetRegistry();
		auto& h = reg.Write<HierarchyComponent>(root);
		h.Depth = depth;
		for (entt::entity c = h.FirstChild; c != entt::null; c = reg.Read<HierarchyComponent>(c).NextSibling)
		{
			SetDepthRecursive(c, depth + 1);
		}
	}

	bool World::IsDescendantOf(const Entity entity, const Entity ancestor) const
	{
		if (!entity || !ancestor)
		{
			return false;
		}
		auto& reg = m_SystemManager->GetRegistry();
		entt::entity cur = entity.Handle();
		while (cur != entt::null)
		{
			const auto* h = reg.try_get_const<HierarchyComponent>(cur);
			if (!h)
			{
				return false;
			}
			if (h->Parent == ancestor.Handle())
			{
				return true;
			}
			cur = h->Parent;
		}
		return false;
	}

	Entity World::GetParent(const Entity entity) const
	{
		if (!entity)
		{
			return {};
		}
		const auto* h = m_SystemManager->GetRegistry().try_get_const<HierarchyComponent>(entity.Handle());
		if (!h || h->Parent == entt::null)
		{
			return {};
		}
		return Entity{h->Parent, const_cast<World*>(this)};
	}

	void World::ForEachChild(const Entity parent, const std::function<void(Entity)>& fn) const
	{
		if (!parent)
		{
			return;
		}
		auto& reg = m_SystemManager->GetRegistry();
		const auto* h = reg.try_get_const<HierarchyComponent>(parent.Handle());
		if (!h)
		{
			return;
		}
		// Snapshot first: the callback may reparent/destroy (which relinks the list we're walking).
		std::vector<entt::entity> children;
		for (entt::entity c = h->FirstChild; c != entt::null; c = reg.Read<HierarchyComponent>(c).NextSibling)
		{
			children.push_back(c);
		}
		for (const entt::entity c : children)
		{
			fn(Entity{c, const_cast<World*>(this)});
		}
	}

	glm::mat4 World::ComputeWorldMatrix(const Entity entity) const
	{
		auto& reg = m_SystemManager->GetRegistry();
		if (!entity || !reg.any_of<TransformComponent>(entity.Handle()))
		{
			return glm::mat4(1.0f);
		}
		glm::mat4 world = reg.Read<TransformComponent>(entity.Handle()).GetTransform();
		const auto* h = reg.try_get_const<HierarchyComponent>(entity.Handle());
		for (entt::entity p = h ? h->Parent : entt::null; p != entt::null;)
		{
			world = reg.Read<TransformComponent>(p).GetTransform() * world;
			const auto* ph = reg.try_get_const<HierarchyComponent>(p);
			p = ph ? ph->Parent : entt::null;
		}
		return world;
	}

	bool World::SetParent(const Entity child, const Entity parent, const bool keepWorld)
	{
		auto& reg = m_SystemManager->GetRegistry();
		if (!child || !reg.any_of<TransformComponent>(child.Handle()))
		{
			SS_CORE_WARN("World::SetParent: child is invalid or has no TransformComponent.");
			return false;
		}
		if (parent && (parent == child || !reg.any_of<TransformComponent>(parent.Handle()) || IsDescendantOf(parent, child)))
		{
			SS_CORE_WARN("World::SetParent: rejected (self-parent, parent without TransformComponent, or cycle).");
			return false;
		}

		const entt::entity newParent = parent ? parent.Handle() : entt::null;
		if (const auto* h = reg.try_get_const<HierarchyComponent>(child.Handle()); h && h->Parent == newParent)
		{
			return true; // already there
		}

		const glm::mat4 childWorld = keepWorld ? ComputeWorldMatrix(child) : glm::mat4(1.0f);

		reg.Ensure<HierarchyComponent>(child.Handle());
		UnlinkFromParent(child.Handle());

		uint32_t depth = 0;
		if (newParent != entt::null)
		{
			auto& ph = reg.Ensure<HierarchyComponent>(newParent);
			// Append as last child (insertion order == sibling order).
			entt::entity last = ph.FirstChild;
			while (last != entt::null && reg.Read<HierarchyComponent>(last).NextSibling != entt::null)
			{
				last = reg.Read<HierarchyComponent>(last).NextSibling;
			}
			auto& ch = reg.Write<HierarchyComponent>(child.Handle());
			ch.Parent = newParent;
			ch.PrevSibling = last;
			ch.NextSibling = entt::null;
			if (last == entt::null)
			{
				reg.Write<HierarchyComponent>(newParent).FirstChild = child.Handle();
			}
			else
			{
				reg.Write<HierarchyComponent>(last).NextSibling = child.Handle();
			}
			depth = reg.Read<HierarchyComponent>(newParent).Depth + 1;
		}
		SetDepthRecursive(child.Handle(), depth);

		if (keepWorld)
		{
			const glm::mat4 parentWorld = parent ? ComputeWorldMatrix(parent) : glm::mat4(1.0f);
			glm::vec3 t, s;
			glm::quat r;
			if (DecomposeTRS(glm::inverse(parentWorld) * childWorld, t, r, s))
			{
				reg.patch<TransformComponent>(child.Handle(), [&](TransformComponent& tr)
				                              {
					tr.Translation = t;
					tr.SetRotation(r);
					tr.Scale = s; });
			}
		}
		else
		{
			reg.MarkChanged<TransformComponent>(child.Handle()); // world matrix changes even if local didn't
		}
		return true;
	}

	void World::Clear() const
	{
		NotifyAllScriptsDestroyed();
		m_SystemManager->GetRegistry().Clear();
	}

	void World::ClearSceneEntities() const
	{
		NotifyAllScriptsDestroyed(); // the persistent editor entities carry no scripts
		m_SystemManager->GetRegistry().ClearExcept<DoNotSerializeComponent>();

		// Advance the scene generation so temporal-history consumers (RenderSystem's TAA / neural upscaler)
		// can detect the wipe and drop their stale per-viewport history — the persistent viewport survives
		// this clear, so without the signal its TAA/neural history would reproject the OLD scene for one
		// frame. See World::SceneGeneration().
		++m_SceneGeneration;

		// Clearing entities leaves editor state (selection, undo history) pointing at destroyed entities.
		// Notify the editor via the hook so it can reset that state — Core no longer names the editor's
		// selection/history types directly (#162). No-op in a runtime (callback unset). (The New-Scene
		// crash itself was a stale VisibilityCache handle in the render pass, fixed there.)
		if (const auto& hooks = GetSingleton<EditorHooksSingleton>(); hooks.OnSceneCleared)
		{
			hooks.OnSceneCleared();
		}
	}

	SystemManager& World::GetSystemManager()
	{
		return *m_SystemManager;
	}

	const SystemManager& World::GetSystemManager() const
	{
		return *m_SystemManager;
	}

	SingletonManager& World::GetSingletonManager()
	{
		return *m_SingletonManager;
	}

	const SingletonManager& World::GetSingletonManager() const
	{
		return *m_SingletonManager;
	}

	TrackedRegistry& World::GetRegistry()
	{
		return m_SystemManager->GetRegistry();
	}

	TrackedRegistry& World::GetRegistry() const
	{
		return m_SystemManager->GetRegistry();
	}

	void World::OnUpdate(const Timestep ts)
	{
		m_SystemManager->ExecuteSystems(ts);
		FlushDestroyQueue(); // apply deferred entity deletions after all systems have run
		m_SingletonManager->GetSingleton<InputStateSingleton>().Clear();
	}
}
