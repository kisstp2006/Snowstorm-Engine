#include "MaterialRuntimeComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<MaterialRuntimeComponent>("Snowstorm::MaterialRuntimeComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterMaterialRuntime
		{
			AutoRegisterMaterialRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<MaterialRuntimeComponent>(opts);
			}
		};
		const AutoRegisterMaterialRuntime g_autoRegisterMaterialRuntime;
	}
}
