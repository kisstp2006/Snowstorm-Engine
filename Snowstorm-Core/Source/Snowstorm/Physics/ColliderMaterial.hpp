#pragma once

namespace Snowstorm
{
	// Per-collider surface response (Hazel ColliderMaterial). Combined pairwise by the backend.
	struct ColliderMaterial
	{
		float Friction = 0.5f;
		float Restitution = 0.15f;
	};

	inline bool operator==(const ColliderMaterial& a, const ColliderMaterial& b)
	{
		return a.Friction == b.Friction && a.Restitution == b.Restitution;
	}
}
