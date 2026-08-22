#pragma once

#include <Snowstorm/Core/Module.hpp>

namespace Snowstorm
{
	// The physics module (ezEngine JoltPlugin shape): Jolt global init + the JoltJobSystem service at
	// startup, one PhysicsWorldSingleton per World, and the body-sync / step / write-back / debug-draw
	// systems in their phases. Depends on "Core" (job system, script events, transform hierarchy).
	class PhysicsJoltModule final : public IModule
	{
	public:
		[[nodiscard]] const char* Name() const override { return "PhysicsJolt"; }
		[[nodiscard]] std::span<const char* const> Dependencies() const override
		{
			static constexpr const char* deps[] = {"Core"};
			return deps;
		}
		void RegisterServices(ServiceManager& services) override;
		void RegisterWorld(World& world) override;
		void Shutdown() override;

		// Jolt's process-wide state (allocator, factory, type registry). Idempotent; the module calls it
		// from RegisterServices and tests call it directly.
		static void EnsureJoltInitialized();
	};
}
