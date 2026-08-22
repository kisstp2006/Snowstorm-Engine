#include "TransformComponent.hpp"

#include "ComponentRegistry.hpp"

#include "Snowstorm/Math/Transform.hpp"

#include <glm/gtx/norm.hpp>

#include <rttr/registration.h>

#include <array>

namespace Snowstorm
{
	namespace
	{
		// Wrap each angle into [-pi, pi] so "closest" comparisons don't trip over full turns.
		glm::vec3 WrapToPi(const glm::vec3& v)
		{
			constexpr float pi = glm::pi<float>();
			return glm::mod(v + pi, 2.0f * pi) - pi;
		}
	}

	void TransformComponent::SetTransform(const glm::mat4& transform)
	{
		glm::vec3 translation, scale;
		glm::quat rotation;
		if (!DecomposeTRS(transform, translation, rotation, scale))
		{
			return;
		}
		Translation = translation;
		Scale = scale;
		SetRotation(rotation);
	}

	void TransformComponent::SetRotationEuler(const glm::vec3& euler)
	{
		m_RotationEuler = euler;
		m_Rotation = QuatFromEulerRadians(euler);
	}

	void TransformComponent::SetRotation(const glm::quat& rotation)
	{
		const glm::vec3 previous = m_RotationEuler;
		m_Rotation = rotation;

		// A quaternion maps to infinitely many Euler triples, and the extraction returns just one of them.
		// For the YXZ factorization the second family of solutions is (pi - pitch, yaw + pi, roll + pi),
		// with either sign on the outer two. Take whichever candidate sits closest to the angles we had, so
		// dragging a gizmo (or a physics body rolling over) never makes the inspector snap by 180 degrees.
		const glm::vec3 extracted = EulerRadiansFromQuat(rotation);
		constexpr float pi = glm::pi<float>();
		const std::array<glm::vec3, 5> candidates{
		    extracted,
		    glm::vec3{pi - extracted.x, extracted.y - pi, extracted.z - pi},
		    glm::vec3{pi - extracted.x, extracted.y - pi, extracted.z + pi},
		    glm::vec3{pi - extracted.x, extracted.y + pi, extracted.z - pi},
		    glm::vec3{pi - extracted.x, extracted.y + pi, extracted.z + pi},
		};

		const glm::vec3* best = &candidates[0];
		float bestDistance = glm::length2(WrapToPi(candidates[0] - previous));
		for (size_t i = 1; i < candidates.size(); ++i)
		{
			if (const float distance = glm::length2(WrapToPi(candidates[i] - previous)); distance < bestDistance)
			{
				bestDistance = distance;
				best = &candidates[i];
			}
		}
		m_RotationEuler = WrapToPi(*best);
	}

	RTTR_REGISTRATION
	{
		using namespace rttr;

		// The Euler angles are the authored/serialized truth (radians); the quaternion is derived, so it is
		// not a reflected property. "Position" keeps the scene-file key the component has always used.
		registration::class_<TransformComponent>("Snowstorm::TransformComponent")
		    .constructor()
		    .property("Position", &TransformComponent::Translation)
		    .property("Rotation", &TransformComponent::GetRotationEuler, &TransformComponent::SetRotationEuler)(
		        metadata("EulerDegrees", true) // inspector edits degrees; storage stays radians
		        )
		    .property("Scale", &TransformComponent::Scale);
	}

	AUTO_REGISTER_COMPONENT(TransformComponent);
}
