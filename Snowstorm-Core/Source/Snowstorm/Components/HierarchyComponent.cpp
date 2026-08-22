#include "HierarchyComponent.hpp"

#include "ComponentRegistry.hpp"

#include <rttr/registration.h>

namespace Snowstorm
{
	RTTR_REGISTRATION
	{
		// Registered for the registry's type-erased plumbing (Has/Remove) only — no properties: the links
		// are entt handles that mean nothing outside this World.
		rttr::registration::class_<HierarchyComponent>("Snowstorm::HierarchyComponent").constructor();
	}

	namespace
	{
		struct AutoRegisterHierarchy
		{
			AutoRegisterHierarchy()
			{
				ComponentRegisterOptions opts{};
				opts.Serializable = false; // written as "Parent" at the entity level by SceneSerializer
				opts.DrawInEditor = false;
				opts.Copyable = false; // duplication re-links the clone explicitly (World::SetParent)
				RegisterComponent<HierarchyComponent>(opts);
			}
		};
		const AutoRegisterHierarchy g_autoRegisterHierarchy;
	}
}
