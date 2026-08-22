#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetMeta.hpp"
#include "Snowstorm/Assets/AssetRegistry.hpp"

#include <filesystem>
#include <fstream>
#include <random>

using namespace Snowstorm;

namespace
{
	// A throwaway project directory under the system temp dir, removed on scope exit.
	struct TempProject
	{
		std::filesystem::path Dir;
		TempProject()
		{
			std::random_device rd;
			Dir = std::filesystem::temp_directory_path() / ("ss-assets-" + std::to_string(rd()));
			std::filesystem::create_directories(Dir / "assets" / "textures");
			std::filesystem::create_directories(Dir / "assets" / "cache");
		}
		~TempProject()
		{
			std::error_code ec;
			std::filesystem::remove_all(Dir, ec);
		}
		void Write(const std::filesystem::path& rel, const std::string& bytes) const
		{
			std::ofstream out(Dir / rel, std::ios::binary);
			out << bytes;
		}
	};
}

TEST_CASE("Scan creates .meta sidecars and registry rows; a rescan keeps the handles", "[assets]")
{
	TempProject tp;
	tp.Write("assets/textures/a.png", "not-really-a-png");
	tp.Write("assets/textures/b.jpg", "jpeg-bytes");
	tp.Write("assets/cache/ignored.png", "cooked artifact, never a source");
	tp.Write("assets/scene.world", "{}");

	AssetRegistry reg;
	reg.SetProjectDirectory(tp.Dir);
	bool changed = false;
	const auto found = reg.Scan(tp.Dir / "assets", changed);
	REQUIRE(changed);
	REQUIRE(found.size() == 3); // a.png, b.jpg, scene.world (cache/ skipped)

	REQUIRE(std::filesystem::exists(tp.Dir / "assets/textures/a.png.meta"));
	REQUIRE_FALSE(std::filesystem::exists(tp.Dir / "assets/cache/ignored.png.meta"));
	REQUIRE_FALSE(std::filesystem::exists(tp.Dir / "assets/scene.world.meta")); // scenes are path-opened

	const AssetHandle a = reg.FindHandleByPath("assets/textures/a.png", AssetType::Texture);
	REQUIRE(a.Value() != 0);
	REQUIRE(reg.SourceKey(a) != 0);
	const auto meta = AssetMetaIO::Load(tp.Dir / "assets/textures/a.png");
	REQUIRE(meta);
	REQUIRE(meta->Guid == a);
	REQUIRE(meta->Type == AssetType::Texture);

	// A brand-new registry (fresh clone: no cache file) rebuilds the same handles from the sidecars.
	AssetRegistry fresh;
	fresh.SetProjectDirectory(tp.Dir);
	fresh.Scan(tp.Dir / "assets", changed);
	REQUIRE(fresh.FindHandleByPath("assets/textures/a.png", AssetType::Texture) == a);
	REQUIRE(fresh.SourceKey(a) == reg.SourceKey(a));

	// Nothing changed: a rescan is a no-op.
	reg.Scan(tp.Dir / "assets", changed);
	REQUIRE_FALSE(changed);
}

TEST_CASE("Source content or import settings change the cook key; a removed source drops its row", "[assets]")
{
	TempProject tp;
	tp.Write("assets/textures/a.png", "v1");
	AssetRegistry reg;
	reg.SetProjectDirectory(tp.Dir);
	bool changed = false;
	reg.Scan(tp.Dir / "assets", changed);
	const AssetHandle a = reg.FindHandleByPath("assets/textures/a.png", AssetType::Texture);
	const uint64_t key1 = reg.SourceKey(a);

	// Different bytes -> different key (content hash), same handle (the .meta owns identity).
	tp.Write("assets/textures/a.png", "v2 with more bytes");
	REQUIRE(reg.Refresh("assets/textures/a.png"));
	const uint64_t key2 = reg.SourceKey(a);
	REQUIRE(key2 != key1);
	REQUIRE(reg.FindHandleByPath("assets/textures/a.png", AssetType::Texture) == a);

	// Different import settings -> different key, persisted in the sidecar.
	ImportSettings settings = reg.GetImportSettings(a);
	REQUIRE(settings.Texture.GenerateMips);
	settings.Texture.GenerateMips = false;
	REQUIRE(reg.SetImportSettings(a, settings));
	REQUIRE(reg.SourceKey(a) != key2);
	REQUIRE_FALSE(AssetMetaIO::Load(tp.Dir / "assets/textures/a.png")->Import.Texture.GenerateMips);

	// Source deleted -> row dropped on the next scan.
	std::filesystem::remove(tp.Dir / "assets/textures/a.png");
	reg.Scan(tp.Dir / "assets", changed);
	REQUIRE(changed);
	REQUIRE(reg.GetMetadata(a) == nullptr);
}

