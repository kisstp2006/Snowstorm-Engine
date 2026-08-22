#include "SkinnedMeshImporter.hpp"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <unordered_set>

namespace Snowstorm
{
	namespace
	{
		glm::mat4 ToGlm(const aiMatrix4x4& m)
		{
			// assimp is row-major, glm is column-major: this is a transpose, not a memcpy. Getting it wrong
			// produces a model that is inside-out and rotated, which is exactly the kind of bug the fixture
			// test exists to catch.
			return glm::mat4{
			    m.a1, m.b1, m.c1, m.d1,
			    m.a2, m.b2, m.c2, m.d2,
			    m.a3, m.b3, m.c3, m.d3,
			    m.a4, m.b4, m.c4, m.d4};
		}

		BoneTransform DecomposeNode(const aiMatrix4x4& m)
		{
			aiVector3D translation, scale;
			aiQuaternion rotation;
			m.Decompose(scale, rotation, translation);

			BoneTransform out;
			out.Translation = {translation.x, translation.y, translation.z};
			out.Rotation = {rotation.w, rotation.x, rotation.y, rotation.z};
			out.Scale = {scale.x, scale.y, scale.z};
			return out;
		}

		Vertex ReadVertex(const aiMesh* mesh, const uint32_t index)
		{
			Vertex vertex;
			vertex.Position = {mesh->mVertices[index].x, mesh->mVertices[index].y, mesh->mVertices[index].z};
			vertex.Normal = mesh->HasNormals()
			                    ? glm::vec3{mesh->mNormals[index].x, mesh->mNormals[index].y, mesh->mNormals[index].z}
			                    : glm::vec3{0.0f, 1.0f, 0.0f};
			vertex.TexCoord = mesh->HasTextureCoords(0)
			                      ? glm::vec2{mesh->mTextureCoords[0][index].x, 1.0f - mesh->mTextureCoords[0][index].y}
			                      : glm::vec2{vertex.Position.x, vertex.Position.z};
			if (mesh->HasTangentsAndBitangents())
			{
				const glm::vec3 tangent{mesh->mTangents[index].x, mesh->mTangents[index].y, mesh->mTangents[index].z};
				const glm::vec3 bitangent{mesh->mBitangents[index].x, mesh->mBitangents[index].y, mesh->mBitangents[index].z};
				const float handedness = glm::dot(glm::cross(vertex.Normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = glm::vec4(tangent, handedness);
			}
			return vertex;
		}

		// Every node that is a bone, plus every ancestor of one: a chain of un-skinned nodes between two
		// bones still carries transform, so dropping it would detach the lower half of the skeleton.
		void MarkBoneNodes(const aiNode* node, const std::unordered_set<std::string>& boneNames,
		                   std::unordered_set<const aiNode*>& outMarked)
		{
			for (uint32_t i = 0; i < node->mNumChildren; ++i)
			{
				MarkBoneNodes(node->mChildren[i], boneNames, outMarked);
			}
			const bool isBone = boneNames.contains(node->mName.C_Str());
			const bool hasMarkedChild = std::any_of(node->mChildren, node->mChildren + node->mNumChildren,
			                                        [&](const aiNode* child) { return outMarked.contains(child); });
			if (isBone || hasMarkedChild)
			{
				outMarked.insert(node);
			}
		}

		// Depth-first so a parent is always added before its children -- the invariant Skeleton relies on.
		void AddBonesDepthFirst(const aiNode* node, const uint32_t parentIndex,
		                        const std::unordered_set<const aiNode*>& marked, Skeleton& outSkeleton)
		{
			uint32_t index = parentIndex;
			if (marked.contains(node))
			{
				index = outSkeleton.AddBone(node->mName.C_Str(), parentIndex, DecomposeNode(node->mTransformation));
			}
			for (uint32_t i = 0; i < node->mNumChildren; ++i)
			{
				AddBonesDepthFirst(node->mChildren[i], index, marked, outSkeleton);
			}
		}

		// Keep the four strongest influences per vertex and renormalize. assimp's LimitBoneWeights already
		// trims to four, but a file can still carry weights that don't sum to 1 (or none at all).
		void AddInfluence(SkinnedVertexWeights& weights, const uint32_t boneIndex, const float weight)
		{
			uint32_t weakest = 0;
			for (uint32_t i = 1; i < 4; ++i)
			{
				if (weights.BoneWeights[i] < weights.BoneWeights[weakest])
				{
					weakest = i;
				}
			}
			if (weight > weights.BoneWeights[weakest])
			{
				weights.BoneIndices[weakest] = boneIndex;
				weights.BoneWeights[weakest] = weight;
			}
		}

		void NormalizeWeights(SkinnedVertexWeights& weights)
		{
			const float total = weights.BoneWeights.x + weights.BoneWeights.y + weights.BoneWeights.z + weights.BoneWeights.w;
			if (total > 1e-6f)
			{
				weights.BoneWeights /= total;
			}
			else
			{
				// Unskinned vertex: bind it rigidly to bone 0 rather than leaving it at the origin, which is
				// what a zero weight sum would do once the shader multiplies through.
				weights.BoneIndices = glm::uvec4(0u);
				weights.BoneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
			}
		}
	}

	std::optional<SkinnedModel> ImportSkinnedModel(const std::filesystem::path& path, std::string& outError)
	{
		outError.clear();

		Assimp::Importer importer;
		// No aiProcess_PreTransformVertices (it would delete the node graph and the animations) and no
		// aiProcess_JoinIdenticalVertices either: welding vertices merges their bone influences too, which
		// is a silent change to the skin the file authored. LimitBoneWeights trims to the four influences
		// the GPU layout carries; PopulateArmatureData fills in aiBone::mNode so bones resolve without a
		// name search.
		const aiScene* scene = importer.ReadFile(
		    path.string(), aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
		                       aiProcess_LimitBoneWeights | aiProcess_PopulateArmatureData);
		if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->mRootNode)
		{
			outError = importer.GetErrorString();
			return std::nullopt;
		}

		// Which nodes are bones? Every mesh's bone list, unioned.
		std::unordered_set<std::string> boneNames;
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				boneNames.insert(mesh->mBones[boneIndex]->mName.C_Str());
			}
		}
		if (boneNames.empty())
		{
			outError = "no skinned mesh in '" + path.string() + "' (no bones on any mesh)";
			return std::nullopt;
		}

