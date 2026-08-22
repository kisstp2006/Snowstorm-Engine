#include "SkinningPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		struct SkinningCB
		{
			uint32_t VertexCount = 0;
			uint32_t BoneCount = 0;
			uint32_t _Pad0 = 0;
			uint32_t _Pad1 = 0;
		};

		// Binding indices in Skinning.comp.hlsl set 0.
		constexpr uint32_t kBindPoseBinding = 0;
		constexpr uint32_t kSkinBinding = 1;
		constexpr uint32_t kBoneMatrixBinding = 2;
		constexpr uint32_t kOutputBinding = 3;
		constexpr uint32_t kParamsBinding = 4;

		constexpr uint32_t kThreadsPerGroup = 64; // matches [numthreads(64,1,1)]
	}

	void SkinningPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/Skinning.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load skinning compute shader");
		if (!cs || !cs->IsReady())
		{
			return; // async compile; the next frame retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "SkinningPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create skinning pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_Sets.resize(frames);
		m_ParamBuffers.resize(frames);
		m_Cursor.assign(frames, 0);
	}

	void SkinningPass::BeginFrame(const uint32_t frameIndex)
	{
		if (frameIndex < m_Cursor.size())
		{
			m_Cursor[frameIndex] = 0;
		}
	}

	void SkinningPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                            const Ref<Buffer>& bindPoseVertices, const Ref<Buffer>& skinBindings,
	                            const Ref<Buffer>& boneMatrices, const Ref<Buffer>& outVertices,
	                            const uint32_t vertexCount, const uint32_t boneCount)
	{
		if (!ctx || !bindPoseVertices || !skinBindings || !boneMatrices || !outVertices || vertexCount == 0 || boneCount == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline || frameIndex >= m_Sets.size())
		{
			return;
		}

		// One descriptor set + params buffer per dispatch this frame. Reusing a single set would have the
		// second character overwrite the first one's bindings before the GPU consumed them.
		const uint32_t slot = m_Cursor[frameIndex]++;
		if (slot >= m_Sets[frameIndex].size())
		{
			const auto& layouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(!layouts.empty() && layouts[0], "Skinning pipeline missing set=0 layout");
			DescriptorSetDesc dsd{};
			dsd.DebugName = "SkinningSet";
			m_Sets[frameIndex].push_back(DescriptorSet::Create(layouts[0], dsd));
			m_ParamBuffers[frameIndex].push_back(
			    Buffer::Create(sizeof(SkinningCB), BufferUsage::Uniform, nullptr, true, "SkinningCB"));
		}

		SkinningCB cb{};
		cb.VertexCount = vertexCount;
		cb.BoneCount = boneCount;
		m_ParamBuffers[frameIndex][slot]->SetData(&cb, sizeof(SkinningCB), 0);

		const Ref<DescriptorSet>& set = m_Sets[frameIndex][slot];
		set->SetBuffer(kBindPoseBinding, {.Buffer = bindPoseVertices});
		set->SetBuffer(kSkinBinding, {.Buffer = skinBindings});
		set->SetBuffer(kBoneMatrixBinding, {.Buffer = boneMatrices});
		set->SetBuffer(kOutputBinding, {.Buffer = outVertices});
		set->SetBuffer(kParamsBinding, {.Buffer = m_ParamBuffers[frameIndex][slot], .Offset = 0, .Range = sizeof(SkinningCB)});
		set->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(set, 0);
		ctx->Dispatch((vertexCount + kThreadsPerGroup - 1) / kThreadsPerGroup, 1, 1);
	}
}
