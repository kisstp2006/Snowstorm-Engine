#include "MeshRuntimeComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<MeshRuntimeComponent>("Snowstorm::MeshRuntimeComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterMeshRuntime
		{
			AutoRegisterMeshRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<MeshRuntimeComponent>(opts);
			}
		};
		const AutoRegisterMeshRuntime g_autoRegisterMeshRuntime;
	}
}
