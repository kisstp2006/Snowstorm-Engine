#pragma once

#include "Snowstorm/Core/Module.hpp"

namespace Snowstorm
{
	// The engine itself as a module: job system + GPU resource services, and the built-in ECS systems in
	// their phases. Every executable lists it first; everything else depends on "Core". Component
	// reflection/registration stays in the per-component static initializers (RTTR_REGISTRATION +
	// AUTO_REGISTER_COMPONENT, kept alive by WHOLE_ARCHIVE), so RegisterTypes has nothing to add here.
	class CoreModule final : public IModule
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Core"; }
		void RegisterServices(ServiceManager& services) override;
		void RegisterWorld(World& world) override;
	};
}
