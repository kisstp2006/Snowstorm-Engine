#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	class AssetManagerSingleton;

	// Properties-panel body for a selected texture/mesh asset (Unity's texture/model importer inspector,
	// reduced to the settings the pipeline consumes): the reflected ImportSettings block for the asset's
	// type plus "Reimport", which writes the .meta and hot-swaps the live object.
	void DrawAssetImportInspector(AssetManagerSingleton& assets, AssetHandle handle, AssetType type);
}
