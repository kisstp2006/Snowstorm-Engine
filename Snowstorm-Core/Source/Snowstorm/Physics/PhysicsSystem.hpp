#pragma once

#include "Snowstorm/Physics/PhysicsSettings.hpp"

namespace Snowstorm
{
	// Engine-side physics facade (Hazel PhysicsSystem): the settings every scene simulates with. The
	// simulation itself is the physics module (JoltScene per World); this header is what Core code and
	// the editor's settings panel talk to without knowing the backend.
	class PhysicsSystem
	{
	public:
		[[nodiscard]] static PhysicsSettings& GetSettings() { return s_Settings; }

	private:
		inline static PhysicsSettings s_Settings;
	};
}
