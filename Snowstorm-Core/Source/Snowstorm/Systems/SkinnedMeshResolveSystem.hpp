#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/System.hpp"

#include <cstdint>

namespace Snowstorm
{
	class Mesh;
	class Buffer;

	// The GPU-side twin of AnimationRuntimeComponent: this entity's own skinned vertex buffer (wrapped in
	// a Mesh so the whole render path takes it without knowing it is skinned) and the bone matrices the
	// skinning dispatch reads. Never serialized -- all of it is derived from the authored components.
	struct SkinnedMeshRuntimeComponent
	{
		Ref<Mesh> BindPose;      // shared, owned by the asset manager: the skinning input
		Ref<Buffer> SkinBinding; // shared: per-vertex bone indices + weights
		Ref<Mesh> Skinned;       // per entity: the skinning OUTPUT, and what actually gets drawn
		Ref<Buffer> BoneMatrices;

		uint64_t ResolvedMesh = 0; // which mesh handle the resources above were built for
		uint32_t VertexCount = 0;
		uint32_t BoneCapacity = 0; // matrices the bone buffer can hold; grows if a skeleton is swapped
	};

	// Resolve phase, AFTER MeshResolveSystem: gives every animated entity its own skinned vertex buffer
	// and points MeshRuntimeComponent at it, so the draw, shadow and TLAS paths pick up the skinned
	// geometry without a single change of their own. Also uploads this frame's bone matrices.
	//
	// Does NOT dispatch the skinning itself -- that needs a command context and has to happen inside the
	// frame's render graph, before anything draws (RenderSystem).
	class SkinnedMeshResolveSystem final : public System
	{
	public:
		explicit SkinnedMeshResolveSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Edit mode too: a skinned mesh must show its bind pose in the editor, not disappear until Play.
		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
