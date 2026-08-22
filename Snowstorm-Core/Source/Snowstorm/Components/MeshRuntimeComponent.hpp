#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"

#include <cstdint>

namespace Snowstorm
{
	// Runtime twin of MeshComponent: the resolved GPU mesh. Owned by MeshResolveSystem (Resolve phase);
	// never serialized, never copied on duplicate, never shown in the inspector. ResolvedFrom records the
	// handle the Instance was resolved from, so a changed handle (or a hot-reloaded asset, which bumps the
	// cache) re-resolves without any polling on "Instance == null".
	struct MeshRuntimeComponent
	{
		Ref<Mesh> Instance;
		uint64_t ResolvedFrom = 0;
	};
}
