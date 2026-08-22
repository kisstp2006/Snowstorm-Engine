#include "AnimationClip.hpp"

#include <algorithm>
#include <cmath>

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

	void ComputeSkinningMatrices(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices)
	{
		ComputeModelSpaceTransforms(skeleton, pose, outMatrices);
		for (uint32_t bone = 0; bone < skeleton.GetBoneCount(); ++bone)
		{
			outMatrices[bone] = outMatrices[bone] * skeleton.GetInverseBindMatrix(bone);
		}
	}
}
