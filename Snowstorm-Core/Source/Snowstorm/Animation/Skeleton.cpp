#include "Skeleton.hpp"

#include "Snowstorm/Core/Base.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Snowstorm
{
	glm::mat4 BoneTransform::ToMatrix() const
	{
		return glm::translate(glm::mat4(1.0f), Translation) * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
	}

	BoneTransform BoneTransform::operator*(const BoneTransform& child) const
	{
		// T/R/S composition is exact only while the parent scale is uniform: a non-uniform parent scale
		// applied through a child rotation produces a shear, which no T/R/S triple can represent. Bones
		// are composed into matrices for rendering anyway, so the fast path stays exact and the rare case
		// is simply approximated here rather than silently pretending to be exact -- callers that need the
		// exact result compose ToMatrix() products (which is what the model-space pass below does).
		BoneTransform out;
		out.Translation = Translation + Rotation * (Scale * child.Translation);
		out.Rotation = glm::normalize(Rotation * child.Rotation);
		out.Scale = Scale * child.Scale;
		return out;
	}

	uint32_t Skeleton::AddBone(std::string name, const uint32_t parentIndex, const BoneTransform& restPose)
	{
		const auto index = static_cast<uint32_t>(m_BoneNames.size());
		SS_CORE_ASSERT(parentIndex == NullIndex || parentIndex < index,
		               "Skeleton: a bone's parent must be added before it (parents must precede children)");

		m_BoneNames.push_back(std::move(name));
		m_ParentBoneIndices.push_back(parentIndex);
		m_RestPose.push_back(restPose);

		// Derived data is stale now; Finalize() rebuilds it.
		m_ModelSpaceRestPose.clear();
		m_InverseBindMatrices.clear();
		return index;
	}

	void Skeleton::Finalize()
	{
		const uint32_t boneCount = GetBoneCount();
		m_ModelSpaceRestPose.assign(boneCount, glm::mat4(1.0f));
		m_InverseBindMatrices.assign(boneCount, glm::mat4(1.0f));

		// One forward pass: the parent-precedes-child invariant guarantees the parent's model-space matrix
		// is already final when a child reads it.
		for (uint32_t bone = 0; bone < boneCount; ++bone)
		{
			const glm::mat4 local = m_RestPose[bone].ToMatrix();
			const uint32_t parent = m_ParentBoneIndices[bone];
			m_ModelSpaceRestPose[bone] = parent == NullIndex ? local : m_ModelSpaceRestPose[parent] * local;
			m_InverseBindMatrices[bone] = glm::inverse(m_ModelSpaceRestPose[bone]);
		}
	}

	uint32_t Skeleton::FindBoneIndex(const std::string_view name) const
	{
		const auto it = std::ranges::find(m_BoneNames, name);
		return it == m_BoneNames.end() ? NullIndex : static_cast<uint32_t>(std::distance(m_BoneNames.begin(), it));
	}

	void Skeleton::SetInverseBindMatrix(const uint32_t boneIndex, const glm::mat4& inverseBind)
	{
		if (m_InverseBindMatrices.size() != m_BoneNames.size())
		{
			m_InverseBindMatrices.assign(m_BoneNames.size(), glm::mat4(1.0f));
		}
		m_InverseBindMatrices[boneIndex] = inverseBind;
	}
}
