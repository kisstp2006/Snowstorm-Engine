#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <memory>
#include <span>
#include <vector>

namespace Snowstorm
{
	class ServiceManager;
	class World;

	// A module is the unit the engine is assembled from (Unreal IModuleInterface, ezEngine ezPlugin): a
	// named bundle of types, application-scoped services and per-World systems/singletons. The executable
	// decides which modules it is made of (the runtime: Core; the editor: Core + Editor; physics is its own
	// module), and ModuleRegistry drives them in dependency order. Today modules are linked statically;
	// loading one from a DLL only changes how the IModule instance is obtained, not this contract.
	class IModule
	{
	public:
		virtual ~IModule() = default;

		[[nodiscard]] virtual const char* Name() const = 0;

		// Names of modules that must initialize before this one (and register their World parts first).
		[[nodiscard]] virtual std::span<const char* const> Dependencies() const { return {}; }

		// Once, at startup, before any service exists: reflection / component / script registration.
		virtual void RegisterTypes() {}

		// Once, at startup, after the render device exists: application-scoped services.
		virtual void RegisterServices(ServiceManager& /*services*/) {}

		// For every World created by the application: singletons and systems (phase-ordered).
		virtual void RegisterWorld(World& /*world*/) {}

		// Reverse dependency order, before services are torn down.
		virtual void Shutdown() {}
	};

	// Convenience for the application constructor: Modules<CoreModule, EditorModule>().
	template <typename... Ms>
	std::vector<Scope<IModule>> Modules()
	{
		std::vector<Scope<IModule>> out;
		(out.emplace_back(CreateScope<Ms>()), ...);
		return out;
	}
}
