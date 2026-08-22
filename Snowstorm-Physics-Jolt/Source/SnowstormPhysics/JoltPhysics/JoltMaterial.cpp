#include "JoltMaterial.hpp"

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	namespace
	{
		const JoltMaterial* MaterialOf(const JPH::Body& body, const JPH::SubShapeID& sub)
		{
			return dynamic_cast<const JoltMaterial*>(body.GetShape()->GetMaterial(sub));
		}
	}

	float JoltMaterial::CombineFriction(const JPH::Body& body1, const JPH::SubShapeID& sub1, const JPH::Body& body2, const JPH::SubShapeID& sub2)
	{
		const JoltMaterial* a = MaterialOf(body1, sub1);
		const JoltMaterial* b = MaterialOf(body2, sub2);
		const float fa = a ? a->Friction : body1.GetFriction();
		const float fb = b ? b->Friction : body2.GetFriction();
		return std::sqrt(fa * fb); // geometric mean (PhysX/Unity "Average"-like, never below either zero)
	}

	float JoltMaterial::CombineRestitution(const JPH::Body& body1, const JPH::SubShapeID& sub1, const JPH::Body& body2, const JPH::SubShapeID& sub2)
	{
		const JoltMaterial* a = MaterialOf(body1, sub1);
		const JoltMaterial* b = MaterialOf(body2, sub2);
		const float ra = a ? a->Restitution : body1.GetRestitution();
		const float rb = b ? b->Restitution : body2.GetRestitution();
		return std::max(ra, rb);
	}
}
