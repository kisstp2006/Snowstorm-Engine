#pragma once

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Snowstorm
{
	// Local TRS (relative to the parent when the entity has a HierarchyComponent; world-space for roots).
	// The derived world matrix lives in WorldTransformComponent, rebuilt by TransformSystem every frame —
	// read that for rendering/culling.
	//
	// Rotation is stored BOTH as a quaternion and as Euler angles (Hazel's TransformComponent), kept in
	// sync through the setters, because neither alone is enough:
	//   - quaternions are what the math wants (no gimbal lock, correct interpolation),
	//   - Euler is what a human authors, can exceed 360 degrees, and — crucially — quat -> Euler -> quat
	//     is not invariant, so deriving the Euler fresh every frame makes the inspector jump by 180
	//     degrees at some poses while dragging.
	// The fields are private so a caller cannot set one and forget the other.
	//
	// Euler order is the engine's YXZ (yaw -> pitch -> roll, radians), see Math/Transform.hpp.
	struct TransformComponent
	{
		glm::vec3 Translation{0.0f, 0.0f, 0.0f};
		glm::vec3 Scale{1.0f, 1.0f, 1.0f};

		TransformComponent() = default;
		explicit TransformComponent(const glm::vec3& translation)
		    : Translation(translation)
		{
		}

		// Local matrix: T * R * S.
		[[nodiscard]] glm::mat4 GetTransform() const
		{
			return glm::translate(glm::mat4(1.0f), Translation) * glm::mat4_cast(m_Rotation) * glm::scale(glm::mat4(1.0f), Scale);
		}
		// Decomposes into translation / rotation / scale (shear and perspective are discarded).
		void SetTransform(const glm::mat4& transform);

		[[nodiscard]] const glm::quat& GetRotation() const { return m_Rotation; }
		// Also refreshes the Euler representation, picking the equivalent angles closest to the current
		// ones so a continuous rotation doesn't flip in the inspector.
		void SetRotation(const glm::quat& rotation);

		[[nodiscard]] const glm::vec3& GetRotationEuler() const { return m_RotationEuler; }
		void SetRotationEuler(const glm::vec3& euler);

		operator glm::mat4() const { return GetTransform(); }

	private:
		glm::vec3 m_RotationEuler{0.0f, 0.0f, 0.0f};
		glm::quat m_Rotation{1.0f, 0.0f, 0.0f, 0.0f};
	};

	inline bool operator==(const TransformComponent& a, const TransformComponent& b)
	{
		return a.Translation == b.Translation && a.GetRotation() == b.GetRotation() && a.Scale == b.Scale;
	}
}
