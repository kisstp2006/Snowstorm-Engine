#pragma once

#include <string>

namespace Snowstorm
{
	// Authored: which registered native script class drives this entity (ScriptRegistry name). The
	// live instance is ScriptRuntimeComponent, created by ScriptSystem when the simulation plays.
	struct ScriptComponent
	{
		std::string ClassName;
	};
}
