#pragma once

#include "Snowstorm/Utility/UUID.hpp"

#include <glm/vec3.hpp>

#include <limits>
#include <vector>

namespace Snowstorm
{
	// Geometry queries against a PhysicsScene (Hazel SceneQueries.h). Entities are reported by UUID so a
	// script can hold the result across frames.
	struct SceneQueryHit
	{
		UUID HitEntity{0};
		glm::vec3 Position{0.0f};
		glm::vec3 Normal{0.0f};
		float Distance = 0.0f;

		void Clear()
		{
			HitEntity = UUID{0};
			Position = glm::vec3(std::numeric_limits<float>::max());
			Normal = glm::vec3(std::numeric_limits<float>::max());
			Distance = std::numeric_limits<float>::max();
		}
	};

	using ExcludedEntityMap = std::vector<UUID>;

	struct RayCastInfo
	{
		glm::vec3 Origin{0.0f};
		glm::vec3 Direction{0.0f, 0.0f, -1.0f};
		float MaxDistance = 1000.0f;
		ExcludedEntityMap ExcludedEntities;
	};

	enum class ShapeCastType : uint8_t
	{
		Box,
		Sphere,
		Capsule,
	};

	struct ShapeCastInfo
	{
		explicit ShapeCastInfo(const ShapeCastType type)
		    : m_Type(type)
		{
		}
		glm::vec3 Origin{0.0f};
		glm::vec3 Direction{0.0f};
		float MaxDistance = 0.0f;
		ExcludedEntityMap ExcludedEntities;
		[[nodiscard]] ShapeCastType GetCastType() const { return m_Type; }

	private:
		ShapeCastType m_Type;
	};

	struct BoxCastInfo final : ShapeCastInfo
	{
		BoxCastInfo()
		    : ShapeCastInfo(ShapeCastType::Box)
		{
		}
		glm::vec3 HalfExtent{0.0f};
	};

	struct SphereCastInfo final : ShapeCastInfo
	{
		SphereCastInfo()
		    : ShapeCastInfo(ShapeCastType::Sphere)
		{
		}
		float Radius = 0.0f;
	};

	struct CapsuleCastInfo final : ShapeCastInfo
	{
		CapsuleCastInfo()
		    : ShapeCastInfo(ShapeCastType::Capsule)
		{
		}
		float HalfHeight = 0.0f;
		float Radius = 0.0f;
	};
}
