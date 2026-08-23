#include "AnimationSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/AnimationComponents.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>
#include <glm/common.hpp>

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
				// The outgoing clip's mapping was built against the OLD skeleton's bone order, so a blend
				// across a skeleton change would read the wrong bones. Cancel it.
				runtime.BlendFromClip.reset();
				runtime.BlendFromTrackToBone.clear();
				runtime.BlendTotal = 0.0f;
				runtime.BlendElapsed = 0.0f;
			}
			if (runtime.ResolvedClip != clipHandle || (clipHandle != 0 && !runtime.Clip))
			{
				const auto& animation = reg.Read<AnimationComponent>(e);

				// A clip SWITCH (not the first bind, and not a rebind after the skeleton changed) starts a
				// crossfade: the clip that was playing becomes the outgoing side, at the time it had reached.
				// An interruption mid-blend simply re-arms from the clip that was current -- the in-flight
				// blend is dropped rather than stacked. That can pop when a transition is interrupted early;
				// the real answer there is inertialization (Unreal 5), which needs pose velocity we do not
				// track, so it is a later, self-contained change rather than a chain of blend nodes.
				if (runtime.Clip && animation.BlendDuration > 0.0f && runtime.ResolvedClip != 0)
				{
					runtime.BlendFromClip = runtime.Clip;
					runtime.BlendFromTrackToBone = runtime.TrackToBone;
					runtime.BlendFromTime = animation.Time;
					runtime.BlendFromLoop = animation.Loop;
					runtime.BlendElapsed = 0.0f;
					runtime.BlendTotal = animation.BlendDuration;
				}

				runtime.Clip = assets.GetAnimation(animation.Clip);
				runtime.ResolvedClip = clipHandle;
				runtime.TrackToBone.clear();
				if (runtime.Clip && runtime.Skeleton)
				{
					runtime.TrackToBone = runtime.Clip->BuildTrackToBoneMapping(*runtime.Skeleton);
				}

				// The incoming clip starts at its own beginning, not wherever the outgoing one happened to
				// be. Time is authored state, so patch it rather than writing behind the registry's back.
				reg.patch<AnimationComponent>(e, [](AnimationComponent& a)
				                              { a.Time = 0.0f; });
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

			// Advance the outgoing side by the same dt, so both clips are read at a consistent moment. Only
			// while simulating: in Edit mode a scrub should preview the blend, not run it forward.
			if (runtime.IsBlending() && simulating)
			{
				const float dt = ts.GetSeconds();
				runtime.BlendFromTime += dt * reg.Read<AnimationComponent>(e).Speed;
				runtime.BlendElapsed += dt;
				if (runtime.BlendElapsed >= runtime.BlendTotal)
				{
					// Finished: drop the outgoing clip so the common case costs one sample again.
					runtime.BlendFromClip.reset();
					runtime.BlendFromTrackToBone.clear();
					runtime.BlendTotal = 0.0f;
					runtime.BlendElapsed = 0.0f;
				}
			}

			if (runtime.Clip && runtime.IsBlending())
			{
				// Linear weight over the transition, matching Unity's default CrossFade. An ease curve is a
				// knob nothing turns yet; it belongs with the transition data a state machine would carry.
				const float w = glm::clamp(runtime.BlendElapsed / runtime.BlendTotal, 0.0f, 1.0f);
				runtime.BlendFromClip->Sample(runtime.BlendFromTime, runtime.BlendFromLoop, *runtime.Skeleton,
				                              runtime.BlendFromTrackToBone, runtime.BlendScratchFrom);
				runtime.Clip->Sample(time, loop, *runtime.Skeleton, runtime.TrackToBone, runtime.BlendScratchTo);
				BlendPoses(runtime.BlendScratchFrom, runtime.BlendScratchTo, w, runtime.SampledPose);
			}
			else if (runtime.Clip)
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
