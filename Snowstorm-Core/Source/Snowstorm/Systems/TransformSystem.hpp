#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Rebuilds every entity's WorldTransformComponent from its local TransformComponent and the
	// HierarchyComponent chain (Unity DOTS TransformSystemGroup / LocalToWorld). Runs first in Resolve so
	// everything downstream (camera view, culling, lights, TLAS, draws) reads finished world matrices.
	//
	// Entities are bucketed by hierarchy depth; each bucket is a pure per-entity job (parent world matrix
	// is finished by the previous bucket), so buckets run through JobSystem::ParallelFor (ecs.parallel) and
	// the buckets themselves in order. WorldTransform is marked Changed only where the matrix actually
	// changed, so ChangedView<WorldTransformComponent> is the precise "moved in world space" signal.
	class TransformSystem final : public System
	{
	public:
		explicit TransformSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		bool NeedsRebucket() const;
		void Rebucket();

		std::vector<std::vector<entt::entity>> m_Buckets; // index = depth
		std::vector<uint8_t> m_Changed;                   // scratch: per-entity "world matrix changed" flag
		size_t m_BucketedCount = 0;
	};
}
