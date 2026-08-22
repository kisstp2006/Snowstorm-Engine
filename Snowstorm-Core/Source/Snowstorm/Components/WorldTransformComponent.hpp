#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	// Derived world matrix of an entity (Unity DOTS LocalToWorld): TransformComponent is local to the
	// parent, this is the product down the hierarchy, rebuilt every frame by TransformSystem (Resolve
	// phase, first). Renderers, culling, lights and the TLAS read THIS; nothing but TransformSystem writes
	// it. It is marked Changed only when the matrix actually changed, so ChangedView<WorldTransformComponent>
	// is the precise "something moved in world space" signal (a parent move dirties its children too).
	struct WorldTransformComponent
	{
		glm::mat4 LocalToWorld{1.0f};
	};
}
