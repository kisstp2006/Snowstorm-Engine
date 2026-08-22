#include "SkinningSelfTest.hpp"

#include "Snowstorm/Animation/AnimationClip.hpp"
#include "Snowstorm/Animation/Skeleton.hpp"
#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Snowstorm
{
	namespace
	{
		// Anything above this is a real disagreement, not float noise: the shader and the reference do the
		// same multiplies in the same order, so they should agree to within a few ULPs of the magnitudes
		// involved (positions up to ~4 units here).
		constexpr float kTolerance = 1e-3f;
	}

	namespace
	{
		// Linear blend skinning on the CPU: accumulate the weighted matrices, renormalize by the total
		// weight, transform once. Exactly what Skinning.comp.hlsl claims to do -- which is the point.
		std::vector<glm::vec3> ExpectedPositions(const std::vector<Vertex>& bindPose,
		                                         const std::vector<SkinnedVertexWeights>& skin,
		                                         const std::vector<glm::mat4>& boneMatrices)
		{
			const auto boneCount = static_cast<uint32_t>(boneMatrices.size());
			std::vector<glm::vec3> expected;
			expected.reserve(bindPose.size());
			for (size_t v = 0; v < bindPose.size(); ++v)
			{
				glm::mat4 blended(0.0f);
				float totalWeight = 0.0f;
				for (int i = 0; i < 4; ++i)
				{
					const float weight = v < skin.size() ? skin[v].BoneWeights[i] : 0.0f;
					if (weight <= 0.0f || skin[v].BoneIndices[i] >= boneCount)
					{
						continue;
					}
					blended += boneMatrices[skin[v].BoneIndices[i]] * weight;
					totalWeight += weight;
				}
				expected.push_back(totalWeight > 1e-5f
				                       ? glm::vec3(blended / totalWeight * glm::vec4(bindPose[v].Position, 1.0f))
				                       : bindPose[v].Position);
			}
			return expected;
		}

		// Returns the worst position error, and which vertex it was on.
		float WorstError(const Vertex* skinned, const std::vector<glm::vec3>& expected, uint32_t& outVertex)
		{
			float worst = 0.0f;
			outVertex = 0;
			for (uint32_t i = 0; i < expected.size(); ++i)
			{
				if (const float error = glm::length(skinned[i].Position - expected[i]); error > worst)
				{
					worst = error;
					outVertex = i;
				}
			}
			return worst;
		}
	}

	bool SkinningSelfTest::WantsEntityVerification() const
	{
		return CVars::SkinVerify.Get() && m_EntityState == State::Idle;
	}

	void SkinningSelfTest::VerifyEntity(RenderGraph& graph, const Ref<Buffer>& skinnedOutput,
	                                    const std::vector<Vertex>& bindPose,
	                                    const std::vector<SkinnedVertexWeights>& skin,
	                                    const std::vector<glm::mat4>& boneMatrices)
	{
		if (!WantsEntityVerification() || !skinnedOutput || bindPose.empty() || boneMatrices.empty())
		{
			return;
		}

		m_EntityExpected = ExpectedPositions(bindPose, skin, boneMatrices);
		m_EntityVertexCount = static_cast<uint32_t>(bindPose.size());
		m_EntityReadback = Buffer::Create(sizeof(Vertex) * bindPose.size(), BufferUsage::Readback, nullptr, true,
		                                  "SkinVerify.EntityReadback");
		if (!m_EntityReadback)
		{
			m_EntityState = State::Finished;
			return;
		}

		graph.AddPass({.Name = "SkinVerifyEntity",
		               .IsCompute = true,
		               .Execute = [this, skinnedOutput](CommandContext& c)
		               { c.CopyBuffer(skinnedOutput, m_EntityReadback); }});
		m_EntityFramesWaited = 0;
		m_EntityState = State::Dispatched;
	}

	void SkinningSelfTest::BuildResources()
	{
		// Two bones: Root at the origin, Child 2 up. Rotating the root 90 degrees about +Z turns model-space
		// +Y into -X, which makes every expected value below checkable by hand.
		Skeleton skeleton;
		const uint32_t root = skeleton.AddBone("Root", Skeleton::NullIndex, {});
		BoneTransform childRest;
		childRest.Translation = {0.0f, 2.0f, 0.0f};
		const uint32_t child = skeleton.AddBone("Child", root, childRest);
		skeleton.Finalize();

		Pose pose;
		pose.BoneTransforms = skeleton.GetRestPose();
		pose.BoneTransforms[root].Rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));

		std::vector<glm::mat4> boneMatrices;
		ComputeSkinningMatrices(skeleton, pose, boneMatrices);
		m_BoneCount = static_cast<uint32_t>(boneMatrices.size());

		// The vertices, chosen for what they exercise rather than for looking like a mesh.
		struct TestVertex
		{
			glm::vec3 Position;
			glm::uvec4 Bones;
			glm::vec4 Weights;
		};
		const std::vector<TestVertex> inputs{
		    {{1.0f, 0.0f, 0.0f}, {root, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}},   // rigid, on the root
		    {{1.0f, 4.0f, 0.0f}, {child, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}},  // rigid, on the child
		    {{0.0f, 2.0f, 0.0f}, {root, child, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f}}, // blended 50/50
		    {{0.0f, 2.0f, 0.0f}, {root, child, 0, 0}, {0.25f, 0.25f, 0.0f, 0.0f}}, // weights summing to 0.5
		    {{3.0f, 1.0f, 0.0f}, {root, 99u, 0, 0}, {1.0f, 1.0f, 0.0f, 0.0f}},  // one influence out of range
		};
		m_VertexCount = static_cast<uint32_t>(inputs.size());

		// CPU reference: linear blend skinning, exactly what the shader claims to do -- accumulate the
		// weighted matrices, renormalize by the total weight, transform once. An out-of-range bone index
		// contributes nothing (and its weight is not counted), which is what the shader's bounds check does.
		m_ExpectedPositions.clear();
		m_ExpectedPositions.reserve(inputs.size());
		for (const TestVertex& vertex : inputs)
		{
			glm::mat4 blended(0.0f);
			float totalWeight = 0.0f;
			for (int i = 0; i < 4; ++i)
			{
				const float weight = vertex.Weights[i];
				if (weight <= 0.0f || vertex.Bones[i] >= m_BoneCount)
				{
					continue;
				}
				blended += boneMatrices[vertex.Bones[i]] * weight;
				totalWeight += weight;
			}
			m_ExpectedPositions.push_back(totalWeight > 1e-5f
			                                  ? glm::vec3(blended / totalWeight * glm::vec4(vertex.Position, 1.0f))
			                                  : vertex.Position);
		}

		// GPU-side buffers, in the exact layouts the shader declares.
		std::vector<Vertex> bindPose(inputs.size());
		std::vector<SkinnedVertexWeights> skin(inputs.size());
		for (size_t i = 0; i < inputs.size(); ++i)
		{
			bindPose[i].Position = inputs[i].Position;
			bindPose[i].Normal = {0.0f, 1.0f, 0.0f};
			bindPose[i].TexCoord = {0.0f, 0.0f};
			bindPose[i].Tangent = {1.0f, 0.0f, 0.0f, 1.0f};
			skin[i].BoneIndices = inputs[i].Bones;
			skin[i].BoneWeights = inputs[i].Weights;
		}

		const size_t vertexBytes = sizeof(Vertex) * bindPose.size();
		m_BindPose = Buffer::Create(vertexBytes, BufferUsage::Vertex, bindPose.data(), false, "SkinVerify.BindPose");
		m_Skin = Buffer::Create(sizeof(SkinnedVertexWeights) * skin.size(), BufferUsage::Storage, skin.data(), false,
		                        "SkinVerify.Skin");
		m_BoneMatrices = Buffer::Create(sizeof(glm::mat4) * boneMatrices.size(), BufferUsage::Storage,
		                                boneMatrices.data(), false, "SkinVerify.Bones");
		m_Output = Buffer::Create(vertexBytes, BufferUsage::SkinnedVertex, nullptr, false, "SkinVerify.Output");
		m_Readback = Buffer::Create(vertexBytes, BufferUsage::Readback, nullptr, true, "SkinVerify.Readback");
	}

	void SkinningSelfTest::Report()
	{
		const auto* skinned = static_cast<const Vertex*>(m_Readback->Map());
		if (!skinned)
		{
			SS_CORE_ERROR("[skin-verify] could not map the readback buffer.");
			return;
		}

		uint32_t worstVertex = 0;
		const float worstError = WorstError(skinned, m_ExpectedPositions, worstVertex);
		m_Readback->Unmap();

		if (worstError <= kTolerance)
		{
			SS_CORE_INFO("[skin-verify] PASS: {} vertices match the CPU reference (max error {:.6f}).",
			             m_VertexCount, worstError);
		}
		else
		{
			// Loud, because this means the GPU is deforming meshes differently from everything the CPU-side
			// tests assert -- the animation would be wrong on screen and in the BLAS with no other symptom.
			SS_CORE_ERROR("[skin-verify] FAIL: vertex {} is off by {:.6f} (expected [{:.4f} {:.4f} {:.4f}], "
			              "got [{:.4f} {:.4f} {:.4f}]).",
			              worstVertex, worstError, m_ExpectedPositions[worstVertex].x,
			              m_ExpectedPositions[worstVertex].y, m_ExpectedPositions[worstVertex].z,
			              skinned[worstVertex].Position.x, skinned[worstVertex].Position.y,
			              skinned[worstVertex].Position.z);
		}
	}

	void SkinningSelfTest::Update(RenderGraph& graph, SkinningPass& pass, const Ref<CommandContext>& ctx,
	                              const uint32_t frameIndex)
	{
		if (!CVars::SkinVerify.Get() || m_State == State::Finished)
		{
			return;
		}

		if (m_State == State::Idle)
		{
			BuildResources();
			if (!m_Output || !m_Readback)
			{
				SS_CORE_ERROR("[skin-verify] could not create the test buffers.");
				m_State = State::Finished;
				return;
			}

			graph.AddPass({.Name = "SkinVerify",
			               .IsCompute = true,
			               .Execute = [this, &pass, frameIndex](CommandContext& c)
			               {
				               const Ref<CommandContext> self = Renderer::GetGraphicsCommandContext();
				               // No BeginFrame here: the per-frame descriptor cursor belongs to the FRAME,
				               // not to a recorder. Resetting it per pass makes two dispatches share one
				               // descriptor set -- the second rewrite invalidates the command buffer the
				               // first was already recorded into, and the first dispatch runs with the
				               // second's bindings.
				               pass.Dispatch(self, frameIndex, m_BindPose, m_Skin, m_BoneMatrices, m_Output,
				                             m_VertexCount, m_BoneCount);
				               // CopyBuffer inserts the compute-write -> transfer-read barrier itself.
				               c.CopyBuffer(m_Output, m_Readback);
			               }});

			if (!pass.IsReady())
			{
				return; // shader still compiling: try again next frame rather than reading an empty buffer
			}
			m_DispatchFrame = frameIndex;
			m_FramesWaited = 0;
			m_State = State::Dispatched;
			return;
		}

		// Wait for the dispatching frame's slot to come round again: BeginFrame waited on its fence, so the
		// copy has retired and the mapped bytes are this frame's, not a race.
		if (++m_FramesWaited < Renderer::GetFramesInFlight() + 1)
		{
			return;
		}
		Report();
		m_State = State::Finished;
	}

	void SkinningSelfTest::PumpEntity()
	{
		if (m_EntityState != State::Dispatched)
		{
			return;
		}
		if (++m_EntityFramesWaited < Renderer::GetFramesInFlight() + 1)
		{
			return;
		}
		m_EntityState = State::Finished;

		const auto* skinned = static_cast<const Vertex*>(m_EntityReadback->Map());
		if (!skinned)
		{
			SS_CORE_ERROR("[skin-verify] entity: could not map the readback buffer.");
			return;
		}
		uint32_t worstVertex = 0;
		const float worstError = WorstError(skinned, m_EntityExpected, worstVertex);
		m_EntityReadback->Unmap();

		if (worstError <= kTolerance)
		{
			SS_CORE_INFO("[skin-verify] entity PASS: {} vertices match the CPU reference (max error {:.6f}).",
			             m_EntityVertexCount, worstError);
		}
		else
		{
			SS_CORE_ERROR("[skin-verify] entity FAIL: vertex {} is off by {:.6f} (expected [{:.4f} {:.4f} {:.4f}], "
			              "got [{:.4f} {:.4f} {:.4f}]).",
			              worstVertex, worstError, m_EntityExpected[worstVertex].x, m_EntityExpected[worstVertex].y,
			              m_EntityExpected[worstVertex].z, skinned[worstVertex].Position.x,
			              skinned[worstVertex].Position.y, skinned[worstVertex].Position.z);
		}
		m_EntityReadback = nullptr;
	}
}
