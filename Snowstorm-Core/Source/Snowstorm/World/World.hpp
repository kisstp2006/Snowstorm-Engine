#pragma once

#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/ECS/SingletonManager.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Input/InputEventBridge.hpp"
#include "Snowstorm/Utility/NonCopyable.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <entt/entt.hpp>
#include <glm/mat4x4.hpp>

#include <functional>
#include <vector>

namespace Snowstorm
{
	class SystemManager;
	class Entity;

	// What a World is for (Unreal EWorldType): modules consult it in RegisterWorld — the editor module adds
	// its UI systems only to an Editor world, never to the runtime's Game world or a Utility world (the
	// project picker's placeholder, test worlds).
	enum class WorldType : uint8_t
	{
		Game,
		Editor,
		Utility,
	};

	class World final : public NonCopyable
	{
	public:
		explicit World(WorldType type = WorldType::Game);

		[[nodiscard]] WorldType Type() const { return m_Type; }
		// Out-of-line: m_SystemManager is a unique_ptr to a forward-declared type, so the
		// destructor must be emitted in World.cpp where SystemManager is a complete type.
		~World();

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());

		// Find an entity by its IDComponent UUID, or return an invalid Entity if none matches. Linear
		// scan over view<IDComponent> — fine at editor entity counts. Used by undo/redo commands, which
		// must reference entities by stable UUID (entt handles are recycled across destroy/create).
		[[nodiscard]] Entity FindEntityByUUID(UUID uuid) const;

		// Queue an entity for destruction at the end of the current frame. Deferred so callers can
		// request deletion while iterating an ECS view (e.g. from a UI system) without invalidating
		// the iteration. FlushDestroyQueue() performs the actual destroy and is called once per frame.
		void DestroyEntity(Entity entity);
		void FlushDestroyQueue();

		// ---- Transform hierarchy (the only way to mutate HierarchyComponent) ----

		// Attach `child` under `parent` (an invalid `parent` detaches to root). Both need a
		// TransformComponent. `keepWorld` (Unity Transform.SetParent(p, worldPositionStays=true)) rewrites
		// the child's local transform so it stays put in world space; false keeps the local values (what
		// the serializer wants). Rejects self-parenting and cycles. Appends as the last child, so sibling
		// order is insertion order (deterministic saves).
		bool SetParent(Entity child, Entity parent, bool keepWorld = true);
		[[nodiscard]] Entity GetParent(Entity entity) const;
		[[nodiscard]] bool IsDescendantOf(Entity entity, Entity ancestor) const;
		void ForEachChild(Entity parent, const std::function<void(Entity)>& fn) const;

		// Exact world matrix computed by walking the parent chain (not the per-frame cache). Use when a
		// world-space result is needed in the same frame the local transform/hierarchy was edited (gizmo,
		// reparent-keep-world, UI-phase picking); hot per-frame consumers read WorldTransformComponent.
		[[nodiscard]] glm::mat4 ComputeWorldMatrix(Entity entity) const;

		void Clear() const;

		// "Open Scene" wipe: destroys scene entities but keeps engine-owned ones tagged
		// DoNotSerializeComponent (the editor's persistent Scene-view camera/viewport) alive across the
		// load. Use this for scene transitions; Clear() is full teardown.
		void ClearSceneEntities() const;

		// Monotonic counter bumped on every ClearSceneEntities (scene wipe / Open Scene / New Scene). It is
		// the engine's "camera cut" signal (cf. Unreal bCameraCut, Unity's history reset on scene load):
		// the persistent editor viewport survives a wipe (it's DoNotSerialize), so any per-viewport temporal
		// history — TAA, the neural temporal upscaler — is now pointing at the PREVIOUS scene's frame while
		// its "history valid" flag still reads true, bleeding a one-frame ghost of the old scene into the
		// new one. A consumer records the generation it last saw and resets its history when it changes.
		[[nodiscard]] uint64_t SceneGeneration() const { return m_SceneGeneration; }

		[[nodiscard]] SystemManager& GetSystemManager();
		[[nodiscard]] const SystemManager& GetSystemManager() const;

		[[nodiscard]] SingletonManager& GetSingletonManager();
		[[nodiscard]] const SingletonManager& GetSingletonManager() const;

		[[nodiscard]] TrackedRegistry& GetRegistry();
		[[nodiscard]] TrackedRegistry& GetRegistry() const;

		template <typename T>
		T& GetSingleton() const
		{
			return m_SingletonManager->GetSingleton<T>();
		}

		template <typename T>
		bool HasSingleton() const
		{
			return m_SingletonManager->HasSingleton<T>();
		}

		void OnUpdate(Timestep ts);

	private:
		Scope<SystemManager> m_SystemManager;
		Scope<SingletonManager> m_SingletonManager;

		Scope<InputEventBridge> m_InputEventBridge;

		std::vector<entt::entity> m_PendingDestroy; // flushed at end of frame by FlushDestroyQueue
		WorldType m_Type = WorldType::Game;

		void UnlinkFromParent(entt::entity child) const;
		void SetDepthRecursive(entt::entity root, uint32_t depth) const;

		// Bumped by ClearSceneEntities (which is const, so this is mutable). See SceneGeneration().
		mutable uint64_t m_SceneGeneration = 0;

		friend class Entity;
		friend class SceneHierarchyPanel;
	};
}
