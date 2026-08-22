#pragma once

#include "Snowstorm/Assets/AssetMeta.hpp"
#include "Snowstorm/Assets/AssetTypes.hpp"

#include <functional>
#include <unordered_map>
#include <vector>

namespace Snowstorm
{
	// The asset database (Unity AssetDatabase): handle -> source path + freshness. The `.meta` sidecars
	// own the GUIDs and import settings; this registry is the fast index built from them by Scan() and
	// cached in AssetRegistry.json (so a warm start doesn't re-read every meta). Paths are project-relative
	// generic strings; a multi-part source appends "?submesh=N" (its GUID lives in the meta's SubAssets).
	class AssetRegistry
	{
	public:
		struct ScannedFile
		{
			std::filesystem::path Path; // project-relative
			AssetType Type = AssetType::None;
			AssetHandle Handle{0}; // 0 for scenes (opened by path, never imported)
		};

		// Project root every relative path resolves against. Set before Import/Scan.
		void SetProjectDirectory(const std::filesystem::path& projectDir) { m_ProjectDir = projectDir; }
		[[nodiscard]] const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDir; }
		[[nodiscard]] std::filesystem::path Resolve(const std::filesystem::path& assetPath) const;

		bool LoadFromFile(const std::filesystem::path& file);
		bool SaveToFile(const std::filesystem::path& file) const;

		// The import step: walk the asset directory, give every importable source a .meta (adopting the
		// handle an older registry had for it), register its parts, refresh freshness (size/mtime fast
		// path, content hash on change), drop rows whose source is gone. Returns every file found,
		// scenes included, for the content browser. `changed` is set when the registry cache should be
		// saved.
		std::vector<ScannedFile> Scan(const std::filesystem::path& assetDir, bool& changed);

		// Register one source (or one "file?submesh=N" part); creates/updates its .meta. Idempotent.
		AssetHandle Import(const std::filesystem::path& assetPath, AssetType type);

		AssetHandle FindHandleByPath(const std::filesystem::path& assetPath, AssetType type) const;
		const AssetMetadata* GetMetadata(AssetHandle handle) const;
		void Iterate(const std::function<void(const AssetMetadata&)>& fn) const;

		// Every handle backed by this source file (the file itself and its sub-assets) — hot reload.
		[[nodiscard]] std::vector<AssetHandle> HandlesForSource(const std::filesystem::path& sourcePath) const;

		// Re-stat one source file; rehashes when size/mtime moved. True if its SourceKey changed.
		bool Refresh(const std::filesystem::path& sourcePath);

		// Cook-cache freshness key (content hash ^ import-settings hash); 0 when unknown.
		[[nodiscard]] uint64_t SourceKey(AssetHandle handle) const;

		[[nodiscard]] ImportSettings GetImportSettings(AssetHandle handle) const;
		// Writes the .meta and refreshes the SourceKey of every part of that source. False on I/O failure.
		bool SetImportSettings(AssetHandle handle, const ImportSettings& settings);

	private:
		struct SourceInfo
		{
			ImportSettings Import;
			uint64_t ContentHash = 0;
			uint64_t Size = 0;
			uint64_t WriteTime = 0;
		};

		static std::string SourceKeyOf(const std::filesystem::path& assetPath); // lower-cased source path
		static std::filesystem::path SourcePathOf(const std::filesystem::path& assetPath); // strips ?part
		SourceInfo& RefreshSource(const std::filesystem::path& sourcePath, bool force);
		void ApplySourceKeys(const std::filesystem::path& sourcePath);

		std::filesystem::path m_ProjectDir;
		std::unordered_map<UUID, AssetMetadata> m_Metadata;    // key = handle
		std::unordered_map<std::string, SourceInfo> m_Sources; // key = SourceKeyOf(path)
	};
}
