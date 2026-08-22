#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Physics/PhysicsLayer.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Project/ProjectSerializer.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;

namespace
{
	class TemporaryProjectFile
	{
	public:
		TemporaryProjectFile()
		    : m_Path(std::filesystem::temp_directory_path() / "Snowstorm-ProjectSerializerTests.ssproj")
		{
		}

		~TemporaryProjectFile()
		{
			std::error_code error;
			std::filesystem::remove(m_Path, error);
		}

		void Write(const char* contents) const
		{
			std::ofstream out(m_Path);
			REQUIRE(out.is_open());
			out << contents;
		}

		[[nodiscard]] const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};
}

TEST_CASE("ProjectSerializer rejects malformed project files without mutating the project", "[project][serialize]")
{
	TemporaryProjectFile file;
	Project project;
	project.GetConfig().Name = "Original";
	project.SetProjectDirectory("original/directory");
	project.SetProjectFileName("Original.ssproj");

	SECTION("Malformed JSON")
	{
		file.Write(R"({"Project":)");
	}

	SECTION("Wrong field type")
	{
		file.Write(R"({"Project":{"Name":42}})");
	}

	bool deserialized = true;
	REQUIRE_NOTHROW(deserialized = ProjectSerializer::Deserialize(project, file.GetPath()));
	REQUIRE_FALSE(deserialized);
	REQUIRE(project.GetConfig().Name == "Original");
	REQUIRE(project.GetProjectDirectory() == "original/directory");
	REQUIRE(project.GetProjectFileName() == "Original.ssproj");
}

TEST_CASE("ProjectSerializer round-trips the physics layers and their collision matrix", "[project][serialize][physics]")
{
	TemporaryProjectFile file;
	PhysicsLayerManager::ClearLayers();

	const uint32_t player = PhysicsLayerManager::AddLayer("Player");
	const uint32_t enemy = PhysicsLayerManager::AddLayer("Enemy");
	const uint32_t ghost = PhysicsLayerManager::AddLayer("Ghost");

	// A matrix that a "everything collides" default could NOT reproduce: Ghost passes through everything
	// including itself, and Player ignores Enemy. If the loader only ORs bits in, this is what catches it.
	PhysicsLayerManager::SetLayerCollision(player, enemy, false);
	for (uint32_t other = 0; other < PhysicsLayerManager::GetLayerCount(); ++other)
	{
		PhysicsLayerManager::SetLayerCollision(ghost, other, false);
	}

	Project project;
	project.GetConfig().Name = "Layers";
	REQUIRE(ProjectSerializer::Serialize(project, file.GetPath()));

	// Wipe the table the way opening a different project would, then load it back.
	PhysicsLayerManager::ClearLayers();
	REQUIRE(PhysicsLayerManager::GetLayerCount() == 1);

	Project loaded;
	REQUIRE(ProjectSerializer::Deserialize(loaded, file.GetPath()));

	REQUIRE(PhysicsLayerManager::GetLayerCount() == 4);
	// Order is significant: the array index IS the LayerID that RigidBodyComponent stores.
	REQUIRE(PhysicsLayerManager::GetLayer(0u).Name == "Default");
	REQUIRE(PhysicsLayerManager::GetLayer(player).Name == "Player");
	REQUIRE(PhysicsLayerManager::GetLayer(enemy).Name == "Enemy");
	REQUIRE(PhysicsLayerManager::GetLayer(ghost).Name == "Ghost");

	REQUIRE_FALSE(PhysicsLayerManager::ShouldCollide(player, enemy));
	REQUIRE(PhysicsLayerManager::ShouldCollide(player, 0u));
	REQUIRE(PhysicsLayerManager::ShouldCollide(player, player));
	REQUIRE_FALSE(PhysicsLayerManager::ShouldCollide(ghost, ghost));
	REQUIRE_FALSE(PhysicsLayerManager::ShouldCollide(ghost, player));
	REQUIRE(PhysicsLayerManager::ShouldCollide(enemy, 0u));

	PhysicsLayerManager::ClearLayers();
}

TEST_CASE("ProjectSerializer resets the layer table for a project file without layers", "[project][serialize][physics]")
{
	TemporaryProjectFile file;
	PhysicsLayerManager::ClearLayers();
	PhysicsLayerManager::AddLayer("Leftover");
	REQUIRE(PhysicsLayerManager::GetLayerCount() == 2);

	// The layer table is process-wide, so a project that does not define layers must not inherit the
	// previously opened project's.
	file.Write(R"({"Project":{"Name":"NoLayers"}})");
	Project project;
	REQUIRE(ProjectSerializer::Deserialize(project, file.GetPath()));

	REQUIRE(PhysicsLayerManager::GetLayerCount() == 1);
	REQUIRE(PhysicsLayerManager::GetLayer(0u).Name == "Default");
	REQUIRE(PhysicsLayerManager::ShouldCollide(0u, 0u));

	PhysicsLayerManager::ClearLayers();
}
