#pragma once

#include "Snowstorm/Animation/Skeleton.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	// An instantaneous skeleton state: one BONE-LOCAL transform per bone, in skeleton bone order (Hazel's
	// Pose). Model-space composition and the skinning matrices are derived from it, never stored in it --
	// a pose is what an animation produces and what a future blend node would interpolate.
	struct Pose
	{
		std::vector<BoneTransform> BoneTransforms;
		float TimePos = 0.0f; // seconds into the clip this pose was sampled at (diagnostics / root motion)
	};

	// Keyframe tracks for one bone. Each channel is independent (a bone can be rotated every frame but
	// never translated), and an empty channel means "keep the skeleton's rest pose value" rather than
	// "identity" -- getting that wrong collapses every untranslated bone onto the origin.
	struct BoneTrack
	{
		std::vector<float> TranslationTimes;
		std::vector<glm::vec3> TranslationKeys;
		std::vector<float> RotationTimes;
		std::vector<glm::quat> RotationKeys;
		std::vector<float> ScaleTimes;
		std::vector<glm::vec3> ScaleKeys;

		[[nodiscard]] bool IsEmpty() const { return TranslationKeys.empty() && RotationKeys.empty() && ScaleKeys.empty(); }
	};

	// One animation clip: raw keyframes, sampled with linear interpolation (slerp for rotation).
	//
	// SKELETON-INDEPENDENT on purpose: a track names the BONE it drives, and binding to a skeleton is a
	// separate lookup (BuildTrackToBoneMapping). Indexing tracks by the importing skeleton's bone order
	// would make a clip playable only on the exact skeleton it came from -- which breaks the standard
	// workflow of one file per animation sharing a character's skeleton, and is the difference between an
	// animation being an ASSET and being an appendix of one mesh.
	//
	// Deliberately NOT Hazel's shape here: Hazel compresses tracks with ACL and hides them behind a void*.
	// That is a dependency and a format we do not need to evaluate playback -- and an uncompressed clip is
	// something a test can assert exact values against. Compression is a later, isolated change: it only
	// has to keep Sample() answering the same values.
	class AnimationClip
	{
	public:
		explicit AnimationClip(std::string name = {})
		    : m_Name(std::move(name))
		{
		}

		[[nodiscard]] const std::string& GetName() const { return m_Name; }
		[[nodiscard]] float GetDuration() const { return m_Duration; }
		void SetDuration(const float duration) { m_Duration = duration; }

		// One track per ANIMATED bone, in file order; returns the new track's index. A clip only carries
		// the bones it actually drives, so an additive/partial clip costs nothing for the rest.
		uint32_t AddTrack(std::string boneName);
		[[nodiscard]] uint32_t GetTrackCount() const { return static_cast<uint32_t>(m_Tracks.size()); }
		[[nodiscard]] BoneTrack& GetTrack(const uint32_t trackIndex) { return m_Tracks[trackIndex]; }
		[[nodiscard]] const BoneTrack& GetTrack(const uint32_t trackIndex) const { return m_Tracks[trackIndex]; }
		[[nodiscard]] const std::string& GetTrackBoneName(const uint32_t trackIndex) const { return m_TrackBoneNames[trackIndex]; }

		// track index -> skeleton bone index (Skeleton::NullIndex for a bone this skeleton does not have).
		// Build it ONCE per (clip, skeleton) pair and keep it: it is a string lookup per track, which is
		// fine at bind time and would be absurd per frame.
		[[nodiscard]] std::vector<uint32_t> BuildTrackToBoneMapping(const Skeleton& skeleton) const;

		// Fills `outPose` with bone-local transforms at `time` seconds. `loop` wraps into [0, duration);
		// otherwise the time is clamped, so a non-looping clip holds its last frame. Bones this clip does
		// not drive come out at the skeleton's rest pose.
		void Sample(float time, bool loop, const Skeleton& skeleton, const std::vector<uint32_t>& trackToBone,
		            Pose& outPose) const;

	private:
		std::string m_Name;
		float m_Duration = 0.0f;
		std::vector<BoneTrack> m_Tracks;
		std::vector<std::string> m_TrackBoneNames; // parallel to m_Tracks
	};

	// The matrices a skinning shader consumes: for each bone, model-space pose * inverse bind. A vertex is
	// skinned as sum(weight_i * SkinningMatrix[bone_i] * vertexPosition), so feeding the REST pose here
	// yields identity matrices and leaves the mesh in its bind shape -- which is the cheapest sanity check
	// there is, and the one the tests use.
	void ComputeSkinningMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices);

	// Model-space transform per bone (pose composed down the hierarchy), without the inverse bind. Useful
	// on its own for attaching entities to bones and for debug-drawing a skeleton.
	void ComputeModelSpaceTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices);
}
