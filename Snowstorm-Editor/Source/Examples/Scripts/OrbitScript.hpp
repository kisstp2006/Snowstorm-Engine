#pragma once

#include <Snowstorm/Scripting/ScriptableEntity.hpp>

namespace Snowstorm
{
	// Example native script: orbits the entity around the world origin while the simulation plays,
	// restoring nothing on stop (the editor's Play snapshot does that). Exercises the full lifecycle
	// (OnCreate/OnStart/OnUpdate/OnFixedUpdate/OnDestroy) for the smoke test and as a template.
	class OrbitScript final : public ScriptableEntity
	{
	protected:
		void OnStart() override;
		void OnUpdate(Timestep ts) override;
		void OnFixedUpdate(float fixedDt) override;

	private:
		float m_Angle = 0.0f;
		float m_Radius = 1.0f;
		float m_Height = 0.0f;
		int m_FixedTicks = 0;
	};
}
