#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// Authored mesh reference (asset handle). The resolved GPU mesh lives in MeshRuntimeComponent,
	// filled by MeshResolveSystem (Unity DOTS authoring vs runtime component; Unreal UPROPERTY(Transient)).
	struct MeshComponent
	{
		AssetHandle Mesh{0};
	};
}
