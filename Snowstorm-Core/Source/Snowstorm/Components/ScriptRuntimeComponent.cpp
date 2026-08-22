#include "ScriptRuntimeComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		rttr::registration::class_<ScriptRuntimeComponent>("Snowstorm::ScriptRuntimeComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterScriptRuntime
		{
			AutoRegisterScriptRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false;
				RegisterComponent<ScriptRuntimeComponent>(opts);
			}
		};
		const AutoRegisterScriptRuntime g_autoRegisterScriptRuntime;
	}
}
