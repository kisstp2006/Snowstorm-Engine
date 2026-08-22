#include "PhysicsLayer.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <algorithm>

namespace Snowstorm
{
	std::vector<PhysicsLayer> PhysicsLayerManager::s_Layers = {PhysicsLayer{0, "Default", 1, ~0, true}};
	std::vector<std::string> PhysicsLayerManager::s_LayerNames = {"Default"};
	PhysicsLayer PhysicsLayerManager::s_NullLayer{0, "NULL", -1, -1, false};

	uint32_t PhysicsLayerManager::AddLayer(const std::string& name, const bool setCollisions)
	{
		for (const PhysicsLayer& layer : s_Layers)
		{
			if (layer.Name == name)
			{
				return layer.LayerID;
			}
		}

		const uint32_t layerId = GetNextLayerID();
		PhysicsLayer layer{layerId, name, 1 << layerId, 0, true};
		s_Layers.insert(s_Layers.begin() + layerId, layer);
		s_LayerNames.insert(s_LayerNames.begin() + layerId, name);

		if (setCollisions)
		{
			for (const PhysicsLayer& other : s_Layers)
			{
				SetLayerCollision(layerId, other.LayerID, true);
			}
		}
		return layerId;
	}

	void PhysicsLayerManager::RemoveLayer(const uint32_t layerId)
	{
		if (layerId == 0 || !IsLayerValid(layerId))
		{
			return; // Default stays
		}
		const PhysicsLayer removed = GetLayer(layerId);
		for (PhysicsLayer& other : s_Layers)
		{
			if (other.LayerID == layerId)
			{
				continue;
			}
			if (other.CollidesWith & removed.BitValue)
			{
				other.CollidesWith &= ~removed.BitValue;
			}
		}
		s_LayerNames.erase(s_LayerNames.begin() + layerId);
		s_Layers.erase(s_Layers.begin() + layerId);
	}

	void PhysicsLayerManager::UpdateLayerName(const uint32_t layerId, const std::string& newName)
	{
		for (const std::string& existing : s_LayerNames)
		{
			if (existing == newName)
			{
				return;
			}
		}
		PhysicsLayer& layer = GetLayer(layerId);
		s_LayerNames.erase(std::ranges::find(s_LayerNames, layer.Name));
		s_LayerNames.insert(s_LayerNames.begin() + layerId, newName);
		layer.Name = newName;
	}

	void PhysicsLayerManager::SetLayerCollision(const uint32_t layerId, const uint32_t otherLayer, const bool shouldCollide)
	{
		if (ShouldCollide(layerId, otherLayer) == shouldCollide)
		{
			return;
		}
		PhysicsLayer& a = GetLayer(layerId);
		PhysicsLayer& b = GetLayer(otherLayer);
		if (layerId == otherLayer)
		{
			a.CollidesWithSelf = shouldCollide;
		}
		if (shouldCollide)
		{
			a.CollidesWith |= b.BitValue;
			b.CollidesWith |= a.BitValue;
		}
		else
		{
			a.CollidesWith &= ~b.BitValue;
			b.CollidesWith &= ~a.BitValue;
		}
	}

	std::vector<PhysicsLayer> PhysicsLayerManager::GetLayerCollisions(const uint32_t layerId)
	{
		const PhysicsLayer& layer = GetLayer(layerId);
		std::vector<PhysicsLayer> out;
		for (const PhysicsLayer& other : s_Layers)
		{
			if (other.LayerID == layerId)
			{
				continue;
			}
			if (layer.CollidesWith & other.BitValue)
			{
				out.push_back(other);
			}
		}
		return out;
	}

	bool PhysicsLayerManager::ShouldCollide(const uint32_t layer1, const uint32_t layer2)
	{
		if (layer1 == layer2)
		{
			return GetLayer(layer1).CollidesWithSelf;
		}
		return (GetLayer(layer1).CollidesWith & GetLayer(layer2).BitValue) != 0;
	}

	PhysicsLayer& PhysicsLayerManager::GetLayer(const uint32_t layerId)
	{
		return layerId < s_Layers.size() ? s_Layers[layerId] : s_NullLayer;
	}

	PhysicsLayer& PhysicsLayerManager::GetLayer(const std::string& layerName)
	{
		for (PhysicsLayer& layer : s_Layers)
		{
			if (layer.Name == layerName)
			{
				return layer;
			}
		}
		return s_NullLayer;
	}

	bool PhysicsLayerManager::IsLayerValid(const uint32_t layerId)
	{
		return layerId < s_Layers.size() && s_Layers[layerId].IsValid();
	}

	bool PhysicsLayerManager::IsLayerValid(const std::string& layerName)
	{
		return GetLayer(layerName).IsValid();
	}

	void PhysicsLayerManager::ClearLayers()
	{
		s_Layers = {PhysicsLayer{0, "Default", 1, ~0, true}};
		s_LayerNames = {"Default"};
	}

	uint32_t PhysicsLayerManager::GetNextLayerID()
	{
		// The first gap in the ID sequence, else one past the end (max 32 layers: one bit each).
		int32_t last = -1;
		for (const PhysicsLayer& layer : s_Layers)
		{
			if (last != -1 && static_cast<int32_t>(layer.LayerID) != last + 1)
			{
				return static_cast<uint32_t>(last + 1);
			}
			last = static_cast<int32_t>(layer.LayerID);
		}
		const auto next = static_cast<uint32_t>(s_Layers.size());
		SS_CORE_ASSERT(next < 32, "At most 32 physics layers");
		return next;
	}
}
