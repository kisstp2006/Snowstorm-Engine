#pragma once

#include "Snowstorm/Animation/AnimationClip.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Core/Base.hpp"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Authored: which skeleton this entity's mesh is skinned to. Separate from MeshComponent because the
	// skeleton is its own asset -- several meshes (a body and its clothing) share one, which is exactly
	// what lets one clip drive all of them.
	struct SkeletalMeshComponent
	{
		AssetHandle Skeleton{0};
	};

	// Authored playback state (Hazel's AnimationComponent). One clip at a time in v1: blending belongs to
	// a layer above this, over Pose, and adding a second slot before there is anything to blend WITH would
	// be a knob nothing turns.
	struct AnimationComponent
	{
		AssetHandle Clip{0};
		bool Playing = true;
		bool Loop = true;
		float Speed = 1.0f;
		float Time = 0.0f; // seconds into the clip; serialized so a paused scene reopens where it was
	};

	// Runtime twin: the resolved assets, the sampled pose and the matrices a skinning pass consumes.
	// Never serialized -- everything here is derived from the two components above.
	struct AnimationRuntimeComponent
	{
		Ref<Snowstorm::Skeleton> Skeleton;
		Ref<AnimationClip> Clip;

		// What the resolved pair was built from, so the system can tell when a handle changed and rebind
		// instead of re-resolving assets every frame.
		uint64_t ResolvedSkeleton = 0;
		uint64_t ResolvedClip = 0;

		// Track -> bone index for THIS (clip, skeleton) pair. Built once at bind time; a per-frame string
		// lookup per track would be absurd.
		std::vector<uint32_t> TrackToBone;

		Pose SampledPose;
		std::vector<glm::mat4> SkinningMatrices; // model-space pose * inverse bind, one per bone
	};
}
