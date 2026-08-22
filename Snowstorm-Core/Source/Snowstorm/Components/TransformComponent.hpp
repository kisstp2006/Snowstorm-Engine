#pragma once

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Snowstorm
{
	// Local TRS (relative to the parent when the entity has a HierarchyComponent; world-space for roots).
	// Rotation is a quaternion (Unity/Unreal/Godot store orientation the same way); the inspector and the
	// legacy v1 scene format speak Euler via Math/Transform.hpp. The derived world matrix lives in
	// WorldTransformComponent, filled by TransformSystem every frame — read that for rendering/culling.
	struct TransformComponent
	{
		glm::vec3 Position{0.0f, 0.0f, 0.0f};
		glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f}; // identity (w, x, y, z)
		glm::vec3 Scale{1.0f, 1.0f, 1.0f};

		// Local matrix: T * R * S.
		[[nodiscard]] glm::mat4 GetTransformMatrix() const
		{
			return glm::translate(glm::mat4(1.0f), Position) * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
		}

		operator glm::mat4() const { return GetTransformMatrix(); }
	};

	inline bool operator==(const TransformComponent& a, const TransformComponent& b)
	{
		return a.Position == b.Position && a.Rotation == b.Rotation && a.Scale == b.Scale;
	}
}
