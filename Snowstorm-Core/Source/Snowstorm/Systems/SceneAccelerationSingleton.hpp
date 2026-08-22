#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"

#include <vector>

namespace Snowstorm
{
	class TLAS;
	class Mesh;

	// The hand-off between the acceleration structures' CPU half and their GPU half.
	//
	// TlasBuildSystem (PreRender) does the gather -- walking entities, resolving BLASes, filling the instance
	// array -- and leaves the result here. RenderSystem then RECORDS the GPU work into the frame's command
	// buffer, after the skinning dispatch: skin -> refit each deformable BLAS -> build the TLAS -> trace.
	//
	// That split is the whole reason this exists. While the TLAS was built on an immediate submit it ran
	// before the frame's recorded commands, so it could only ever see the PREVIOUS frame's skinning, and the
	// ray-traced view of a character trailed the rasterized one by a frame.
	class SceneAccelerationSingleton final : public Singleton
	{
	public:
		Ref<TLAS> Tlas;             // the scene TLAS; the same object across frames, rebuilt in place
		bool BuildPending = false;  // the gather produced something for this frame to record

		// Skinned meshes whose BLAS needs re-fitting from this frame's skinned vertices, in gather order.
		// Cleared and refilled every gather, so an entity that stops being skinned simply stops appearing.
		std::vector<Ref<Mesh>> DeformableMeshes;
	};
}
