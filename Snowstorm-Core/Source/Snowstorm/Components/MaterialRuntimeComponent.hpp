#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

#include <cstdint>

namespace Snowstorm
{
	// Runtime twin of MaterialComponent: the resolved MaterialInstance (shared per asset, or a unique one
	// when MaterialOverridesComponent needs per-entity constants). Owned by MaterialResolveSystem; never
	// serialized/copied/shown. See MeshRuntimeComponent for the ResolvedFrom contract.
	struct MaterialRuntimeComponent
	{
		Ref<MaterialInstance> Instance;
		uint64_t ResolvedFrom = 0;
		bool Unique = false;
	};
}
