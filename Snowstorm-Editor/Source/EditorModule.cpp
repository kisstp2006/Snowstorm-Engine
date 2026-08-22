#include "EditorModule.hpp"

#include "Examples/MandelbrotSet/MandelbrotControllerSystem.hpp"
#include "Examples/Scripts/OrbitScript.hpp"
#include "Service/ImGuiService.hpp"
#include "Singletons/EditorHistorySingleton.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Singletons/EditorSelectionSingleton.hpp"
#include "Singletons/EditorStatusBarSingleton.hpp"
#include "System/CVarPanelSystem.hpp"
#include "System/CameraFocusSystem.hpp"
#include "System/ConsoleSystem.hpp"
#include "System/ContentBrowserSystem.hpp"
#include "System/DockspaceSetupSystem.hpp"
#include "System/EditorMenuSystem.hpp"
#include "System/EditorNotificationSystem.hpp"
#include "System/LoadingOverlaySystem.hpp"
#include "System/SceneHierarchySystem.hpp"
#include "System/StatusBarSystem.hpp"
#include "System/ViewportDisplaySystem.hpp"
#include "System/ViewportResizeSystem.hpp"

#include <Snowstorm/ECS/SystemManager.hpp>
#include <Snowstorm/Scripting/ScriptRegistry.hpp>
#include <Snowstorm/Service/ServiceManager.hpp>
#include <Snowstorm/World/EditorHooksSingleton.hpp>
#include <Snowstorm/World/SimulationStateSingleton.hpp>
#include <Snowstorm/World/World.hpp>

namespace Snowstorm
{
	void EditorModule::RegisterTypes()
	{
		SS_REGISTER_SCRIPT(OrbitScript); // example script, selectable in the ScriptComponent combo
	}

	void EditorModule::RegisterServices(ServiceManager& services)
	{
		services.RegisterService<ImGuiService>();
	}

	void EditorModule::RegisterWorld(World& world)
	{
		if (world.Type() != WorldType::Editor)
		{
			return; // tooling only on the editor's scene world
		}

		auto& systemManager = world.GetSystemManager();
		auto& singletonManager = world.GetSingletonManager();

		singletonManager.RegisterSingleton<EditorNotificationsSingleton>();
		singletonManager.RegisterSingleton<EditorSelectionSingleton>();
		singletonManager.RegisterSingleton<EditorHistorySingleton>();
		singletonManager.RegisterSingleton<EditorStatusBarSingleton>();
		singletonManager.RegisterSingleton<SimulationStateSingleton>();

		// Install the editor-integration hooks (#162): Core-run systems (RotatorSystem, ComponentRegistry,
		// World::ClearSceneEntities) call these, but Core no longer names the editor's selection/history
		// types. Wire them to the real singletons here. Captured `world` outlives these callbacks (same
		// World owns both the hooks and the target singletons); the hooks singleton is registered by Core
		// in the World ctor, so it already exists — we only fill its callbacks.
		{
			World* const worldPtr = &world;
			auto& hooks = singletonManager.GetSingleton<EditorHooksSingleton>();

			hooks.ManipulatedEntity = [worldPtr]() -> entt::entity
			{
				const auto& sel = worldPtr->GetSingleton<EditorSelectionSingleton>();
				return (sel.GizmoActive && sel.Selected) ? sel.Selected.Handle() : entt::null;
			};

			hooks.HasPendingComponentEdit = [worldPtr]() -> bool
			{
				return worldPtr->GetSingleton<EditorHistorySingleton>().HasPendingEdit();
			};
			hooks.BeginComponentEdit = [worldPtr](const UUID target, const std::string& typeName, nlohmann::json before)
			{
				worldPtr->GetSingleton<EditorHistorySingleton>().BeginEdit(target, typeName, std::move(before));
			};
			hooks.FinalizeComponentEdit = [worldPtr](nlohmann::json after)
			{
				worldPtr->GetSingleton<EditorHistorySingleton>().FinalizeEdit(std::move(after));
			};

			hooks.OnSceneCleared = [worldPtr]()
			{
				// A scene wipe leaves selection/undo pointing at destroyed entities. Reset selection and
				// drop history (its commands reference a world that no longer exists). Consolidates what
				// World::ClearSceneEntities used to do inline for selection, plus the history Clear that
				// TryLoadWorldFromFile did separately.
				auto& sel = worldPtr->GetSingleton<EditorSelectionSingleton>();
				sel.Selected = {};
				sel.GizmoActive = false;
				worldPtr->GetSingleton<EditorHistorySingleton>().Clear();
			};
		}

		// Editor UI systems. The UI phase is empty in a packaged runtime, so the engine
		// systems (registered by RegisterCoreSystems) run identically with or without these.
		// StatusBarSystem first: it reserves the bottom strip via BeginViewportSideBar before
		// DockspaceSetupSystem sizes the dockspace to the (now-shrunk) viewport work-area.
		systemManager.RegisterSystem<StatusBarSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<DockspaceSetupSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ViewportResizeSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ViewportDisplaySystem>(SystemPhase::UI);
		systemManager.RegisterSystem<EditorMenuSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<EditorNotificationSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<SceneHierarchySystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ContentBrowserSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<CameraFocusSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<LoadingOverlaySystem>(SystemPhase::UI);
		systemManager.RegisterSystem<CVarPanelSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ConsoleSystem>(SystemPhase::UI);

		// Editor example
		systemManager.RegisterSystem<MandelbrotControllerSystem>(SystemPhase::PreRender);
		}
}
