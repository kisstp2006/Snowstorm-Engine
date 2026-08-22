#pragma once

#include "Snowstorm/Core/Module.hpp"

#include <string>
#include <vector>

namespace Snowstorm
{
	// Owns the application's modules and calls their hooks in dependency order (a stable topological sort
	// of IModule::Dependencies; declaration order breaks ties). Pure data->order logic in Resolve() so the
	// ordering is unit-testable without an Application.
	class ModuleRegistry final
	{
	public:
		void Add(Scope<IModule> module);

		// Sorts and runs RegisterTypes + RegisterServices. Fails loudly (SS_CORE_VERIFY) on an unknown
		// dependency or a cycle; the offending module is then skipped rather than crashing later.
		void Initialize(ServiceManager& services);
		void RegisterWorld(World& world) const;
		void Shutdown();

		[[nodiscard]] bool IsLoaded(const std::string& name) const;
		[[nodiscard]] std::vector<std::string> OrderedNames() const;

		// Pure ordering: returns the indices of `modules` in a valid initialization order, or an empty
		// vector (and fills `error`) when a dependency is missing or cyclic.
		static std::vector<size_t> Resolve(const std::vector<IModule*>& modules, std::string& error);

	private:
		std::vector<Scope<IModule>> m_Modules; // in resolved order after Initialize
		bool m_Initialized = false;
	};
}
