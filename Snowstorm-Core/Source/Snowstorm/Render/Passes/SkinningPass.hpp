#pragma once

#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Pipeline.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;

	// GPU skin cache (Skinning.comp.hlsl): one dispatch per skinned mesh instance, turning bind-pose
	// vertices + per-vertex bone bindings + this frame's bone matrices into a per-entity vertex buffer.
	//
	// The output is a real buffer, not a vertex-shader transform, because a BLAS is built from vertex
	// MEMORY: a ray query would otherwise traverse the bind pose while the raster image showed the
	// animation. Same reason Unreal requires its Skin Cache for ray-traced skeletal meshes.
	//
	// Descriptor sets are allocated per (frame, dispatch) rather than per frame: several characters skin
	// in the same frame, and one set per frame would have each overwrite the previous one's bindings
	// before the GPU ran them.
	class SkinningPass final
	{
	public:
		// Call once per frame before any dispatch: resets the per-frame descriptor-set cursor.
		void BeginFrame(uint32_t frameIndex);

		// Skin one mesh instance. `boneMatrices` holds `boneCount` mat4s (model-space pose * inverse bind),
		// already uploaded by the caller. No-op until the shader has compiled (async).
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<Buffer>& bindPoseVertices,
		              const Ref<Buffer>& skinBindings, const Ref<Buffer>& boneMatrices, const Ref<Buffer>& outVertices,
		              uint32_t vertexCount, uint32_t boneCount);

		[[nodiscard]] bool IsReady() const { return m_Pipeline != nullptr; }

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;

		// [frameIndex][dispatchIndex]; grown on demand and reused, so a steady scene stops allocating.
		std::vector<std::vector<Ref<DescriptorSet>>> m_Sets;
		std::vector<std::vector<Ref<Buffer>>> m_ParamBuffers;
		std::vector<uint32_t> m_Cursor;
	};
}
