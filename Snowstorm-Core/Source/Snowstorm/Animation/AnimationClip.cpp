#include "AnimationClip.hpp"

#include "Snowstorm/Core/Base.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Snowstorm
{
	namespace
	{
		// Index of the last key at or before `time`, plus the blend factor to the next one. Keys are sorted
		// by time, so this is a binary search -- linear scanning would be O(bones * keys) every frame.
		void FindKeyPair(const std::vector<float>& times, const float time, size_t& outIndex, float& outAlpha)
		{
			outIndex = 0;
			outAlpha = 0.0f;
			if (times.size() < 2)
			{
				return;
			}
			if (time <= times.front())
			{
				return;
			}
			if (time >= times.back())
			{
				outIndex = times.size() - 1;
				return;
			}

			const auto upper = std::ranges::upper_bound(times, time);
			const size_t next = static_cast<size_t>(std::distance(times.begin(), upper));
			outIndex = next - 1;

			const float span = times[next] - times[outIndex];
			outAlpha = span > 1e-8f ? (time - times[outIndex]) / span : 0.0f;
		}

		glm::vec3 SampleVec3(const std::vector<float>& times, const std::vector<glm::vec3>& keys, const float time,
		                     const glm::vec3& fallback)
		{
			if (keys.empty())
			{
				return fallback;
			}
			if (keys.size() == 1)
			{
				return keys.front();
			}
			size_t index = 0;
			float alpha = 0.0f;
			FindKeyPair(times, time, index, alpha);
			return index + 1 < keys.size() ? glm::mix(keys[index], keys[index + 1], alpha) : keys.back();
		}

		glm::quat SampleQuat(const std::vector<float>& times, const std::vector<glm::quat>& keys, const float time,
		                     const glm::quat& fallback)
		{
			if (keys.empty())
			{
				return fallback;
			}
			if (keys.size() == 1)
			{
				return keys.front();
			}
			size_t index = 0;
			float alpha = 0.0f;
			FindKeyPair(times, time, index, alpha);
			if (index + 1 >= keys.size())
			{
				return keys.back();
			}
			// slerp, not mix: lerping quaternions shortens the arc and makes a fast rotation slow down in
			// the middle of the interval. glm::slerp already takes the shortest path.
			return glm::normalize(glm::slerp(keys[index], keys[index + 1], alpha));
		}
	}

	uint32_t AnimationClip::AddTrack(std::string boneName)
	{
		const auto index = static_cast<uint32_t>(m_Tracks.size());
		m_Tracks.emplace_back();
		m_TrackBoneNames.push_back(std::move(boneName));
		return index;
	}

	std::vector<uint32_t> AnimationClip::BuildTrackToBoneMapping(const Skeleton& skeleton) const
	{
		std::vector<uint32_t> mapping(m_Tracks.size(), Skeleton::NullIndex);
		for (size_t track = 0; track < m_TrackBoneNames.size(); ++track)
		{
			mapping[track] = skeleton.FindBoneIndex(m_TrackBoneNames[track]);
		}
		return mapping;
	}

	void AnimationClip::Sample(const float time, const bool loop, const Skeleton& skeleton,
	                           const std::vector<uint32_t>& trackToBone, Pose& outPose) const
	{
		const uint32_t boneCount = skeleton.GetBoneCount();
		outPose.BoneTransforms.resize(boneCount);

		float sampleTime = time;
		if (m_Duration > 0.0f)
		{
			if (loop)
			{
				sampleTime = std::fmod(time, m_Duration);
				if (sampleTime < 0.0f)
				{
					sampleTime += m_Duration; // fmod keeps the sign, so a negative time would run backwards
				}
			}
			else
			{
				sampleTime = std::clamp(time, 0.0f, m_Duration);
			}
		}
		else
		{
			sampleTime = 0.0f;
		}
		outPose.TimePos = sampleTime;

		// Start from the rest pose: a clip only drives the bones it has tracks for, and "not animated"
		// means "stay where the skeleton put you", never "identity".
		for (uint32_t bone = 0; bone < boneCount; ++bone)
		{
			outPose.BoneTransforms[bone] = skeleton.GetRestPose(bone);
		}

		const size_t trackCount = std::min(m_Tracks.size(), trackToBone.size());
		for (size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
		{
			const uint32_t bone = trackToBone[trackIndex];
			if (bone == Skeleton::NullIndex || bone >= boneCount)
			{
				continue; // a bone this skeleton doesn't have: the clip simply doesn't drive anything here
			}
			const BoneTrack& track = m_Tracks[trackIndex];
			if (track.IsEmpty())
			{
				continue;
			}

			const BoneTransform& rest = skeleton.GetRestPose(bone);
			BoneTransform& out = outPose.BoneTransforms[bone];
			out.Translation = SampleVec3(track.TranslationTimes, track.TranslationKeys, sampleTime, rest.Translation);
			out.Rotation = SampleQuat(track.RotationTimes, track.RotationKeys, sampleTime, rest.Rotation);
			out.Scale = SampleVec3(track.ScaleTimes, track.ScaleKeys, sampleTime, rest.Scale);
		}
	}

	void ComputeModelSpaceTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices)
	{
		const uint32_t boneCount = skeleton.GetBoneCount();
		outMatrices.assign(boneCount, glm::mat4(1.0f));

		// Single forward pass, valid because a bone's parent always has a smaller index (Skeleton's
		// invariant). Composed as matrices so a non-uniform bone scale stays exact.
		for (uint32_t bone = 0; bone < boneCount; ++bone)
		{
			const glm::mat4 local = bone < pose.BoneTransforms.size() ? pose.BoneTransforms[bone].ToMatrix()
			                                                          : skeleton.GetRestPose(bone).ToMatrix();
			const uint32_t parent = skeleton.GetParentBoneIndex(bone);
			outMatrices[bone] = parent == Skeleton::NullIndex ? local : outMatrices[parent] * local;
		}
	}

	void BlendPoses(const Pose& a, const Pose& b, const float w, Pose& outResult)
	{
		// A blend between poses of different lengths has no meaning (the bone indices would not line up), so
		// fail loudly rather than silently blending a prefix.
		SS_CORE_ASSERT(a.BoneTransforms.size() == b.BoneTransforms.size(),
		               "BlendPoses: poses come from different skeletons ({} vs {} bones)",
		               a.BoneTransforms.size(), b.BoneTransforms.size());

		const size_t boneCount = std::min(a.BoneTransforms.size(), b.BoneTransforms.size());
		outResult.BoneTransforms.resize(boneCount);
		for (size_t i = 0; i < boneCount; ++i)
		{
			const BoneTransform& from = a.BoneTransforms[i];
			const BoneTransform& to = b.BoneTransforms[i];
			BoneTransform& out = outResult.BoneTransforms[i];
			out.Translation = glm::mix(from.Translation, to.Translation, w);
			out.Scale = glm::mix(from.Scale, to.Scale, w);
			// Shortest arc: q and -q are the SAME rotation, but slerp between an unflipped pair travels the
			// long way round (a limb spinning 350 degrees instead of 10). Flipping the sign when the pair
			// points away from each other is what every engine's blend does, and it is why this cannot be a
			// component-wise lerp.
			glm::quat toRotation = to.Rotation;
			if (glm::dot(from.Rotation, toRotation) < 0.0f)
			{
				toRotation = -toRotation;
			}
			out.Rotation = glm::normalize(glm::slerp(from.Rotation, toRotation, w));
		}

		// The blended pose is not "at" either clip's time; report the time it is being driven towards, which
		// is what a diagnostic readout (and later, root motion) cares about.
		outResult.TimePos = glm::mix(a.TimePos, b.TimePos, w);
	}

	void ComputeSkinningMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices)
	{
		ComputeModelSpaceTransforms(skeleton, pose, outMatrices);
		for (uint32_t bone = 0; bone < skeleton.GetBoneCount(); ++bone)
		{
			outMatrices[bone] = outMatrices[bone] * skeleton.GetInverseBindMatrix(bone);
		}
	}

	MeshBounds ComputeSkinnedBounds(const MeshBounds& bindPose, const std::vector<glm::mat4>& skinningMatrices)
	{
		if (skinningMatrices.empty())
		{
			return bindPose;
		}

		AABB box{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
		for (const glm::mat4& matrix : skinningMatrices)
		{
			const AABB posed = TransformAABB(bindPose.Box, matrix);
			box.Min = glm::min(box.Min, posed.Min);
			box.Max = glm::max(box.Max, posed.Max);
		}

		MeshBounds out;
		out.Box = box;
		out.Sphere.Center = box.Center();
		out.Sphere.Radius = glm::length(box.Extents());
		return out;
	}
}
