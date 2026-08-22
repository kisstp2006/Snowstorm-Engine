#include "Mesh.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) : m_VertexCount(static_cast<uint32_t>(vertices.size())),
	                                                                                        m_IndexCount(static_cast<uint32_t>(indices.size()))
	{
		SS_CORE_ASSERT(m_VertexCount > 0, "Mesh must have vertices");
		SS_CORE_ASSERT(m_IndexCount > 0, "Mesh must have indices");

		m_VertexBuffer = Buffer::Create(
		    sizeof(Vertex) * vertices.size(),
		    BufferUsage::Vertex,
		    vertices.data(),
		    false,
		    "Mesh Vertex Buffer");

		m_IndexBuffer = Buffer::Create(
		    sizeof(uint32_t) * indices.size(),
		    BufferUsage::Index,
		    indices.data(),
		    false,
		    "Mesh Index Buffer");

		SS_CORE_ASSERT(m_VertexBuffer, "Failed to create mesh vertex buffer");
		SS_CORE_ASSERT(m_IndexBuffer, "Failed to create mesh index buffer");
	}

	Mesh::Mesh(Ref<Buffer> vertexBuffer, Ref<Buffer> indexBuffer, const uint32_t vertexCount, const uint32_t indexCount)
	    : m_VertexBuffer(std::move(vertexBuffer)), m_IndexBuffer(std::move(indexBuffer)),
	      m_VertexCount(vertexCount), m_IndexCount(indexCount)
	{
		SS_CORE_ASSERT(m_VertexBuffer && m_IndexBuffer, "Mesh needs both buffers");
	}

	Ref<Mesh> Mesh::CreateSkinnedInstance(const Ref<Mesh>& source, const std::string& debugName)
	{
		if (!source || source->GetVertexCount() == 0)
		{
			return nullptr;
		}

		// Uninitialized on purpose: the skinning pass writes every vertex before anything reads it, and
		// uploading a bind-pose copy would be a full mesh of pointless PCIe traffic per character.
		Ref<Buffer> skinnedVertices = Buffer::Create(sizeof(Vertex) * source->GetVertexCount(),
		                                             BufferUsage::SkinnedVertex, nullptr, false, debugName);
		if (!skinnedVertices)
		{
			return nullptr;
		}

		Ref<Mesh> instance = CreateRef<Mesh>(std::move(skinnedVertices), source->GetIndexBuffer(),
		                                     source->GetVertexCount(), source->GetIndexCount());
		// Bind-pose bounds. Correct for culling as long as the animation stays roughly within the bind
		// shape; a clip that throws a limb far outside it would need per-frame bounds from the posed
		// skeleton, which is a follow-up, not a v1 requirement.
		instance->SetBounds(source->GetBounds());
		instance->m_Deformable = true; // its vertices change every frame -> its BLAS must be updatable
		return instance;
	}

	const Ref<BLAS>& Mesh::GetOrBuildBLAS()
	{
		if (!m_BLAS)
		{
			// Position is the first Vertex field (offset 0), stride is the whole vertex. The vertex/index
			// buffers carry the AS-build-input usage (added in VulkanBuffer when RT is on), so the build reads
			// them by device address.
			m_BLAS = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                      m_IndexBuffer, m_IndexCount, m_Deformable ? "Skinned BLAS" : "Mesh BLAS",
			                      nullptr, m_Deformable);
		}
		return m_BLAS;
	}

	void Mesh::RecordBlasRefit(CommandContext& ctx) const
	{
		// Only ever meaningful after the skinning pass rewrote this mesh's vertices; a BLAS that was never
		// built has nothing to update (the first GetOrBuildBLAS will build it from the posed vertices).
		if (m_BLAS && m_Deformable)
		{
			m_BLAS->RecordRefit(ctx);
		}
	}

	const Ref<BLAS>& Mesh::GetOrBuildOmmBlas(const uint32_t subdivisionLevel, const uint32_t albedoTextureIndex,
	                                         const float alphaCutoff, const float baseColorAlpha)
	{
		if (!m_OmmBlas)
		{
			const uint32_t triangleCount = m_IndexCount / 3;
			// GPU-bake the micromap states from the material's albedo alpha, then attach to a non-opaque BLAS.
			// Null when the bake pipeline is still compiling: leave m_OmmBlas null (returned below) so the gather
			// falls back to the any-hit path this frame and retries next.
			const Ref<Micromap> micromap =
			    Micromap::CreateBaked(m_VertexBuffer->GetGPUAddress(), m_IndexBuffer->GetGPUAddress(), triangleCount,
			                          subdivisionLevel, albedoTextureIndex, alphaCutoff, baseColorAlpha, "Mesh OMM");
			if (!micromap)
			{
				return m_OmmBlas; // still null; retried next call
			}
			m_OmmBlas = BLAS::Create(m_VertexBuffer, m_VertexCount, sizeof(Vertex), offsetof(Vertex, Position),
			                         m_IndexBuffer, m_IndexCount, "Mesh OMM BLAS", micromap);
		}
		return m_OmmBlas;
	}
}
