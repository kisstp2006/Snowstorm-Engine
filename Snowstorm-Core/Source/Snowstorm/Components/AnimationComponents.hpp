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

	// Authored playback state (Hazel's AnimationComponent). One clip plays at a time; CHANGING the clip
	// crossfades into it over BlendDuration, which is the one verb every engine exposes -- Unity's
	// Animator.CrossFade(state, duration), Godot's AnimationPlayer.play(name, custom_blend), the blend time
	// on an Unreal state-machine transition. There is no second clip SLOT here on purpose: the outgoing clip
	// is a consequence of a switch, not something an author picks, so it lives in the runtime twin.
	struct AnimationComponent
	{
		AssetHandle Clip{0};
		bool Playing = true;
		bool Loop = true;
		float Speed = 1.0f;
		float Time = 0.0f; // seconds into the clip; serialized so a paused scene reopens where it was

		// Seconds to crossfade when Clip changes. 0 = hard cut (the old behaviour, still the right choice
		// for a deliberate camera-like cut). Not clamped to the clip length: a blend longer than the
		// incoming clip is unusual but not wrong, and looping clips have no length to speak of.
		float BlendDuration = 0.2f;
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

		// --- Crossfade (only live while a switch is in flight) --------------------------------------
		// The OUTGOING clip keeps playing during the blend rather than freezing at the switch frame. That
		// is what Unity and Unreal do, and it is the difference between a run->walk transition where the
		// feet keep moving and one where the source pose slides. It costs a second sample per frame for
		// the duration of the blend, and nothing at all once it ends.
		Ref<AnimationClip> BlendFromClip;
		std::vector<uint32_t> BlendFromTrackToBone; // that clip's mapping onto THIS skeleton
		float BlendFromTime = 0.0f;                 // advances at the entity's Speed, like the incoming clip
		bool BlendFromLoop = true;
		float BlendElapsed = 0.0f; // 0 -> BlendTotal; weight = BlendElapsed / BlendTotal
		float BlendTotal = 0.0f;   // 0 = no blend in flight

		// Scratch for the two sides of a blend, kept as members so a crossfade does not allocate per frame.
		Pose BlendScratchFrom;
		Pose BlendScratchTo;

		[[nodiscard]] bool IsBlending() const { return BlendTotal > 0.0f && BlendFromClip != nullptr; }
	};
}
