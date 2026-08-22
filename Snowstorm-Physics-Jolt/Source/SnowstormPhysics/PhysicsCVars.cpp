#include "PhysicsCVars.hpp"

namespace Snowstorm::PhysicsCVars
{
	CVar<bool> DebugDraw{"physics.debug_draw", false, "Draw every physics body's collision shape as a wireframe in the editor viewport (Edit and Play mode).", CVarFlags::Persist};
	CVar<bool> LogStats{"physics.log_stats", false, "Log body / active-body / contact counts once per 60 fixed steps (headless physics diagnostics)."};
}
