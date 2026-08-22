#pragma once

#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Passes/SkinningPass.hpp"

#include <glm/mat4x4.hpp>

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class RenderGraph;

	// Headless proof that the GPU skin cache computes what the CPU says it should (CVar anim.skin_verify).
	//
	// Skinning is GPU-only work whose output never reaches a pixel directly -- it lands in a vertex buffer
	// that later passes consume -- so "it looks right" is not available as a check, and a clean smoke run
	// proves only that nothing crashed. This dispatches the real Skinning.comp.hlsl over synthetic data
	// with a known answer, copies the result back, and compares it against ComputeSkinningMatrices (the CPU
	// path the unit tests already pin down). The verdict goes to the log, so the smoke harness catches a
	// regression the same way it catches a validation error.
	//
	// The data is synthetic on purpose: it needs no asset, and it includes the cases most likely to be
	// wrong -- a vertex blended 50/50 between two bones, a vertex whose weights don't sum to 1, and a
	// vertex bound to a bone index past the end of the skeleton.
	class SkinningSelfTest
	{
	public:
		// Called once per frame from the render system. Records the dispatch + readback on the first frame
		// it runs, then latches and reports the result once that frame has retired. Runs exactly once per
		// process: it is a self-test, not a per-frame cost.
		void Update(RenderGraph& graph, SkinningPass& pass, const Ref<CommandContext>& ctx, uint32_t frameIndex);

		// Verify a REAL entity's skinning output, once, against the same CPU reference. The synthetic test
		// above proves the shader; this proves the WIRING -- that the buffers a live entity ends up with are
		// the ones the shader was supposed to get. Called by the render system while recording the skinning
		// pass, so the copy lands right after the dispatch that produced the data.
		void VerifyEntity(RenderGraph& graph, const Ref<Buffer>& skinnedOutput, const std::vector<Vertex>& bindPose,
		                  const std::vector<SkinnedVertexWeights>& skin, const std::vector<glm::mat4>& boneMatrices);

		[[nodiscard]] bool WantsEntityVerification() const;

		// Latches a retired entity readback and reports it. Called every frame alongside Update.
		void PumpEntity();

	private:
		void BuildResources();
		void Report();

		enum class State : uint8_t
		{
			Idle,
			Dispatched,
			Finished
		};
		State m_State = State::Idle;
		uint32_t m_DispatchFrame = 0;
		uint32_t m_FramesWaited = 0;

		Ref<Buffer> m_BindPose;
		Ref<Buffer> m_Skin;
		Ref<Buffer> m_BoneMatrices;
		Ref<Buffer> m_Output;
		Ref<Buffer> m_Readback;

		uint32_t m_VertexCount = 0;
		uint32_t m_BoneCount = 0;
		std::vector<glm::vec3> m_ExpectedPositions; // CPU reference, computed when the resources are built

		// Entity verification (the second, independent check).
		State m_EntityState = State::Idle;
		uint32_t m_EntityFramesWaited = 0;
		Ref<Buffer> m_EntityReadback;
		std::vector<glm::vec3> m_EntityExpected;
		uint32_t m_EntityVertexCount = 0;
	};
}
