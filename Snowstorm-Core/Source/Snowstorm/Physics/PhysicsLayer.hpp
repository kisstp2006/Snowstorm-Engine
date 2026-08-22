#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	// A named collision layer with its collides-with mask (Hazel PhysicsLayer). LayerID is the index a
	// RigidBodyComponent stores; BitValue is the bit in another layer's CollidesWith mask.
	struct PhysicsLayer
	{
		uint32_t LayerID = 0;
		std::string Name;
		int32_t BitValue = 0;
		int32_t CollidesWith = 0;
		bool CollidesWithSelf = true;

		[[nodiscard]] bool IsValid() const { return !Name.empty() && BitValue > 0; }
	};

	// Process-wide layer table + collision matrix (Hazel PhysicsLayerManager; Unity's Layer Collision
	// Matrix). Layer 0 "Default" always exists. Persisted with the project (ProjectSerializer).
	class PhysicsLayerManager
	{
	public:
		static uint32_t AddLayer(const std::string& name, bool setCollisions = true);
		static void RemoveLayer(uint32_t layerId);
		static void UpdateLayerName(uint32_t layerId, const std::string& newName);

		static void SetLayerCollision(uint32_t layerId, uint32_t otherLayer, bool shouldCollide);
		[[nodiscard]] static std::vector<PhysicsLayer> GetLayerCollisions(uint32_t layerId);
		[[nodiscard]] static bool ShouldCollide(uint32_t layer1, uint32_t layer2);

		[[nodiscard]] static const std::vector<PhysicsLayer>& GetLayers() { return s_Layers; }
		[[nodiscard]] static const std::vector<std::string>& GetLayerNames() { return s_LayerNames; }
		static PhysicsLayer& GetLayer(uint32_t layerId);
		static PhysicsLayer& GetLayer(const std::string& layerName);
		[[nodiscard]] static uint32_t GetLayerCount() { return static_cast<uint32_t>(s_Layers.size()); }
		[[nodiscard]] static bool IsLayerValid(uint32_t layerId);
		[[nodiscard]] static bool IsLayerValid(const std::string& layerName);

		// Back to the single "Default" layer that collides with everything.
		static void ClearLayers();

	private:
		static uint32_t GetNextLayerID();

		static std::vector<PhysicsLayer> s_Layers;
		static std::vector<std::string> s_LayerNames;
		static PhysicsLayer s_NullLayer;
	};
}