		SkinnedModel model;
		{
			std::unordered_set<const aiNode*> marked;
			MarkBoneNodes(scene->mRootNode, boneNames, marked);
			// Start below the scene root: the root node's own transform belongs to the MODEL (it is the
			// exporter's up-axis/unit conversion), not to the skeleton, and folding it into bone 0 would
			// apply it twice once the entity transform is taken into account.
			for (uint32_t i = 0; i < scene->mRootNode->mNumChildren; ++i)
			{
				AddBonesDepthFirst(scene->mRootNode->mChildren[i], Skeleton::NullIndex, marked, model.Bones);
			}
			model.Bones.Finalize();
		}

		// Inverse bind matrices come from the FILE (aiBone::mOffsetMatrix), not from the rest pose: they
		// only agree when the skin was authored in the rest pose.
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh->mBones[boneIndex];
				const uint32_t skeletonIndex = model.Bones.FindBoneIndex(bone->mName.C_Str());
				if (skeletonIndex != Skeleton::NullIndex)
				{
					model.Bones.SetInverseBindMatrix(skeletonIndex, ToGlm(bone->mOffsetMatrix));
				}
			}
		}

		// Geometry + skin, one submesh per aiMesh (same shape the static path produces).
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (mesh->mNumBones == 0)
			{
				continue; // a static mesh riding along in the same file: not ours to skin
			}

			SkinnedSubmesh submesh;
			submesh.Name = mesh->mName.C_Str();
			submesh.Mesh.Vertices.reserve(mesh->mNumVertices);
			for (uint32_t v = 0; v < mesh->mNumVertices; ++v)
			{
				submesh.Mesh.Vertices.push_back(ReadVertex(mesh, v));
			}
			for (uint32_t f = 0; f < mesh->mNumFaces; ++f)
			{
				const aiFace& face = mesh->mFaces[f];
				for (uint32_t i = 0; i < face.mNumIndices; ++i)
				{
					submesh.Mesh.Indices.push_back(face.mIndices[i]);
				}
			}

			submesh.Skin.assign(mesh->mNumVertices, SkinnedVertexWeights{});
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh->mBones[boneIndex];
				const uint32_t skeletonIndex = model.Bones.FindBoneIndex(bone->mName.C_Str());
				if (skeletonIndex == Skeleton::NullIndex)
				{
					continue;
				}
				for (uint32_t w = 0; w < bone->mNumWeights; ++w)
				{
					const aiVertexWeight& weight = bone->mWeights[w];
					if (weight.mVertexId < submesh.Skin.size())
					{
						AddInfluence(submesh.Skin[weight.mVertexId], skeletonIndex, weight.mWeight);
					}
				}
			}
			for (SkinnedVertexWeights& weights : submesh.Skin)
			{
				NormalizeWeights(weights);
			}
			model.Submeshes.push_back(std::move(submesh));
		}

		// Clips. assimp reports times in "ticks"; the clip stores seconds, so playback never has to know
		// what a tick was. A file that omits the rate (0) means "ticks are frames" -- 25 fps is assimp's
		// own documented assumption.
		for (uint32_t animIndex = 0; animIndex < scene->mNumAnimations; ++animIndex)
		{
			const aiAnimation* animation = scene->mAnimations[animIndex];
			const double ticksPerSecond = animation->mTicksPerSecond != 0.0 ? animation->mTicksPerSecond : 25.0;
			const auto toSeconds = [ticksPerSecond](const double ticks) { return static_cast<float>(ticks / ticksPerSecond); };

			AnimationClip clip(animation->mName.length > 0 ? animation->mName.C_Str()
			                                               : "Animation" + std::to_string(animIndex));
			clip.SetDuration(toSeconds(animation->mDuration));

			for (uint32_t channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
			{
				const aiNodeAnim* channel = animation->mChannels[channelIndex];
				BoneTrack& track = clip.GetTrack(clip.AddTrack(channel->mNodeName.C_Str()));

				track.TranslationTimes.reserve(channel->mNumPositionKeys);
				track.TranslationKeys.reserve(channel->mNumPositionKeys);
				for (uint32_t k = 0; k < channel->mNumPositionKeys; ++k)
				{
					const aiVectorKey& key = channel->mPositionKeys[k];
					track.TranslationTimes.push_back(toSeconds(key.mTime));
					track.TranslationKeys.emplace_back(key.mValue.x, key.mValue.y, key.mValue.z);
				}
				track.RotationTimes.reserve(channel->mNumRotationKeys);
				track.RotationKeys.reserve(channel->mNumRotationKeys);
				for (uint32_t k = 0; k < channel->mNumRotationKeys; ++k)
				{
					const aiQuatKey& key = channel->mRotationKeys[k];
					track.RotationTimes.push_back(toSeconds(key.mTime));
					track.RotationKeys.emplace_back(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z);
				}
				track.ScaleTimes.reserve(channel->mNumScalingKeys);
				track.ScaleKeys.reserve(channel->mNumScalingKeys);
				for (uint32_t k = 0; k < channel->mNumScalingKeys; ++k)
				{
					const aiVectorKey& key = channel->mScalingKeys[k];
					track.ScaleTimes.push_back(toSeconds(key.mTime));
					track.ScaleKeys.emplace_back(key.mValue.x, key.mValue.y, key.mValue.z);
				}
			}
			model.Clips.push_back(std::move(clip));
		}

		if (model.Submeshes.empty())
		{
			outError = "'" + path.string() + "' has bones but no skinned geometry";
			return std::nullopt;
		}
		return model;
	}

	std::vector<std::string> EnumerateSkinnedSubAssetParts(const std::filesystem::path& path)
	{
		// Enumerating means a full parse, and this runs on every model import -- so skip the formats that
		// provably cannot carry a skin. OBJ is the only one in this pipeline: it has no notion of bones or
		// animation at all, so parsing one to discover none is pure waste.
		std::string extension = path.extension().string();
		std::ranges::transform(extension, extension.begin(),
		                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension == ".obj")
		{
			return {};
		}

		std::string error;
		const std::optional<SkinnedModel> model = ImportSkinnedModel(path, error);
		if (!model)
		{
			return {}; // static model (or unreadable): it contributes no skeleton and no clips
		}

		std::vector<std::string> parts;
		parts.reserve(model->Clips.size() + 1);
		parts.emplace_back("skeleton");
		for (const AnimationClip& clip : model->Clips)
		{
			parts.push_back("animation=" + clip.GetName());
		}
		return parts;
	}
}