TEST_CASE("Sub-assets (file?submesh=N) get their GUIDs from the meta's SubAssets map", "[assets]")
{
	TempProject tp;
	std::filesystem::create_directories(tp.Dir / "assets/meshes");
	tp.Write("assets/meshes/m.obj", "o cube");
	AssetRegistry reg;
	reg.SetProjectDirectory(tp.Dir);
	const AssetHandle part0 = reg.Import("assets/meshes/m.obj?submesh=0", AssetType::Mesh);
	const AssetHandle part1 = reg.Import("assets/meshes/m.obj?submesh=1", AssetType::Mesh);
	REQUIRE(part0 != part1);
	REQUIRE(reg.HandlesForSource("assets/meshes/m.obj").size() == 2);

	const auto meta = AssetMetaIO::Load(tp.Dir / "assets/meshes/m.obj");
	REQUIRE(meta);
	REQUIRE(meta->SubAssets.at("submesh=0") == part0);
	REQUIRE(meta->SubAssets.at("submesh=1") == part1);

	// A fresh scan (no cache) re-creates the part rows with the same handles.
	AssetRegistry fresh;
	fresh.SetProjectDirectory(tp.Dir);
	bool changed = false;
	fresh.Scan(tp.Dir / "assets", changed);
	REQUIRE(fresh.FindHandleByPath("assets/meshes/m.obj?submesh=1", AssetType::Mesh) == part1);
}

TEST_CASE("A model's parts get their own asset types, not the source's", "[assets][animation]")
{
	// A skinned model contributes parts that are NOT meshes. If every part inherited the source's type,
	// a scene referencing a clip would get a Mesh back from the registry, and the inspector's asset
	// picker would offer clips as meshes.
	REQUIRE(AssetRegistry::TypeForPart("", AssetType::Mesh) == AssetType::Mesh);
	REQUIRE(AssetRegistry::TypeForPart("submesh=3", AssetType::Mesh) == AssetType::Mesh);
	REQUIRE(AssetRegistry::TypeForPart("skeleton", AssetType::Mesh) == AssetType::Skeleton);
	REQUIRE(AssetRegistry::TypeForPart("animation=Walk", AssetType::Mesh) == AssetType::Animation);
	// A clip whose name contains an '=' must still resolve (the key is a prefix match, not a split).
	REQUIRE(AssetRegistry::TypeForPart("animation=A=B", AssetType::Mesh) == AssetType::Animation);
}

TEST_CASE("Skeleton and animation sub-assets survive a rescan with stable handles", "[assets][animation]")
{
	TempProject tp;
	std::filesystem::create_directories(tp.Dir / "assets" / "meshes");
	tp.Write("assets/meshes/hero.gltf", "{}"); // Scan only stats the source; the .meta carries the parts

	const std::filesystem::path source = "assets/meshes/hero.gltf";

	AssetRegistry registry;
	registry.SetProjectDirectory(tp.Dir);
	bool changed = false;
	registry.Scan(tp.Dir / "assets", changed);

	// Register the parts the skinned importer would have found.
	const AssetHandle skeleton = registry.Import(source.generic_string() + "?skeleton", AssetType::Skeleton);
	const AssetHandle walk = registry.Import(source.generic_string() + "?animation=Walk", AssetType::Animation);
	const AssetHandle run = registry.Import(source.generic_string() + "?animation=Run", AssetType::Animation);
	REQUIRE(skeleton.Value() != 0);
	REQUIRE(walk.Value() != 0);
	REQUIRE(run.Value() != 0);
	REQUIRE(walk != run);

	// A fresh clone of the project: nothing but the committed .meta sidecars. Handles must come back
	// identical, or every scene reference to a clip breaks on the next machine.
	AssetRegistry rebuilt;
	rebuilt.SetProjectDirectory(tp.Dir);
	rebuilt.Scan(tp.Dir / "assets", changed);

	const AssetMetadata* skeletonMeta = rebuilt.GetMetadata(skeleton);
	const AssetMetadata* walkMeta = rebuilt.GetMetadata(walk);
	const AssetMetadata* runMeta = rebuilt.GetMetadata(run);
	REQUIRE(skeletonMeta != nullptr);
	REQUIRE(walkMeta != nullptr);
	REQUIRE(runMeta != nullptr);

	REQUIRE(skeletonMeta->Type == AssetType::Skeleton);
	REQUIRE(walkMeta->Type == AssetType::Animation);
	REQUIRE(runMeta->Type == AssetType::Animation);
	REQUIRE(walkMeta->Path.generic_string() == source.generic_string() + "?animation=Walk");
}
