#pragma once

#include <Snowstorm/Utility/CVar.hpp>

namespace Snowstorm::PhysicsCVars
{
	// Module-owned CVars (self-registering, like EngineCVars): physics.debug_draw, physics.log_stats.
	extern CVar<bool> DebugDraw;
	extern CVar<bool> LogStats;
}
