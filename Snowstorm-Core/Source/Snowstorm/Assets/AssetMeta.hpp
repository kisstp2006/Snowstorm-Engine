#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace Snowstorm
{
	// Per-asset import settings, authored in the .meta sidecar and reflected (RTTR) so the editor can show
	// them generically. Only settings the pipeline actually consumes live here — add a field when a cook
	// step reads it, not before.
	struct TextureImportSettings
	{
		bool GenerateMips = true; // cook the full mip chain (off for UI/LUT textures sampled at 1:1)
		// BC7 block compression at cook time: a quarter of the VRAM and bandwidth for a difference that is
		// hard to see on albedo. On by default, the way Unity and Unreal ship. Measured on Sponza (69
		// textures): a cold cook costs 11 s against 7 s uncompressed, a warm load is 10 s against 11 s (less
		// to read), and the cache drops from 363 MB to 91 MB. Turn it off per texture where block artifacts
		// do show -- a normal map at grazing angles, or a LUT that has to be exact.
		bool Compress = true;
	};

	struct MeshImportSettings
	{
		bool GenerateCollision = false; // cook a physics shape alongside the render mesh (physics module)
	};

	struct ImportSettings
	{
		TextureImportSettings Texture;
		MeshImportSettings Mesh;
	};

	bool operator==(const TextureImportSettings& a, const TextureImportSettings& b);
	bool operator==(const MeshImportSettings& a, const MeshImportSettings& b);
	bool operator==(const ImportSettings& a, const ImportSettings& b);

	// The `<source>.meta` sidecar (Unity: foo.png.meta): owns the asset's stable GUID and its import
	// settings; committed next to the source file, so a move/rename keeps every scene reference intact.
	// A multi-part source (a model with N submeshes) lists its parts' GUIDs under SubAssets, keyed by the
	// part suffix the registry path uses ("submesh=0"), cf. Unity's fileID per sub-asset.
	struct AssetMeta
	{
		AssetHandle Guid{0};
		AssetType Type = AssetType::None;
		ImportSettings Import;
		std::map<std::string, AssetHandle> SubAssets;
	};

	class AssetMetaIO
	{
	public:
		static std::filesystem::path MetaPathFor(const std::filesystem::path& sourcePath);
		static std::optional<AssetMeta> Load(const std::filesystem::path& sourcePath);
		// Atomic write (temp + rename). False on I/O failure.
		static bool Save(const std::filesystem::path& sourcePath, const AssetMeta& meta);
	};

	// Source file extension -> asset type (any case, with the dot). None for anything the pipeline
	// doesn't import. Scenes are reported as Scene but never get a meta/handle (opened by path).
	AssetType AssetTypeFromExtension(std::string ext);

	// FNV-1a over the file bytes (0 on read failure). The cook-cache freshness key is
	// ContentHash ^ ImportSettingsHash so a changed source OR a changed import setting re-cooks.
	uint64_t HashFileContents(const std::filesystem::path& path);
	uint64_t HashImportSettings(const ImportSettings& settings, AssetType type);
}
