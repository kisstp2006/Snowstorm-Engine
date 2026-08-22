#include "CoreModule.hpp"

#include "Snowstorm/Core/FileWatcher.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Lighting/EnvironmentSystem.hpp"
#include "Snowstorm/Lighting/LightingSystem.hpp"
#include "Snowstorm/Render/MeshLibrary.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Scripting/ScriptEvents.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"
#include "Snowstorm/Systems/AssetLoadSystem.hpp"
#include "Snowstorm/Systems/AssetWatchSystem.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/CameraJitterSystem.hpp"
#include "Snowstorm/Systems/CameraPathSystem.hpp"
#include "Snowstorm/Systems/CameraRuntimeUpdateSystem.hpp"
#include "Snowstorm/Systems/MaterialResolveSystem.hpp"
#include "Snowstorm/Systems/MeshResolveSystem.hpp"
#include "Snowstorm/Systems/PrevTransformSnapshotSystem.hpp"
#include "Snowstorm/Systems/PrimaryCameraSystem.hpp"
#include "Snowstorm/Systems/RenderSystem.hpp"
#include "Snowstorm/Systems/RotatorSystem.hpp"
#include "Snowstorm/Systems/RuntimeInitSystem.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/Systems/ShaderReloadSystem.hpp"
#include "Snowstorm/Systems/TlasBuildSystem.hpp"
#include "Snowstorm/Systems/TransformSystem.hpp"
#include "Snowstorm/Systems/VisibilitySystem.hpp"
#include "Snowstorm/World/World.hpp"

namespace Snowstorm
{
	void CoreModule::RegisterServices(ServiceManager& services)
	{
		// Job system first: it's the off-main-thread work pool the others may submit to (async asset
		// loading), and it's device-independent so it can exist before the Vulkan-bound services.
		services.RegisterService<JobSystem>();
		services.RegisterService<FileWatcherService>(); // OS directory notifications for hot reload

		// Device-bound, application-scoped subsystems. Registered after Renderer::Init so the Vulkan device
		// exists. Order among these is not significant (none tick, none depend on another at construction).
		services.RegisterService<RendererService>();
		services.RegisterService<ShaderLibrary>();
		services.RegisterService<MeshLibrary>();
	}

	void CoreModule::RegisterWorld(World& world)
	{
		auto& sm = world.GetSystemManager();
		world.GetSingletonManager().RegisterSingleton<ScriptEventQueue>(); // physics -> script callbacks

		sm.RegisterSystem<RuntimeInitSystem>(SystemPhase::Init);

		sm.RegisterSystem<ScriptSystem>(SystemPhase::Logic);
		sm.RegisterSystem<PrimaryCameraSystem>(SystemPhase::Logic);
		sm.RegisterSystem<CameraControllerSystem>(SystemPhase::Logic);
		sm.RegisterSystem<CameraPathSystem>(SystemPhase::Logic);
		sm.RegisterSystem<RotatorSystem>(SystemPhase::Logic);

		sm.RegisterSystem<ScriptFixedSystem>(SystemPhase::FixedUpdate); // OnFixedUpdate before the physics step

		sm.RegisterSystem<AssetWatchSystem>(SystemPhase::AssetSync); // file changes -> re-import / hot reload
		sm.RegisterSystem<ShaderReloadSystem>(SystemPhase::AssetSync);
		sm.RegisterSystem<AssetLoadSystem>(SystemPhase::AssetSync);

		sm.RegisterSystem<TransformSystem>(SystemPhase::Resolve); // world matrices first: everything below reads them
		sm.RegisterSystem<CameraRuntimeUpdateSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MeshResolveSystem>(SystemPhase::Resolve);
		sm.RegisterSystem<MaterialResolveSystem>(SystemPhase::Resolve);

		sm.RegisterSystem<EnvironmentSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<LightingSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<VisibilitySystem>(SystemPhase::PreRender);
		// After the resolve systems so MeshRuntimeComponent instances are populated; no-op on a non-RT device.
		sm.RegisterSystem<TlasBuildSystem>(SystemPhase::PreRender);
		sm.RegisterSystem<CameraJitterSystem>(SystemPhase::PreRender);

		sm.RegisterSystem<RenderSystem>(SystemPhase::Render);
		// Last: snapshots this frame's world matrices / view-projection as next frame's "previous".
		sm.RegisterSystem<PrevTransformSnapshotSystem>(SystemPhase::Render);
	}
}
