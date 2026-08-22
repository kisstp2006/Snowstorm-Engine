#include "SceneSerializer.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"

#include "Snowstorm/Utility/JsonUtils.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <sstream>


namespace Snowstorm
{
	bool SceneSerializer::SerializeEntity(Entity entity, json& out)
	{
		if (!entity || !entity.HasComponent<IDComponent>() || !entity.HasComponent<TagComponent>())
		{
			return false;
		}

		out = json::object();
		out["UUID"] = entity.GetComponent<IDComponent>().Id.ToString();
		out["Name"] = entity.GetComponent<TagComponent>().Tag;
		// Hierarchy is structural (like UUID/Name), not a component: the parent is referenced by UUID and
		// the links are rebuilt by World::SetParent on load (see ResolveParent).
		if (const Entity parent = entity.GetWorld()->GetParent(entity))
		{
			out["Parent"] = parent.GetComponent<IDComponent>().Id.ToString();
		}

		json comps = json::object();

		for (const auto& info : GetComponentRegistry())
		{
			if (!info.Type.is_valid() || !info.HasFn || !info.GetInstanceFn)
			{
				continue;
			}

			if (!info.Serializable)
			{
				continue;
			}

			const std::string typeName = info.Type.get_name().to_string();
			if (typeName == "Snowstorm::IDComponent" || typeName == "Snowstorm::TagComponent")
			{
				continue;
			}

			if (!info.HasFn(entity))
			{
				continue;
			}


			rttr::instance inst = info.GetInstanceFn(entity);
			comps[typeName] = RttrInstanceToJson(inst);
		}

		out["Components"] = std::move(comps);
		return true;
	}

	bool SceneSerializer::ResolveParent(World& world, const Entity entity, const json& entJ)
	{
		if (!entJ.contains("Parent") || !entJ["Parent"].is_string())
		{
			return true; // root
		}
		const Entity parent = world.FindEntityByUUID(UUID::FromString(entJ["Parent"].get<std::string>()));
		if (!parent)
		{
			return false; // not created yet (scene load resolves in a second pass) or genuinely missing
		}
		// Local values in the file are already relative to the parent: keep them (keepWorld = false).
		return world.SetParent(entity, parent, /*keepWorld*/ false);
	}

	Entity SceneSerializer::DeserializeEntity(World& world, const json& entJ)
	{
		const std::string uuidStr = entJ.value("UUID", "0");
		const std::string name = entJ.value("Name", "Entity");

		Entity entity = world.CreateEntityWithUUID(UUID::FromString(uuidStr), name);

		if (!entJ.contains("Components") || !entJ["Components"].is_object())
		{
			ResolveParent(world, entity, entJ);
			return entity;
		}

		const json& comps = entJ["Components"];

		for (auto it = comps.begin(); it != comps.end(); ++it)
		{
			const std::string& compTypeName = it.key();
			const json& compData = it.value();

			// Override path first (assets, entity refs, etc.)

			// Find matching component registration
			const auto& registry = GetComponentRegistry();
			auto found = std::ranges::find_if(registry,
			                                  [&](const ComponentInfo& ci)
			                                  {
				                                  return ci.Type.is_valid() && ci.Type.get_name().to_string() == compTypeName;
			                                  });

			if (found == registry.end())
			{
				continue;
			}

			if (!found->Serializable)
			{
				continue;
			}

			if (!found->EmplaceDefaultFn || !found->GetInstanceFn)
			{
				continue;
			}

			found->EmplaceDefaultFn(entity);
			rttr::instance inst = found->GetInstanceFn(entity);

			JsonToRttrInstance(compData, inst);
		}

		// Single-entity callers (undo redo, duplicate) have the parent alive already; a scene load
		// re-runs this for every entity after all of them exist (file order is not parent-first).
		ResolveParent(world, entity, entJ);

		return entity;
	}

	void SceneSerializer::SerializeSubtree(const Entity root, json& outArray)
	{
		if (!outArray.is_array())
		{
			outArray = json::array();
		}
		json entJ;
		if (SerializeEntity(root, entJ))
		{
			outArray.push_back(std::move(entJ));
		}
		root.GetWorld()->ForEachChild(root, [&](const Entity child)
		                              { SerializeSubtree(child, outArray); });
	}

	void SceneSerializer::DeserializeEntities(World& world, const json& array)
	{
		if (!array.is_array())
		{
			return;
		}
		std::vector<std::pair<Entity, const json*>> unresolved;
		for (const auto& entJ : array)
		{
			Entity entity = DeserializeEntity(world, entJ);
			if (entJ.contains("Parent") && !world.GetParent(entity))
			{
				unresolved.emplace_back(entity, &entJ);
			}
		}
		// Second pass: parents that appeared later in the array than their children.
		for (const auto& [entity, entJ] : unresolved)
		{
			if (!ResolveParent(world, entity, *entJ))
			{
				SS_CORE_WARN("SceneSerializer: entity '{}' references a missing parent {}; left at root.",
				             entity.GetComponent<TagComponent>().Tag, (*entJ)["Parent"].get<std::string>());
			}
		}
	}

	std::string SceneSerializer::SerializeToString(const World& world)
	{
		json root;
		root["Scene"] = {{"Version", 2}, {"Name", "Untitled"}};
		root["Entities"] = json::array();

		auto& reg = world.GetRegistry();

		// Roots in registry order, each followed by its subtree depth-first (sibling order = hierarchy
		// order), so a saved file is deterministic and children always follow their parent.
		World& mutableWorld = const_cast<World&>(world);
		std::function<void(Entity)> writeSubtree = [&](const Entity entity)
		{
			json entJ;
			if (SerializeEntity(entity, entJ))
			{
				root["Entities"].push_back(std::move(entJ));
			}
			mutableWorld.ForEachChild(entity, [&](const Entity child)
			                          {
				if (!reg.any_of<DoNotSerializeComponent>(child.Handle()))
				{
					writeSubtree(child);
				} });
		};

		auto view = reg.view<IDComponent, TagComponent>();
		for (const entt::entity e : view)
		{
			// Skip engine-owned entities tagged DoNotSerialize (editor Scene-view camera/viewport): they
			// live above the scene and are recreated by the editor, not loaded from the file.
			if (reg.any_of<DoNotSerializeComponent>(e))
			{
				continue;
			}
			Entity entity{e, &mutableWorld};
			if (mutableWorld.GetParent(entity))
			{
				continue; // written under its root
			}
			writeSubtree(entity);
		}

		return root.dump(2);
	}

	bool SceneSerializer::DeserializeFromString(World& world, const std::string& jsonText)
	{
		json root;
		try
		{
			root = json::parse(jsonText);
		}
		catch (const json::parse_error&)
		{
			return false;
		}

		if (!root.contains("Entities") || !root["Entities"].is_array())
		{
			return false;
		}

		DeserializeEntities(world, root["Entities"]);
		return true;
	}

	bool SceneSerializer::Serialize(const World& world, const std::string& filePath)
	{
		std::ofstream out(filePath);
		if (!out.is_open())
		{
			return false;
		}

		out << SerializeToString(world);
		return true;
	}

	bool SceneSerializer::Deserialize(World& world, const std::string& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
		{
			return false;
		}

		std::stringstream ss;
		ss << in.rdbuf();
		return DeserializeFromString(world, ss.str());
	}
}
