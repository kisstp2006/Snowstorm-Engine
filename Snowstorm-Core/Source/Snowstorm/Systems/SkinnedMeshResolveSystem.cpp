#include "SkinnedMeshResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AnimationComponents.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/MeshRuntimeComponent.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	namespace
	{
		struct AutoRegisterSkinnedMeshRuntime
		{
			AutoRegisterSkinnedMeshRuntime()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false;
				opts.DrawInEditor = false;
				opts.Copyable = false; // a duplicated entity must get its OWN skinned buffer, not share one
				RegisterComponent<SkinnedMeshRuntimeComponent>(opts);
			}
		};
		const AutoRegisterSkinnedMeshRuntime g_autoRegisterSkinnedMeshRuntime;
	}

	void SkinnedMeshResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		// An entity that stopped being skinned goes back to drawing whatever MeshResolveSystem resolves;
		// dropping the runtime component releases its private vertex buffer.
		for (const entt::entity e : FiniView<SkeletalMeshComponent>())
		{
			if (reg.valid(e) && reg.any_of<SkinnedMeshRuntimeComponent>(e))
			{
				reg.remove<SkinnedMeshRuntimeComponent>(e);
			}
		}

		for (const auto view = reg.view<SkeletalMeshComponent, MeshComponent, AnimationRuntimeComponent>();
		     const entt::entity e : view)
		{
			const uint64_t meshHandle = reg.Read<MeshComponent>(e).Mesh.Value();
			auto& runtime = reg.Ensure<SkinnedMeshRuntimeComponent>(e);

			if (runtime.ResolvedMesh != meshHandle || !runtime.Skinned)
			{
				runtime = SkinnedMeshRuntimeComponent{};
				runtime.ResolvedMesh = meshHandle;

				const AssetManagerSingleton::SkinnedMeshGpu* gpu = assets.GetSkinnedMesh(reg.Read<MeshComponent>(e).Mesh);
				if (!gpu || !gpu->BindPose || !gpu->Skin)
				{
					continue; // not a skinned mesh handle (or not loadable): leave the static mesh alone
				}
				runtime.BindPose = gpu->BindPose;
				runtime.SkinBinding = gpu->Skin;
				runtime.VertexCount = gpu->VertexCount;
				runtime.Skinned = Mesh::CreateSkinnedInstance(gpu->BindPose, "SkinnedVertices");
				if (!runtime.Skinned)
				{
					SS_CORE_ERROR("Skinning: could not create the skinned vertex buffer for a mesh of {} vertices.",
					              gpu->VertexCount);
					continue;
				}
			}

			const auto& animation = reg.Read<AnimationRuntimeComponent>(e);
			const auto boneCount = static_cast<uint32_t>(animation.SkinningMatrices.size());
			if (boneCount == 0 || !runtime.Skinned)
			{
				continue; // no pose yet this frame (AnimationSystem hasn't resolved the skeleton)
			}

			// Host-visible: the matrices change every frame, and a staging copy per character per frame
			// would cost more than the write itself. Grown, never shrunk -- a skeleton swap is rare and
			// reallocating on the way back down would just churn.
			if (!runtime.BoneMatrices || runtime.BoneCapacity < boneCount)
			{
				runtime.BoneMatrices = Buffer::Create(sizeof(glm::mat4) * boneCount, BufferUsage::Storage,
				                                      nullptr, true, "SkinningBoneMatrices");
				runtime.BoneCapacity = runtime.BoneMatrices ? boneCount : 0;
			}
			if (runtime.BoneMatrices)
			{
				runtime.BoneMatrices->SetData(animation.SkinningMatrices.data(), sizeof(glm::mat4) * boneCount, 0);
			}

			// Bounds from the POSE, not the bind pose: culling and the shadow fit read them, and a clip that
			// throws a limb outside the bind box would make the character flicker in and out.
			runtime.Skinned->SetBounds(ComputeSkinnedBounds(runtime.BindPose->GetBounds(), animation.SkinningMatrices));

			// Point the render path at the skinned buffer. Everything downstream -- batching, the shadow
			// pass, the TLAS -- takes a Ref<Mesh> and needs no idea that this one deforms.
			if (const auto* meshRuntime = reg.try_get_const<MeshRuntimeComponent>(e);
			    meshRuntime && meshRuntime->Instance != runtime.Skinned)
			{
				// A tracked write: the resolve systems and the renderer watch this component, and swapping
				// the mesh out from under them without marking it changed would leave stale batches.
				reg.patch<MeshRuntimeComponent>(e, [&](MeshRuntimeComponent& mr) { mr.Instance = runtime.Skinned; });
			}
		}
	}
}
