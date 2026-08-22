#pragma once

#include "Snowstorm/Animation/AnimationClip.hpp"
#include "Snowstorm/Animation/Skeleton.hpp"
#include "Snowstorm/Assets/MeshCache.hpp"

#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Per-vertex skin binding, parallel to CookedMesh::Vertices. Four influences is what every real-time
	// skinning path uses (and what assimp's aiProcess_LimitBoneWeights trims to), so the arrays are fixed
	// width rather than a variable list -- the GPU wants exactly this shape.
	struct SkinnedVertexWeights
	{
		glm::uvec4 BoneIndices{0u};   // indices into the model's Skeleton
		glm::vec4 BoneWeights{0.0f};  // normalized to sum to 1
	};

	struct SkinnedSubmesh
	{
		std::string Name;
		CookedMesh Mesh;                       // the same cooked geometry type the static path produces
		std::vector<SkinnedVertexWeights> Skin; // parallel to Mesh.Vertices
	};

	struct SkinnedModel
	{
		Skeleton Bones;
		std::vector<SkinnedSubmesh> Submeshes;
		std::vector<AnimationClip> Clips;
	};

	// Reads a skinned model (bones, per-vertex weights and animation clips) from any format assimp knows.
	//
	// This is a SEPARATE path from MeshLibrary's static import on purpose: that one runs
	// aiProcess_PreTransformVertices, which -- in assimp's own words -- "removes the node graph" and
	// "animations are removed during this step". Turning that flag off globally would move every existing
	// static mesh's vertices, so skinned models get their own read instead.
	//
	// Returns nullopt when the file cannot be read or contains no skinned mesh (a static model is not an
	// error here, just not this importer's job); `outError` explains which.
	std::optional<SkinnedModel> ImportSkinnedModel(const std::filesystem::path& path, std::string& outError);

	// The sub-asset part keys a skinned source contributes -- "skeleton", and "animation=<clip name>" per
	// clip -- in the form AssetRegistry paths use ("model.gltf?skeleton"). Empty for a static model.
	//
	// Clips are keyed by NAME rather than by index, unlike submeshes: an animation's name survives a
	// re-export, its position in the file does not, and index keys would silently re-point every scene
	// reference the moment someone inserts a clip. Pair each key with AssetRegistry::TypeForPart for its
	// asset type.
	std::vector<std::string> EnumerateSkinnedSubAssetParts(const std::filesystem::path& path);
}
