#include "ProjectSerializer.hpp"

#include "Snowstorm/Physics/PhysicsLayer.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	using json = nlohmann::json;

	bool ProjectSerializer::Serialize(const Project& project, const std::filesystem::path& filePath)
	{
		const ProjectConfig& config = project.GetConfig();

		json projectNode;
		projectNode["Name"] = config.Name;
		projectNode["AssetDirectory"] = config.AssetDirectory.generic_string();
		projectNode["AssetRegistryPath"] = config.AssetRegistryPath.generic_string();
		projectNode["StartScene"] = config.StartScene.generic_string();

		// Physics layers + their collision matrix belong to the project (Hazel keeps them in the project
		// file; Unity's equivalents are ProjectSettings/TagManager + the Physics collision matrix). The
		// PhysicsLayerManager table IS the source of truth, so ProjectConfig deliberately does NOT mirror
		// it -- there is nothing here that can drift out of sync with the running table.
		//
		// CollidesWith is written as layer NAMES rather than the in-memory bitmask: the file stays readable
		// and a hand edit can't quietly reference a bit no layer owns any more. Layer 0 "Default" always
		// exists and is written like any other layer; its array index is its LayerID, which is what
		// RigidBodyComponent::LayerID stores, so ORDER IS SIGNIFICANT.
		json layersNode = json::array();
		for (const PhysicsLayer& layer : PhysicsLayerManager::GetLayers())
		{
			json collidesWith = json::array();
			for (const PhysicsLayer& other : PhysicsLayerManager::GetLayerCollisions(layer.LayerID))
			{
				collidesWith.push_back(other.Name);
			}
			layersNode.push_back({{"Name", layer.Name},
			                      {"CollidesWith", std::move(collidesWith)},
			                      {"CollidesWithSelf", layer.CollidesWithSelf}});
		}
		projectNode["PhysicsLayers"] = std::move(layersNode);

		json root;
		root["Project"] = std::move(projectNode);

		std::ofstream out(filePath);
		if (!out.is_open())
			return false;

		out << root.dump(2);
		return true;
	}

	namespace
	{
		// Rebuild the process-wide layer table from the project file. Always resets first: the table is
		// static, so without this, opening project B would inherit project A's layers. A file with no
		// PhysicsLayers section therefore lands on exactly one layer ("Default", collides with everything),
		// which is the same state a fresh table has.
		void DeserializePhysicsLayers(const json& projectNode)
		{
			PhysicsLayerManager::ClearLayers();

			if (!projectNode.contains("PhysicsLayers") || !projectNode["PhysicsLayers"].is_array())
			{
				return;
			}
			const json& layersNode = projectNode["PhysicsLayers"];

			// Pass 1: every layer must exist before the matrix can name it (an entry may list a layer that
			// appears later in the array). setCollisions=false -- pass 2 is the authority on the matrix.
			for (const json& entry : layersNode)
			{
				const auto name = entry.value("Name", std::string{});
				if (name.empty() || name == "Default") // Default is layer 0 and always present
				{
					continue;
				}
				PhysicsLayerManager::AddLayer(name, /*setCollisions=*/false);
			}

			// Start from "nothing collides with anything", then apply exactly what the file lists. Without
			// the reset, Default's collides-with-everything mask would survive a file that deliberately
			// turns pairs OFF -- the matrix would silently be a union of the default and the saved state.
			for (uint32_t i = 0; i < PhysicsLayerManager::GetLayerCount(); ++i)
			{
				PhysicsLayer& layer = PhysicsLayerManager::GetLayer(i);
				layer.CollidesWith = 0;
				layer.CollidesWithSelf = false;
			}

			// Pass 2: the matrix. SetLayerCollision is symmetric, so listing a pair on either side is enough.
			for (const json& entry : layersNode)
			{
				const auto name = entry.value("Name", std::string{});
				const PhysicsLayer& layer = PhysicsLayerManager::GetLayer(name);
				if (!layer.IsValid())
				{
					continue;
				}
				const uint32_t layerId = layer.LayerID;
				if (entry.value("CollidesWithSelf", true))
				{
					PhysicsLayerManager::SetLayerCollision(layerId, layerId, true);
				}
				if (!entry.contains("CollidesWith") || !entry["CollidesWith"].is_array())
				{
					continue;
				}
				for (const json& otherName : entry["CollidesWith"])
				{
					if (!otherName.is_string())
					{
						continue;
					}
					const PhysicsLayer& other = PhysicsLayerManager::GetLayer(otherName.get<std::string>());
					if (other.IsValid())
					{
						PhysicsLayerManager::SetLayerCollision(layerId, other.LayerID, true);
					}
				}
			}
		}
	}

	bool ProjectSerializer::Deserialize(Project& project, const std::filesystem::path& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
			return false;

		ProjectConfig config = project.GetConfig();
		try
		{
			json root;
			in >> root;

			if (!root.contains("Project") || !root["Project"].is_object())
				return false;

			const json& projectNode = root["Project"];
			config.Name = projectNode.value("Name", config.Name);
			config.AssetDirectory = projectNode.value("AssetDirectory", config.AssetDirectory.generic_string());
			config.AssetRegistryPath = projectNode.value("AssetRegistryPath", config.AssetRegistryPath.generic_string());
			config.StartScene = projectNode.value("StartScene", config.StartScene.generic_string());

			DeserializePhysicsLayers(projectNode);
		}
		catch (const json::exception&)
		{
			return false;
		}

		project.GetConfig() = std::move(config);
		project.SetProjectDirectory(filePath.parent_path());
		project.SetProjectFileName(filePath.filename().string());

		return true;
	}
}
