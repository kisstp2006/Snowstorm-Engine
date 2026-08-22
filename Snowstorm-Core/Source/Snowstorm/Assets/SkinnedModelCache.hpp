#pragma once

#include "Snowstorm/Animation/SkinnedMeshImporter.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

#include <filesystem>
#include <optional>

namespace Snowstorm
{
	// Cooked skinned-model cache (Engine/cache/animation/<handle>.ssanim), the animation twin of the mesh
	// and texture cook caches: parsing a character out of an FBX/glTF costs an assimp import of the whole
	// file, and the result -- skeleton, clips and skinned geometry -- is exactly what a blob can hold.
	//
	// ONE blob per source model, not per sub-asset: the skeleton, every clip and every skinned submesh come
	// out of a single parse, so splitting them would mean re-parsing the file once per part.
	//
	// Freshness is the usual SourceKey (content hash ^ import-settings hash): editing the model OR its
	// import settings misses. The format version guards our own layout changes -- unlike the physics shape
	// cache, nothing here is a third party's binary format, so there is no library version to stamp.
	namespace SkinnedModelCache
	{
		[[nodiscard]] std::filesystem::path GetCachePath(AssetHandle sourceHandle);

		// Null on any miss: no file, foreign/older format, a stale source key, or a truncated blob.
		[[nodiscard]] std::optional<SkinnedModel> Load(AssetHandle sourceHandle, uint64_t sourceKey);

		// Best-effort: a failed write is logged and otherwise ignored (the model is already in memory, and
		// the next run simply re-imports).
		bool Save(AssetHandle sourceHandle, uint64_t sourceKey, const SkinnedModel& model);
	}
}
