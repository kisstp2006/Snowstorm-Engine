#include "MeshResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/MeshRuntimeComponent.hpp"

namespace Snowstorm
{
	// Turns the authored MeshComponent handle into a resolved MeshRuntimeComponent. Drives on the
	// authored component's Init/Changed views plus a cheap per-frame check on
	// "ResolvedFrom != handle || !Instance" — the latter is what keeps polling an async load that is
	// still in flight (GetMeshAsync returns null until the worker finishes) and what re-resolves after
	// a hot reload evicted the asset cache.
	void MeshResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();

		for (const entt::entity e : FiniView<MeshComponent>())
		{
			if (reg.valid(e) && reg.any_of<MeshRuntimeComponent>(e))
			{
				reg.remove<MeshRuntimeComponent>(e);
			}
		}

		for (auto view = reg.view<MeshComponent>(); const entt::entity e : view)
		{
			const AssetHandle handle = reg.Read<MeshComponent>(e).Mesh;
			auto& rt = reg.Ensure<MeshRuntimeComponent>(e);
			if (rt.ResolvedFrom == handle.Value() && (rt.Instance || handle.Value() == 0))
			{
				continue; // up to date
			}
			Resolve(e);
		}
	}

	void MeshResolveSystem::Resolve(const entt::entity e) const
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = m_World->GetSingleton<AssetManagerSingleton>();
		const AssetHandle handle = reg.Read<MeshComponent>(e).Mesh;

		if (handle.Value() == 0)
		{
			reg.patch<MeshRuntimeComponent>(e, [](MeshRuntimeComponent& rt)
			                                {
				rt.Instance.reset();
				rt.ResolvedFrom = 0; });
			return;
		}

		// Non-blocking: null while the async load is in flight; try again next frame.
		Ref<Mesh> resolved = assets.GetMeshAsync(handle);
		if (!resolved)
		{
			return;
		}

		reg.patch<MeshRuntimeComponent>(e, [&](MeshRuntimeComponent& rt)
		                                {
			rt.Instance = resolved;
			rt.ResolvedFrom = handle.Value(); });
	}
}
