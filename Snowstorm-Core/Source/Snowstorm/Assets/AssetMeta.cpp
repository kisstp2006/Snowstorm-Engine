#include "AssetMeta.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Utility/JsonUtils.hpp"

#include <nlohmann/json.hpp>
#include <rttr/registration.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		using namespace rttr;
		registration::class_<TextureImportSettings>("Snowstorm::TextureImportSettings")
		    .constructor()(policy::ctor::as_object)
		    .property("GenerateMips", &TextureImportSettings::GenerateMips);
		registration::class_<MeshImportSettings>("Snowstorm::MeshImportSettings")
		    .constructor()(policy::ctor::as_object)
		    .property("GenerateCollision", &MeshImportSettings::GenerateCollision);
		registration::class_<ImportSettings>("Snowstorm::ImportSettings")
		    .constructor()(policy::ctor::as_object)
		    .property("Texture", &ImportSettings::Texture)
		    .property("Mesh", &ImportSettings::Mesh);
	}

	bool operator==(const TextureImportSettings& a, const TextureImportSettings& b)
	{
		return a.GenerateMips == b.GenerateMips;
	}
	bool operator==(const MeshImportSettings& a, const MeshImportSettings& b)
	{
		return a.GenerateCollision == b.GenerateCollision;
	}
	bool operator==(const ImportSettings& a, const ImportSettings& b)
	{
		return a.Texture == b.Texture && a.Mesh == b.Mesh;
	}

	namespace
	{
		constexpr int kMetaVersion = 1;
	}

	std::filesystem::path AssetMetaIO::MetaPathFor(const std::filesystem::path& sourcePath)
	{
		std::filesystem::path p = sourcePath;
		p += ".meta";
		return p;
	}

	std::optional<AssetMeta> AssetMetaIO::Load(const std::filesystem::path& sourcePath)
	{
		std::ifstream in(MetaPathFor(sourcePath));
		if (!in.is_open())
		{
			return std::nullopt;
		}
		nlohmann::json j;
		try
		{
			in >> j;
		}
		catch (const nlohmann::json::exception&)
		{
			SS_CORE_WARN("AssetMeta: '{}' is not valid JSON; ignoring.", MetaPathFor(sourcePath).string());
			return std::nullopt;
		}
		AssetMeta meta;
		meta.Guid = UUID::FromString(j.value("Guid", "0"));
		meta.Type = AssetTypeFromString(j.value("Type", "None"));
		if (meta.Guid.Value() == 0 || meta.Type == AssetType::None)
		{
			return std::nullopt;
		}
		if (j.contains("Import") && j["Import"].is_object())
		{
			rttr::instance inst = meta.Import;
			JsonToRttrInstance(j["Import"], inst);
		}
		if (j.contains("SubAssets") && j["SubAssets"].is_object())
		{
			for (const auto& [key, val] : j["SubAssets"].items())
			{
				if (val.is_string())
				{
					meta.SubAssets[key] = UUID::FromString(val.get<std::string>());
				}
			}
		}
		return meta;
	}

	bool AssetMetaIO::Save(const std::filesystem::path& sourcePath, const AssetMeta& meta)
	{
		nlohmann::json j;
		j["Version"] = kMetaVersion;
		j["Guid"] = meta.Guid.ToString();
		j["Type"] = AssetTypeToString(meta.Type);
		// Only the settings block that applies to this type, so a texture's meta doesn't carry mesh knobs.
		nlohmann::json import = nlohmann::json::object();
		if (meta.Type == AssetType::Texture)
		{
			import["Texture"] = RttrInstanceToJson(rttr::instance(meta.Import.Texture));
		}
		else if (meta.Type == AssetType::Mesh)
		{
			import["Mesh"] = RttrInstanceToJson(rttr::instance(meta.Import.Mesh));
		}
		j["Import"] = std::move(import);
		if (!meta.SubAssets.empty())
		{
			nlohmann::json subs = nlohmann::json::object();
			for (const auto& [key, guid] : meta.SubAssets)
			{
				subs[key] = guid.ToString();
			}
			j["SubAssets"] = std::move(subs);
		}

		const std::filesystem::path metaPath = MetaPathFor(sourcePath);
		std::filesystem::path tmp = metaPath;
		tmp += ".tmp";
		{
			std::ofstream out(tmp);
			if (!out.is_open())
			{
				return false;
			}
			out << j.dump(2) << '\n';
		}
		std::error_code ec;
		std::filesystem::rename(tmp, metaPath, ec);
		if (ec)
		{
			std::filesystem::remove(tmp, ec);
			return false;
		}
		return true;
	}

	AssetType AssetTypeFromExtension(std::string ext)
	{
		std::ranges::transform(ext, ext.begin(), [](const unsigned char c)
		                       { return static_cast<char>(std::tolower(c)); });
		if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb")
			return AssetType::Mesh;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".dds" || ext == ".bmp")
			return AssetType::Texture;
		if (ext == ".ssmat")
			return AssetType::Material;
		if (ext == ".hlsl")
			return AssetType::Shader;
		if (ext == ".world")
			return AssetType::Scene;
		return AssetType::None;
	}

	uint64_t HashFileContents(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return 0;
		}
		uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
		char buf[64 * 1024];
		while (in.read(buf, sizeof(buf)) || in.gcount() > 0)
		{
			const std::streamsize n = in.gcount();
			for (std::streamsize i = 0; i < n; ++i)
			{
				h ^= static_cast<unsigned char>(buf[i]);
				h *= 1099511628211ull;
			}
		}
		return h == 0 ? 1 : h; // 0 is reserved for "unknown"
	}

	uint64_t HashImportSettings(const ImportSettings& s, const AssetType type)
	{
		// Stable, hand-rolled: only the block that applies. Bump the seed when a field's meaning changes.
		uint64_t h = 0x9E3779B97F4A7C15ull;
		auto mix = [&](const uint64_t v)
		{ h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
		if (type == AssetType::Texture)
		{
			mix(s.Texture.GenerateMips ? 1u : 2u);
		}
		else if (type == AssetType::Mesh)
		{
			mix(s.Mesh.GenerateCollision ? 1u : 2u);
		}
		return h;
	}
}
