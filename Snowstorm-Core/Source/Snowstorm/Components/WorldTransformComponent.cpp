#include "WorldTransformComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<WorldTransformComponent>("Snowstorm::WorldTransformComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterWorldTransform
		{
			AutoRegisterWorldTransform()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false; // derived every frame
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<WorldTransformComponent>(opts);
			}
		};
		const AutoRegisterWorldTransform g_autoRegisterWorldTransform;
	}
}
