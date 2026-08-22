#include "PhysicsBodyRuntimeComponent.hpp"

#include <Snowstorm/Components/ComponentRegistry.hpp>

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<PhysicsBodyRuntimeComponent>("Snowstorm::PhysicsBodyRuntimeComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterPhysicsBodyRuntime
		{
			AutoRegisterPhysicsBodyRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<PhysicsBodyRuntimeComponent>(opts);
			}
		};
		const AutoRegisterPhysicsBodyRuntime g_autoRegisterPhysicsBodyRuntime;
	}
}
