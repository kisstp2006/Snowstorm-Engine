#include "TransformSystem.hpp"

#include "Snowstorm/Components/HierarchyComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/WorldTransformComponent.hpp"

namespace Snowstorm
{
	bool TransformSystem::NeedsRebucket() const
	{
		auto& reg = m_World->GetRegistry();
		if (reg.view<TransformComponent>().size() != m_BucketedCount || m_World->SceneGeneration() != m_BucketedGeneration)
		{
			return true; // entities added/removed, or the scene was wiped (a Play->Stop restore re-creates
			             // the same NUMBER of entities with new handles — the count alone would miss that)
		}
		return !InitView<TransformComponent>().empty() || !FiniView<TransformComponent>().empty() ||
		       !InitView<HierarchyComponent>().empty() || !ChangedView<HierarchyComponent>().empty() ||
		       !FiniView<HierarchyComponent>().empty() || reg.AnyDestroyedThisFrame();
	}

	void TransformSystem::Rebucket()
	{
		auto& reg = m_World->GetRegistry();
		for (auto& bucket : m_Buckets)
		{
			bucket.clear();
		}

		m_BucketedCount = 0;
		m_BucketedGeneration = m_World->SceneGeneration();
		for (const auto view = reg.view<TransformComponent>(); const entt::entity e : view)
		{
			const auto* h = reg.try_get_const<HierarchyComponent>(e);
			const uint32_t depth = h ? h->Depth : 0u;
			if (depth >= m_Buckets.size())
			{
				m_Buckets.resize(static_cast<size_t>(depth) + 1);
			}
			m_Buckets[depth].push_back(e);
			++m_BucketedCount;
		}
	}

	void TransformSystem::Execute(Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();

		// Ensure the derived component exists before the buckets read it (a fresh entity's first frame).
		for (const auto view = reg.view<TransformComponent>(); const entt::entity e : view)
		{
			if (!reg.any_of<WorldTransformComponent>(e))
			{
				reg.emplace<WorldTransformComponent>(e);
			}
		}

		if (NeedsRebucket())
		{
			Rebucket();
		}

		const bool parallel = CVars::EcsParallel.Get() && Application::Exists() &&
		                      Application::Get().GetServiceManager().ServiceRegistered<JobSystem>();

		for (const auto& bucket : m_Buckets)
		{
			if (bucket.empty())
			{
				continue;
			}

			m_Changed.assign(bucket.size(), 0);

			// Pure per-entity body: reads the entity's own local transform + its parent's FINISHED world
			// matrix (previous bucket), writes its own world matrix. Distinct entities -> distinct storage
			// slots, so this is race-free across workers.
			const auto body = [&](const size_t begin, const size_t end)
			{
				for (size_t i = begin; i < end; ++i)
				{
					const entt::entity e = bucket[i];
					if (!reg.valid(e) || !reg.any_of<TransformComponent>(e))
					{
						continue; // bucket is rebuilt on the next frame; never touch a dead handle
					}
					glm::mat4 world = reg.get<TransformComponent>(e).GetTransform();
					if (const auto* h = reg.try_get_const<HierarchyComponent>(e); h && h->Parent != entt::null)
					{
						world = reg.get<WorldTransformComponent>(h->Parent).LocalToWorld * world;
					}
					auto& wt = reg.get<WorldTransformComponent>(e);
					if (wt.LocalToWorld != world)
					{
						wt.LocalToWorld = world;
						m_Changed[i] = 1;
					}
				}
			};

			if (parallel)
			{
				Application::Get().GetServiceManager().GetService<JobSystem>().ParallelFor(bucket.size(), body, 256);
			}
			else
			{
				body(0, bucket.size());
			}

			// Post-barrier, single-threaded: restore ChangedView semantics for the entities that moved.
			for (size_t i = 0; i < bucket.size(); ++i)
			{
				if (m_Changed[i])
				{
					reg.MarkChanged<WorldTransformComponent>(bucket[i]);
				}
			}
		}
	}
}
