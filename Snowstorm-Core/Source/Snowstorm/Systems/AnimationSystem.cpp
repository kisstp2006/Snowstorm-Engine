#include "AnimationSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AnimationComponents.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void AnimationSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		// Time only advances while the simulation runs. In Edit mode the pose still gets rebuilt every
		// frame, so scrubbing the Time field or switching clips previews immediately -- it just doesn't
		// run away on its own while the artist is looking at it.
		const bool simulating = !m_World->HasSingleton<SimulationStateSingleton>() ||
		                        m_World->GetSingleton<SimulationStateSingleton>().IsPlaying();

		for (const auto view = reg.view<SkeletalMeshComponent, AnimationComponent>(); const entt::entity e : view)
		{
			const auto& skeletal = reg.Read<SkeletalMeshComponent>(e);
			auto& runtime = reg.Ensure<AnimationRuntimeComponent>(e);

			// (Re)bind whenever either handle moved. Resolving assets and mapping tracks to bones is the
			// expensive part, so it happens on a change, not per frame.
			const uint64_t skeletonHandle = skeletal.Skeleton.Value();
			const uint64_t clipHandle = reg.Read<AnimationComponent>(e).Clip.Value();
			if (runtime.ResolvedSkeleton != skeletonHandle || !runtime.Skeleton)
			{
				runtime.Skeleton = assets.GetSkeleton(skeletal.Skeleton);
				runtime.ResolvedSkeleton = skeletonHandle;
				runtime.ResolvedClip = 0; // the mapping belongs to a (clip, skeleton) PAIR
				runtime.SkinningMatrices.clear();
			}
			if (runtime.ResolvedClip != clipHandle || (clipHandle != 0 && !runtime.Clip))
			{
				runtime.Clip = assets.GetAnimation(reg.Read<AnimationComponent>(e).Clip);
				runtime.ResolvedClip = clipHandle;
				runtime.TrackToBone.clear();
				if (runtime.Clip && runtime.Skeleton)
				{
					runtime.TrackToBone = runtime.Clip->BuildTrackToBoneMapping(*runtime.Skeleton);
				}
			}

			if (!runtime.Skeleton)
			{
				continue; // no skeleton resolved (missing/!skinned asset, warned once by the loader)
			}

			float time = 0.0f;
			bool loop = true;
			if (const auto& animation = reg.Read<AnimationComponent>(e); animation.Playing && simulating)
			{
				// Patch, not a raw write: Time is authored state, and other systems (and the inspector)
				// watch it change.
				reg.patch<AnimationComponent>(e, [&](AnimationComponent& a)
				                              { a.Time += ts.GetSeconds() * a.Speed; });
				time = reg.Read<AnimationComponent>(e).Time;
				loop = animation.Loop;
			}
			else
			{
				time = animation.Time;
				loop = animation.Loop;
			}

			if (runtime.Clip)
			{
				runtime.Clip->Sample(time, loop, *runtime.Skeleton, runtime.TrackToBone, runtime.SampledPose);
			}
			else
			{
				// A skeleton with no clip is still a valid thing to draw: the bind pose. Without this an
				// entity whose clip failed to resolve would render with stale matrices from the last one.
				runtime.SampledPose.BoneTransforms = runtime.Skeleton->GetRestPose();
				runtime.SampledPose.TimePos = 0.0f;
			}

			ComputeSkinningMatrices(*runtime.Skeleton, runtime.SampledPose, runtime.SkinningMatrices);
		}
	}
}
