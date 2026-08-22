#include "AssetRegistry.hpp"

#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <unordered_set>

namespace Snowstorm
{
	using json = nlohmann::json;

	namespace
	{
		std::filesystem::path NormalizePath(const std::filesystem::path& p)
		{
			return p.lexically_normal();
		}

		// Key used to decide whether two paths refer to the same asset. The filesystem is
		// case-insensitive on Windows (assets/Meshes/x.obj == assets/meshes/x.obj), so compare
		// lower-cased generic strings — otherwise the same file gets two handles and shows up
		// twice in the editor. The stored Path keeps its original casing for display.
		std::string PathKey(const std::filesystem::path& p)
		{
			std::string s = NormalizePath(p).generic_string();
			std::ranges::transform(s, s.begin(), [](const unsigned char c)
			                       { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// "file.obj?submesh=3" -> "submesh=3"; "" for a plain source.
		std::string PartOf(const std::filesystem::path& assetPath)
		{
			const std::string s = assetPath.generic_string();
			const size_t q = s.find('?');
			return q == std::string::npos ? std::string{} : s.substr(q + 1);
		}
	}

	std::filesystem::path AssetRegistry::SourcePathOf(const std::filesystem::path& assetPath)
	{
		const std::string s = assetPath.generic_string();
		const size_t q = s.find('?');
		return NormalizePath(q == std::string::npos ? s : s.substr(0, q));
	}

	std::string AssetRegistry::SourceKeyOf(const std::filesystem::path& assetPath)
	{
		return PathKey(SourcePathOf(assetPath));
	}

	std::filesystem::path AssetRegistry::Resolve(const std::filesystem::path& assetPath) const
	{
		const std::filesystem::path src = SourcePathOf(assetPath);
		return src.is_absolute() ? src : m_ProjectDir / src;
	}

	// ---------------------------------------------------------------------------------------------------
	// Cache file
	// ---------------------------------------------------------------------------------------------------

	bool AssetRegistry::LoadFromFile(const std::filesystem::path& filePath)
	{
		m_Metadata.clear();
		m_Sources.clear();

		std::ifstream in(filePath);
		if (!in.is_open())
			return false;

		json root;
		try
		{
			in >> root;
		}
		catch (const json::exception&)
		{
			return false;
		}
		if (!root.contains("Assets") || !root["Assets"].is_array())
			return false;

		for (const auto& a : root["Assets"])
		{
			const std::string handleStr = a.value("Handle", "0");
			const std::string typeStr = a.value("Type", "None");
			const std::string pathStr = a.value("Path", "");
			if (handleStr == "0" || pathStr.empty())
			{
				continue;
			}
			AssetMetadata m{};
			m.Handle = UUID::FromString(handleStr);
			m.Type = AssetTypeFromString(typeStr);
			m.Path = NormalizePath(pathStr);
			if (m.Type == AssetType::None || m.Handle == 0)
			{
				continue;
			}
			m_Metadata[m.Handle] = std::move(m);
		}
		if (root.contains("Sources") && root["Sources"].is_array())
		{
			for (const auto& s : root["Sources"])
			{
				const std::string pathStr = s.value("Path", "");
				if (pathStr.empty())
				{
					continue;
				}
				SourceInfo& info = m_Sources[PathKey(pathStr)];
				info.ContentHash = s.value("ContentHash", 0ull);
				info.Size = s.value("Size", 0ull);
				info.WriteTime = s.value("WriteTime", 0ull);
			}
		}
		return true;
	}

	bool AssetRegistry::SaveToFile(const std::filesystem::path& filePath) const
	{
		json root;
		root["Assets"] = json::array();
		// Deterministic order (the file is committed): by path.
		std::vector<const AssetMetadata*> rows;
		for (const auto& m : m_Metadata | std::views::values)
		{
			rows.push_back(&m);
		}
		std::ranges::sort(rows, [](const AssetMetadata* a, const AssetMetadata* b)
		                  { return a->Path.generic_string() < b->Path.generic_string(); });
		for (const AssetMetadata* m : rows)
		{
			json a;
			a["Handle"] = m->Handle.ToString();
			a["Type"] = AssetTypeToString(m->Type);
			a["Path"] = m->Path.generic_string();
			root["Assets"].push_back(std::move(a));
		}
		// Per-source freshness (machine-local stat cache; harmless if stale — a mismatch just rehashes).
		root["Sources"] = json::array();
		std::vector<std::pair<std::string, const SourceInfo*>> sources;
		for (const auto& [key, info] : m_Sources)
		{
			sources.emplace_back(key, &info);
		}
		std::ranges::sort(sources, [](const auto& a, const auto& b)
		                  { return a.first < b.first; });
		for (const auto& [key, info] : sources)
		{
			json s;
			s["Path"] = key;
			s["ContentHash"] = info->ContentHash;
			s["Size"] = info->Size;
			s["WriteTime"] = info->WriteTime;
			root["Sources"].push_back(std::move(s));
		}

		std::ofstream out(filePath);
		if (!out.is_open())
			return false;
		out << root.dump(2);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	// Freshness
	// ---------------------------------------------------------------------------------------------------

	AssetRegistry::SourceInfo& AssetRegistry::RefreshSource(const std::filesystem::path& sourcePath, const bool force)
	{
		SourceInfo& info = m_Sources[PathKey(sourcePath)];
		const std::filesystem::path full = Resolve(sourcePath);
		std::error_code ec;
		const uint64_t size = std::filesystem::file_size(full, ec);
		const uint64_t writeTime = GetFileWriteTimeU64(full);
		// Unity-style fast path: unchanged size + mtime -> trust the cached hash; otherwise hash the bytes.
		if (force || info.ContentHash == 0 || size != info.Size || writeTime != info.WriteTime)
		{
			info.ContentHash = HashFileContents(full);
			info.Size = size;
			info.WriteTime = writeTime;
		}
		return info;
	}

	void AssetRegistry::ApplySourceKeys(const std::filesystem::path& sourcePath)
	{
		const std::string key = PathKey(sourcePath);
		const auto it = m_Sources.find(key);
		if (it == m_Sources.end())
		{
			return;
		}
		for (auto& m : m_Metadata | std::views::values)
		{
			if (SourceKeyOf(m.Path) == key)
			{
				m.SourceKey = it->second.ContentHash ^ HashImportSettings(it->second.Import, m.Type);
			}
		}
	}

	bool AssetRegistry::Refresh(const std::filesystem::path& sourcePath)
	{
		const std::string key = PathKey(SourcePathOf(sourcePath));
		const uint64_t before = m_Sources.contains(key) ? m_Sources[key].ContentHash : 0;
		RefreshSource(SourcePathOf(sourcePath), /*force*/ false);
		ApplySourceKeys(SourcePathOf(sourcePath));
		return m_Sources[key].ContentHash != before;
	}

	uint64_t AssetRegistry::SourceKey(const AssetHandle handle) const
	{
		const auto it = m_Metadata.find(handle);
		return it == m_Metadata.end() ? 0 : it->second.SourceKey;
	}

	ImportSettings AssetRegistry::GetImportSettings(const AssetHandle handle) const
	{
		const auto it = m_Metadata.find(handle);
		if (it == m_Metadata.end())
		{
			return {};
		}
		const auto src = m_Sources.find(SourceKeyOf(it->second.Path));
		return src == m_Sources.end() ? ImportSettings{} : src->second.Import;
	}

	bool AssetRegistry::SetImportSettings(const AssetHandle handle, const ImportSettings& settings)
	{
		const auto it = m_Metadata.find(handle);
		if (it == m_Metadata.end())
		{
			return false;
		}
		const std::filesystem::path source = SourcePathOf(it->second.Path);
		std::optional<AssetMeta> meta = AssetMetaIO::Load(Resolve(source));
		if (!meta)
		{
			return false;
		}
		meta->Import = settings;
		if (!AssetMetaIO::Save(Resolve(source), *meta))
		{
			return false;
		}
		m_Sources[PathKey(source)].Import = settings;
		ApplySourceKeys(source);
		return true;
	}

	// ---------------------------------------------------------------------------------------------------
	// Import / scan
	// ---------------------------------------------------------------------------------------------------

	AssetHandle AssetRegistry::Import(const std::filesystem::path& assetPath, const AssetType type)
	{
		const std::filesystem::path source = SourcePathOf(assetPath);
		const std::string part = PartOf(assetPath);
		const std::filesystem::path full = Resolve(source);
		const AssetHandle existing = FindHandleByPath(assetPath, type);

		// The .meta owns the GUID: reuse it when the sidecar exists (a moved/renamed file keeps its
		// identity), create it otherwise — adopting the handle an older, meta-less registry already
		// had for this path, so existing scene references survive the migration. A sub-asset
		// ("?submesh=N") gets its GUID from the meta's SubAssets map.
		std::optional<AssetMeta> meta = AssetMetaIO::Load(full);
		bool metaDirty = false;
		if (!meta)
		{
			meta = AssetMeta{};
			meta->Guid = (part.empty() && existing.Value() != 0) ? existing : AssetHandle{};
			meta->Type = type;
			metaDirty = true;
		}
		AssetHandle handle = meta->Guid;
		if (!part.empty())
		{
			auto sub = meta->SubAssets.find(part);
			if (sub == meta->SubAssets.end())
			{
				sub = meta->SubAssets.emplace(part, existing.Value() != 0 ? existing : AssetHandle{}).first;
				metaDirty = true;
			}
			handle = sub->second;
		}
		if (metaDirty)
		{
			std::error_code ec;
			if (std::filesystem::exists(full, ec) && !AssetMetaIO::Save(full, *meta))
			{
				SS_CORE_WARN("AssetRegistry: could not write '{}'.", AssetMetaIO::MetaPathFor(full).string());
			}
		}
		if (existing.Value() != 0)
		{
			if (existing != handle)
			{
				SS_CORE_WARN("AssetRegistry: '{}' is registered as {} but its .meta says {}; the .meta wins.",
				             assetPath.string(), existing.ToString(), handle.ToString());
				m_Metadata.erase(existing);
			}
			else
			{
				return existing; // already registered and the sidecar agrees
			}
		}

		// A handle may already be registered under this GUID (e.g. the file row of a model whose part is
		// being imported now, or a meta adopted from another path): keep one row per handle.
		if (const auto dup = m_Metadata.find(handle); dup != m_Metadata.end() && PathKey(dup->second.Path) != PathKey(assetPath))
		{
			SS_CORE_WARN("AssetRegistry: '{}' and '{}' share GUID {}; keeping the first.", dup->second.Path.string(), assetPath.string(), handle.ToString());
			return dup->second.Handle;
		}

		AssetMetadata m{};
		m.Handle = handle;
		m.Type = type;
		m.Path = NormalizePath(assetPath);
		m_Metadata[m.Handle] = std::move(m);

		SourceInfo& info = RefreshSource(source, /*force*/ false);
		info.Import = meta->Import;
		ApplySourceKeys(source);
		return handle;
	}

	std::vector<AssetRegistry::ScannedFile> AssetRegistry::Scan(const std::filesystem::path& assetDir, bool& changed)
	{
		changed = false;
		std::vector<ScannedFile> found;
		std::error_code ec;
		if (!std::filesystem::exists(assetDir, ec))
		{
			return found;
		}

		// Snapshot the pre-scan source keys so we can tell whether the scan changed anything worth saving.
		std::unordered_map<UUID, uint64_t> keysBefore;
		for (const auto& [h, m] : m_Metadata)
		{
			keysBefore[h] = m.SourceKey;
		}
		const size_t rowsBefore = m_Metadata.size();

		std::unordered_set<std::string> seenSources;
		for (auto it = std::filesystem::recursive_directory_iterator(assetDir, ec);
		     it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec)
			{
				continue;
			}
			// Cooked artifacts live under cache/ (gitignored): never sources.
			if (it->is_directory(ec) && it->path().filename() == "cache")
			{
				it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(ec))
			{
				continue;
			}
			const AssetType type = AssetTypeFromExtension(it->path().extension().string());
			if (type == AssetType::None)
			{
				continue;
			}
			const std::filesystem::path rel = NormalizePath(it->path().lexically_relative(m_ProjectDir));
			if (type == AssetType::Scene)
			{
				found.push_back({rel, type, AssetHandle{0}});
				continue;
			}

			seenSources.insert(PathKey(rel));

			// File row (adopts the meta's GUID; creates the meta when missing) ...
			const AssetHandle handle = Import(rel, type);
			found.push_back({rel, type, handle});

			// ... sub-asset rows an older registry already had for this source ("?submesh=N") go through
			// Import too, so the meta adopts their handles (a fresh clone then rebuilds them from the meta).
			std::vector<std::pair<std::filesystem::path, AssetType>> parts;
			for (const auto& m : m_Metadata | std::views::values)
			{
				if (!PartOf(m.Path).empty() && SourceKeyOf(m.Path) == PathKey(rel))
				{
					parts.emplace_back(m.Path, m.Type);
				}
			}
			for (const auto& [partPath, partType] : parts)
			{
				Import(partPath, partType);
			}

			// ... plus every sub-asset the meta knows about (a model's submeshes), so a fresh clone gets
			// the same handles its scenes reference.
			if (const auto meta = AssetMetaIO::Load(it->path()); meta && !meta->SubAssets.empty())
			{
				for (const auto& [part, guid] : meta->SubAssets)
				{
					std::filesystem::path partPath = rel;
					partPath += "?" + part;
					if (FindHandleByPath(partPath, type).Value() == 0)
					{
						AssetMetadata m{};
						m.Handle = guid;
						m.Type = type;
						m.Path = partPath;
						m_Metadata[guid] = std::move(m);
					}
				}
				ApplySourceKeys(rel);
			}
			else
			{
				RefreshSource(rel, /*force*/ false);
				ApplySourceKeys(rel);
			}
		}

		// Rows whose source vanished: drop them (a scene that still references the GUID logs an
		// unresolved-handle warning at load, which is the right place to notice).
		for (auto it = m_Metadata.begin(); it != m_Metadata.end();)
		{
			if (!seenSources.contains(SourceKeyOf(it->second.Path)))
			{
				SS_CORE_WARN("AssetRegistry: source '{}' is gone; dropping handle {}.", it->second.Path.string(), it->second.Handle.ToString());
				it = m_Metadata.erase(it);
				changed = true;
			}
			else
			{
				++it;
			}
		}

		if (m_Metadata.size() != rowsBefore)
		{
			changed = true;
		}
		for (const auto& [h, m] : m_Metadata)
		{
			const auto before = keysBefore.find(h);
			if (before == keysBefore.end() || before->second != m.SourceKey)
			{
				changed = true;
				break;
			}
		}
		return found;
	}

	// ---------------------------------------------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------------------------------------------

	AssetHandle AssetRegistry::FindHandleByPath(const std::filesystem::path& assetPath, const AssetType type) const
	{
		const std::string key = PathKey(assetPath);
		for (const auto& m : m_Metadata | std::views::values)
		{
			if (m.Type == type && PathKey(m.Path) == key)
			{
				return m.Handle;
			}
		}
		return AssetHandle{0};
	}

	std::vector<AssetHandle> AssetRegistry::HandlesForSource(const std::filesystem::path& sourcePath) const
	{
		const std::string key = SourceKeyOf(sourcePath);
		std::vector<AssetHandle> out;
		for (const auto& m : m_Metadata | std::views::values)
		{
			if (SourceKeyOf(m.Path) == key)
			{
				out.push_back(m.Handle);
			}
		}
		return out;
	}

	void AssetRegistry::Iterate(const std::function<void(const AssetMetadata&)>& fn) const
	{
		for (const auto& m : m_Metadata | std::views::values)
		{
			fn(m);
		}
	}

	const AssetMetadata* AssetRegistry::GetMetadata(const AssetHandle handle) const
	{
		const auto it = m_Metadata.find(handle);
		if (it == m_Metadata.end())
		{
			return nullptr;
		}
		return &it->second;
	}
}
