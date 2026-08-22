#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// Authored material reference (asset handle). The resolved MaterialInstance lives in
	// MaterialRuntimeComponent, filled by MaterialResolveSystem.
	struct MaterialComponent
	{
		AssetHandle Material{0};
	};
}
