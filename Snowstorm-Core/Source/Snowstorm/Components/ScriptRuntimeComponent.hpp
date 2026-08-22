#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Scripting/ScriptableEntity.hpp"

namespace Snowstorm
{
	// Runtime twin of ScriptComponent: the live script instance while the simulation plays. Owned by
	// ScriptSystem (created on Play, destroyed on Stop); World guarantees OnDestroy runs before the entity
	// or the scene goes away. Never serialized, copied, or shown.
	struct ScriptRuntimeComponent
	{
		Scope<ScriptableEntity> Instance;
		bool Started = false; // OnStart delivered
	};
}
