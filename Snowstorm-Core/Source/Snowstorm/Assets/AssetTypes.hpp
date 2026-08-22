#pragma once

#include "Snowstorm/Utility/UUID.hpp"

#include <filesystem>
#include <string>

namespace Snowstorm
{
	using AssetHandle = UUID;

	enum class AssetType : uint8_t
	{
		None = 0,
		Mesh,
		Texture,
		Shader,
		Material,
		Scene,
		// Sub-assets of a skinned model source, never standalone files: a model contributes one Skeleton
		// and one Animation per clip (see AssetRegistry::TypeForPart). Appended at the end -- the type is
		// serialized by NAME, so the numbering is free to grow, but keeping it append-only costs nothing.
		Skeleton,
		Animation
	};

	inline std::string AssetTypeToString(const AssetType t)
	{
		switch (t)
		{
		case AssetType::Mesh:
			return "Mesh";
		case AssetType::Texture:
			return "Texture";
		case AssetType::Shader:
			return "Shader";
		case AssetType::Material:
			return "Material";
		case AssetType::Scene:
			return "Scene";
		case AssetType::Skeleton:
			return "Skeleton";
		case AssetType::Animation:
			return "Animation";
		default:
			return "None";
		}
	}

	inline AssetType AssetTypeFromString(const std::string& s)
	{
		if (s == "Mesh")
			return AssetType::Mesh;
		if (s == "Texture")
			return AssetType::Texture;
		if (s == "Shader")
			return AssetType::Shader;
		if (s == "Material")
			return AssetType::Material;
		if (s == "Scene")
			return AssetType::Scene;
		if (s == "Skeleton")
			return AssetType::Skeleton;
		if (s == "Animation")
			return AssetType::Animation;
		return AssetType::None;
	}

	struct AssetMetadata
	{
		AssetHandle Handle{};
		AssetType Type = AssetType::None;
		std::filesystem::path Path; // project-relative; "file?submesh=N" for a sub-asset of a model
		// Cook-cache freshness key for this asset's source: content hash ^ import-settings hash (see
		// AssetRegistry::SourceKey). 0 = not yet scanned.
		uint64_t SourceKey = 0;
	};
}
