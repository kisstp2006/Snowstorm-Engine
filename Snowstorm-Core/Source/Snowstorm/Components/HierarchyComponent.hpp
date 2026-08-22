#pragma once

#include <entt/entt.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Parent/child links for the transform hierarchy (Unity DOTS Parent + Child buffer, Unreal
	// USceneComponent attach chain). Intrusive sibling list: no per-entity vector, O(1) unlink. Only
	// present on entities that have a parent or children; a root with no children carries none.
	//
	// Mutate ONLY through World::SetParent / DestroyEntity — the links, Depth and the world matrices
	// (WorldTransformComponent, rebuilt by TransformSystem) must stay consistent. Not reflected: the
	// serializer writes the parent as a UUID at the entity level and rebuilds the links on load.
	struct HierarchyComponent
	{
		entt::entity Parent = entt::null;
		entt::entity FirstChild = entt::null;
		entt::entity NextSibling = entt::null;
		entt::entity PrevSibling = entt::null;
		uint32_t Depth = 0; // 0 = root; TransformSystem propagates parents before children by this
	};
}
