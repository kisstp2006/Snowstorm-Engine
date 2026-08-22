#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Advances every AnimationComponent and turns its clip into the skinning matrices a skinning pass
	// consumes (SystemPhase::Logic, before the transform/render resolve reads anything derived from it).
	//
	// Runs in Edit mode too, unlike RotatorSystem: an artist scrubbing the Time field or picking a clip
	// expects to see the pose, and the editor has no other way to preview one. What it must NOT do is
	// advance time while not playing -- see Execute.
	//
	// Deliberately serial: resolving an asset handle goes through AssetManagerSingleton, which is exactly
	// the "touches a singleton" case that disqualifies a system from ParallelForEach (see AGENTS.md). The
	// per-entity work is also tiny compared to the skinning itself, which is the GPU's job.
	class AnimationSystem final : public System
	{
	public:
		explicit AnimationSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
