#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Render/Buffer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// Opacity micromap (VK_EXT_opacity_micromap, RDNA4 / Ada+): a per-triangle table of per-microtriangle
	// opacity states (4-state, 2 bits each) attached to a BLAS so the hardware resolves cutout coverage during
	// traversal and invokes the any-hit alpha test only on UNKNOWN (edge) microtriangles. Backend-agnostic
	// handle mirroring BLAS/TLAS; the Vulkan impl wraps a VkMicromapEXT. Create only when the device supports it
	// (Renderer::SupportsOpacityMicromap()).
	class Micromap
	{
	public:
		virtual ~Micromap() = default;

		[[nodiscard]] virtual uint32_t GetTriangleCount() const = 0;
		[[nodiscard]] virtual uint32_t GetSubdivisionLevel() const = 0;

		// Packed 4-state (2 bits/microtriangle) bytes for one triangle at a uniform subdivision level: 4^level
		// microtriangles / 4 per byte, at least 1. The states buffer is triangleCount * this.
		[[nodiscard]] static uint64_t BytesPerTriangle(const uint32_t subdivisionLevel)
		{
			const uint64_t microTris = 1ull << (2u * subdivisionLevel); // 4^level
			return microTris < 4 ? 1 : microTris / 4;
		}

		// Build a 4-state opacity micromap over triangleCount triangles at a uniform subdivision level, from
		// packed 2-bit states (statesData, statesSize = triangleCount * BytesPerTriangle(level)). Synchronous.
		static Ref<Micromap> Create(uint32_t triangleCount, uint32_t subdivisionLevel, const void* statesData,
		                            uint64_t statesSize, const std::string& debugName = "");

		// Baked variant (#OMM B2): builds a micromap whose states are baked on the GPU by sampling the mesh's
		// albedo alpha per microtriangle (vertex/index read by device address). Returns NULL if the bake compute
		// pipeline isn't ready yet (async shader compile) — the caller falls back to the non-OMM path that frame.
		static Ref<Micromap> CreateBaked(uint64_t vertexAddress, uint64_t indexAddress, uint32_t triangleCount,
		                                 uint32_t subdivisionLevel, uint32_t albedoTextureIndex, float alphaCutoff,
		                                 float baseColorAlpha, const std::string& debugName = "");
	};

	// Bottom-level acceleration structure (#118): the ray-traced triangle geometry of a single mesh, built
	// once and reused across frames and TLAS instances. Backend-agnostic handle so it can be cached on the
	// (platform-independent) Mesh; the Vulkan impl wraps a VkAccelerationStructureKHR + its backing buffer.
	// Create only when the device supports RT (Renderer::IsRayTracingSupported()).
	class BLAS
	{
	public:
		virtual ~BLAS() = default;

		// GPU device address of the built AS, used as an instance's accelerationStructureReference in a TLAS.
		[[nodiscard]] virtual uint64_t GetDeviceAddress() const = 0;

		// Rebuild this AS in place from its (unchanged) buffers, whose CONTENTS have moved -- the GPU skin
		// cache case. An update reuses the existing tree topology instead of rebuilding it, which is far
		// cheaper but degrades trace quality as the pose drifts from the one the tree was built for; a
		// character that deforms wildly wants a periodic full rebuild. Only valid on a BLAS created with
		// allowUpdate; a no-op otherwise.
		virtual void RecordRefit(CommandContext& ctx) = 0;

		[[nodiscard]] virtual bool IsUpdatable() const = 0;

		// Build a triangle BLAS from a mesh's vertex/index buffers (both must carry the AS-build-input usage;
		// see VulkanBuffer). positionOffset + vertexStride locate the R32G32B32 position inside each vertex.
		// Synchronous — builds on ImmediateSubmit (graphics queue) and returns once complete. When `micromap`
		// is non-null the geometry is built non-opaque with the micromap chained in, so cutout coverage is
		// resolved per-microtriangle during traversal (the alpha any-hit runs only on UNKNOWN microtriangles).
		// `allowUpdate` asks for an AS that Refit() can update later (skinned meshes). It is NOT the default:
		// it costs memory and a little trace performance, which every static mesh in the scene would pay.
		static Ref<BLAS> Create(const Ref<Buffer>& vertexBuffer, uint32_t vertexCount, uint32_t vertexStride,
		                        uint32_t positionOffset, const Ref<Buffer>& indexBuffer, uint32_t indexCount,
		                        const std::string& debugName = "", const Ref<Micromap>& micromap = nullptr,
		                        bool allowUpdate = false);
	};

	// One renderable in a TLAS: a mesh's BLAS placed by a world transform. The build reads BlasAddress
	// (from BLAS::GetDeviceAddress()) and the upper 3x4 of Transform.
	struct TLASInstance
	{
		glm::mat4 Transform{1.0f};
		uint64_t BlasAddress = 0;
		// Alpha-cutout (glTF MASK): when true the TLAS flags this instance FORCE_NON_OPAQUE so RayQuery surfaces
		// its triangles as candidates and the any-hit alpha test (RTCommitCandidate) runs. Without it the global
		// OPAQUE geometry bit auto-commits every hit and cutout foliage renders solid across ALL RT passes (#151).
		bool ForceNonOpaque = false;
	};

	// Top-level acceleration structure (#118): the scene's set of instanced BLASes that ray-query shaders
	// trace against. Rebuilt/refit per frame as the scene changes (TlasBuildSystem). Backend-agnostic handle;
	// the Vulkan impl owns a VkAccelerationStructureKHR + instance/scratch buffers and exposes its handle to
	// the bindless descriptor. Create only when the device supports RT.
	class TLAS
	{
	public:
		virtual ~TLAS() = default;

		// CPU half of a rebuild: fill the instance array and make sure the AS + scratch are big enough.
		// Returns true when the acceleration structure was (re)created, which is the caller's cue to
		// re-point the bindless descriptor at it -- normally false, because the AS is reused in place.
		virtual bool Prepare(const std::vector<TLASInstance>& instances) = 0;

		// GPU half: record the build into the frame's command buffer, between the barriers the caller
		// emits. Recorded rather than immediately submitted so it lands AFTER this frame's skinning and
		// BLAS refits -- an immediate submit would run before the whole frame, which is what used to force
		// the ray-traced view of a skinned mesh to trail the rasterized one by a frame.
		virtual void RecordBuild(CommandContext& ctx) = 0;

		[[nodiscard]] virtual uint32_t GetInstanceCount() const = 0;

		static Ref<TLAS> Create(const std::string& debugName = "");
	};
}
