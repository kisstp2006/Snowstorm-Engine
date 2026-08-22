#pragma once

#include <Snowstorm/Core/Module.hpp>

namespace Snowstorm
{
	// The editor as a module: the ImGui service, and — on Editor worlds only — the editor singletons
	// (selection, undo history, notifications, status bar, simulation state), the Core editor-hook
	// callbacks, and the UI-phase systems. A Utility world (the project picker) and a Game world get none
	// of it, so the engine systems run identically with or without the tooling.
	class EditorModule final : public IModule
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Editor"; }
		[[nodiscard]] std::span<const char* const> Dependencies() const override
		{
			static constexpr const char* deps[] = {"Core"};
			return deps;
		}
		void RegisterTypes() override;
		void RegisterServices(ServiceManager& services) override;
		void RegisterWorld(World& world) override;
	};
}
