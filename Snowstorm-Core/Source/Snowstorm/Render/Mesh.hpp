#pragma once

#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/AccelerationStructure.hpp"
#include "Snowstorm/Render/Buffer.hpp"

#include <vector>

namespace Snowstorm
{
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 TexCoord;
		// Tangent basis for normal mapping. xyz = tangent direction, w = handedness sign for the
		// bitangent (bitangent = cross(normal, tangent.xyz) * tangent.w), the glTF/assimp convention.
		// A plain vec4 keeps the vertex stride 16-byte friendly vs. a separate tangent+bitangent.
		glm::vec4 Tangent{1.0f, 0.0f, 0.0f, 1.0f};
	};

	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

		// Adopt existing buffers instead of uploading data. Used by CreateSkinnedInstance below; kept
		// general because "a Mesh is a vertex buffer + an index buffer + counts" is all the render path
		// ever asks of it.
		Mesh(Ref<Buffer> vertexBuffer, Ref<Buffer> indexBuffer, uint32_t vertexCount, uint32_t indexCount);

		// A per-entity copy of `source` whose VERTEX buffer is writable by the skinning compute pass and
		// whose INDEX buffer is shared with the source (topology doesn't change when a mesh deforms, and
		// duplicating it per character would be pure waste). Bounds are copied from the source; they are
		// the bind-pose bounds, so a wildly deforming mesh needs them refreshed -- see the .cpp.
		[[nodiscard]] static Ref<Mesh> CreateSkinnedInstance(const Ref<Mesh>& source, const std::string& debugName);

		[[nodiscard]] const Ref<Buffer>& GetVertexBuffer() const { return m_VertexBuffer; }
		[[nodiscard]] const Ref<Buffer>& GetIndexBuffer() const { return m_IndexBuffer; }

		[[nodiscard]] uint32_t GetVertexCount() const { return m_VertexCount; }
		[[nodiscard]] uint32_t GetIndexCount() const { return m_IndexCount; }

		[[nodiscard]] const MeshBounds& GetBounds() const { return m_Bounds; }
		void SetBounds(const MeshBounds& b) { m_Bounds = b; }

		// The mesh's ray-tracing BLAS, built lazily on first call and cached (#118). Null when the device has
		// no RT support. Built from this mesh's own vertex/index buffers (Position at offset 0, stride
		// sizeof(Vertex)); a TLAS instance references its device address. Callers gate on RT support.
		[[nodiscard]] const Ref<BLAS>& GetOrBuildBLAS();

		// True for a mesh whose vertices change every frame (a skinned instance): its BLAS is built
		// updatable, and RefitBLAS() re-fits it in place once the skinning pass has rewritten the buffer.
		// Set by CreateSkinnedInstance; a static mesh must NOT pay for it (memory + a little trace cost).
		[[nodiscard]] bool IsDeformable() const { return m_Deformable; }
		void RecordBlasRefit(CommandContext& ctx) const;

		// Variant that builds (and caches) a BLAS carrying an opacity micromap for an alpha-cutout mesh on an
		// OMM-capable device (#OMM). The micromap states are GPU-baked from the material's albedo alpha at the
		// given subdivision level. Returns a null Ref if the bake pipeline isn't ready yet (async compile) — the
		// caller falls back to the FORCE_NO_OPAQUE any-hit path that frame. Gate on IsOpacityMicromapSupported().
		[[nodiscard]] const Ref<BLAS>& GetOrBuildOmmBlas(uint32_t subdivisionLevel, uint32_t albedoTextureIndex,
		                                                 float alphaCutoff, float baseColorAlpha);

	private:
		Ref<Buffer> m_VertexBuffer;
		Ref<Buffer> m_IndexBuffer;

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		MeshBounds m_Bounds{}; //-- bounds won't be set by default

		bool m_Deformable = false; // vertices rewritten every frame -> updatable BLAS
		Ref<BLAS> m_BLAS;    // lazily built on first GetOrBuildBLAS(); null until then / when RT unsupported
		Ref<BLAS> m_OmmBlas; // OMM-carrying BLAS, lazily built on first GetOrBuildOmmBlas() (masked + OMM device)
	};
}
