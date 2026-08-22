#include "ScriptComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;
		registration::class_<ScriptComponent>("Snowstorm::ScriptComponent")
		    .constructor()
		    .property("ClassName", &ScriptComponent::ClassName)(
		        metadata("ScriptClass", true) // inspector: combo over ScriptRegistry::Names()
		    );
	}

	AUTO_REGISTER_COMPONENT(ScriptComponent);
}
