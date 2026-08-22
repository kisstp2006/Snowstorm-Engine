#include "PhysicsJoltModule.hpp"

#include "JoltJobSystem.hpp"
#include "PhysicsSystems.hpp"
#include "PhysicsWorldSingleton.hpp"

#include <Snowstorm/Core/JobSystem.hpp>
#include <Snowstorm/Core/Log.hpp>
#include <Snowstorm/ECS/SystemManager.hpp>
#include <Snowstorm/Service/ServiceManager.hpp>
#include <Snowstorm/World/World.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>

namespace Snowstorm
{
	namespace
	{
		bool g_JoltInitialized = false;

		void JoltTrace(const char* fmt, ...)
		{
			char buffer[1024];
			va_list list;
			va_start(list, fmt);
			vsnprintf(buffer, sizeof(buffer), fmt, list);
			va_end(list);
			SS_CORE_INFO("[Jolt] {}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailed(const char* expression, const char* message, const char* file, const JPH::uint line)
		{
			SS_CORE_ERROR("[Jolt] assert '{}' ({}) at {}:{}", expression, message ? message : "", file, line);
			return true; // break
		}
#endif
	}

	void PhysicsJoltModule::EnsureJoltInitialized()
	{
		if (g_JoltInitialized)
		{
			return;
		}
		JPH::RegisterDefaultAllocator();
		JPH::Trace = JoltTrace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
		g_JoltInitialized = true;
#ifdef JPH_DEBUG_RENDERER
		constexpr const char* debugRenderer = "on";
#else
		constexpr const char* debugRenderer = "off";
#endif
		SS_CORE_INFO("Jolt Physics initialized (debug renderer: {}).", debugRenderer);
	}

	void PhysicsJoltModule::RegisterServices(ServiceManager& services)
	{
		EnsureJoltInitialized();
		// One job pool for the whole engine: Jolt's jobs run on the Core JobSystem's workers.
		services.RegisterService<JoltJobSystem>(services.GetService<JobSystem>());
	}

	void PhysicsJoltModule::RegisterWorld(World& world)
	{
		world.GetSingletonManager().RegisterSingleton<PhysicsWorldSingleton>(&world);

		auto& sm = world.GetSystemManager();
		// Resolve: write-back BEFORE TransformSystem (order -10) so the simulated pose propagates to
		// children this frame; body sync AFTER it (+10) so new/edited bodies read finished world matrices.
		sm.RegisterSystemOrdered<PhysicsWriteBackSystem>(SystemPhase::Resolve, -10);
		sm.RegisterSystemOrdered<PhysicsBodySyncSystem>(SystemPhase::Resolve, 10);
		// FixedUpdate: after Core's ScriptFixedSystem (OnFixedUpdate precedes the step, Unity order).
		sm.RegisterSystem<PhysicsStepSystem>(SystemPhase::FixedUpdate);
		sm.RegisterSystem<PhysicsDebugDrawSystem>(SystemPhase::PreRender);
	}

	void PhysicsJoltModule::Shutdown()
	{
		if (!g_JoltInitialized)
		{
			return;
		}
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
		g_JoltInitialized = false;
	}
}
