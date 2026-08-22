#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Snowstorm
{
	// One bone's pose relative to its parent (Hazel's Animation::Transform). Kept as T/R/S rather than a
	// matrix because that is what animation keys interpolate: slerping a rotation is correct, lerping the
	// rows of two rotation matrices is not.
	//
	// Divergence from Hazel, on purpose: Hazel stores a single float scale per bone. glTF/assimp give a
	// vec3, and collapsing it at import would silently distort any model that uses non-uniform bone scale,
	// so we keep the vec3 and pay 8 bytes for it.
	struct BoneTransform
	{
		glm::vec3 Translation{0.0f};
		glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};
		glm::vec3 Scale{1.0f};

		[[nodiscard]] glm::mat4 ToMatrix() const;

		// Compose: `parent * child` puts `child` into the parent's space, matching matrix multiplication
		// order. Non-uniform parent scale combined with child rotation cannot be represented as another
		// T/R/S, so this composes through matrices where that matters -- see the .cpp.
		[[nodiscard]] BoneTransform operator*(const BoneTransform& child) const;
	};

	// A bone hierarchy (Hazel Skeleton): parallel arrays instead of a node tree, because every consumer
	// walks all bones in order anyway, and an index-based parent link is what lets model-space transforms
	// be composed in ONE forward pass.
	//
	// Invariant: a bone's parent index is always SMALLER than its own index (roots use NullIndex). AddBone
	// enforces it, and it is what makes that single pass valid -- a parent is always already resolved.
	class Skeleton
	{
	public:
		static constexpr uint32_t NullIndex = ~0u;

		// Returns the new bone's index. `parentIndex` must be NullIndex (root) or an already-added bone.
		uint32_t AddBone(std::string name, uint32_t parentIndex, const BoneTransform& restPose);

		// Computes the model-space rest pose and its inverse (the "inverse bind matrices" the skinning
		// shader needs). Call once after all bones are added; AddBone invalidates it.
		void Finalize();

		[[nodiscard]] uint32_t GetBoneCount() const { return static_cast<uint32_t>(m_BoneNames.size()); }
		[[nodiscard]] bool IsEmpty() const { return m_BoneNames.empty(); }

		// NullIndex when the name is unknown -- callers importing a skin use this to detect a weight that
		// references a bone the skeleton doesn't have.
		[[nodiscard]] uint32_t FindBoneIndex(std::string_view name) const;

		[[nodiscard]] const std::string& GetBoneName(uint32_t boneIndex) const { return m_BoneNames[boneIndex]; }
		[[nodiscard]] uint32_t GetParentBoneIndex(uint32_t boneIndex) const { return m_ParentBoneIndices[boneIndex]; }
		[[nodiscard]] const BoneTransform& GetRestPose(uint32_t boneIndex) const { return m_RestPose[boneIndex]; }
		[[nodiscard]] const std::vector<BoneTransform>& GetRestPose() const { return m_RestPose; }

		// Model-space rest pose and its inverse. Finalize() must have run; both are empty until then.
		[[nodiscard]] const glm::mat4& GetModelSpaceRestPose(uint32_t boneIndex) const { return m_ModelSpaceRestPose[boneIndex]; }
		[[nodiscard]] const glm::mat4& GetInverseBindMatrix(uint32_t boneIndex) const { return m_InverseBindMatrices[boneIndex]; }

		// An importer that has authoritative inverse-bind matrices from the file (glTF's inverseBindMatrices
		// accessor, assimp's aiBone::mOffsetMatrix) should hand them over rather than let Finalize() derive
		// them from the rest pose: the two only agree when the file's bind pose IS the rest pose, and a model
		// whose skin was authored in a different pose would otherwise be skinned into a broken shape.
		void SetInverseBindMatrix(uint32_t boneIndex, const glm::mat4& inverseBind);

	private:
		std::vector<std::string> m_BoneNames;
		std::vector<uint32_t> m_ParentBoneIndices;
		std::vector<BoneTransform> m_RestPose; // bone-local

		std::vector<glm::mat4> m_ModelSpaceRestPose;
		std::vector<glm::mat4> m_InverseBindMatrices;
	};
}
