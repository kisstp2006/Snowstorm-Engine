#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <cstdint>

namespace Snowstorm
{
	// Backend-agnostic physics vocabulary (Hazel PhysicsTypes.h shape). Components and the PhysicsScene /
	// PhysicsBody interfaces speak these; the Jolt backend translates.

	enum class EBodyType : uint8_t
	{
		Static,
		Dynamic,
		Kinematic,
	};

	enum class EForceMode : uint8_t
	{
		Force = 0,
		Impulse,
		VelocityChange,
		Acceleration,
	};

	enum class EActorAxis : uint32_t
	{
		None = 0,
		TranslationX = BIT(0),
		TranslationY = BIT(1),
		TranslationZ = BIT(2),
		Translation = TranslationX | TranslationY | TranslationZ,
		RotationX = BIT(3),
		RotationY = BIT(4),
		RotationZ = BIT(5),
		Rotation = RotationX | RotationY | RotationZ,
	};

	inline EActorAxis operator|(const EActorAxis a, const EActorAxis b) { return static_cast<EActorAxis>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
	inline EActorAxis operator&(const EActorAxis a, const EActorAxis b) { return static_cast<EActorAxis>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
	inline bool Any(const EActorAxis a) { return static_cast<uint32_t>(a) != 0; }

	enum class ECollisionDetectionType : uint8_t
	{
		Discrete,
		Continuous,
	};

	// How a MeshColliderComponent is cooked: the render mesh's triangles (static only) or its convex hull.
	enum class ECollisionComplexity : uint8_t
	{
		Default,            // triangle mesh for static bodies, convex hull for moving ones
		UseComplexAsSimple, // always the exact triangle mesh (static bodies only)
		UseSimpleAsComplex, // always the convex hull
	};

	enum class EFalloffMode : uint8_t
	{
		Constant,
		Linear,
	};

	enum class ContactType : int8_t
	{
		None = -1,
		CollisionBegin,
		CollisionEnd,
		TriggerBegin,
		TriggerEnd,
	};

	enum class BodyAddType : uint8_t
	{
		AddImmediate,
		AddBulk, // batched: the backend adds them together in Simulate's next PreSimulate
	};
}
