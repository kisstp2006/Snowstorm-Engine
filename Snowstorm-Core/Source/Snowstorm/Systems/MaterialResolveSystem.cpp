#include "MaterialResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialRuntimeComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"

namespace Snowstorm
{
	namespace
	{
		// True if an override is handled per-instance (in the instance buffer) rather than by baking a
		// unique MaterialInstance. AlbedoTexture rides the per-instance buffer (RenderSystem resolves it
		// to a bindless index), so it does NOT force a unique instance — that's what lets objects with
		// different albedo textures still share a material and batch.
		bool IsPerInstanceOverride(const MaterialOverride& o)
		{
			return o.Type == MaterialOverrideType::Texture && o.Name == "AlbedoTexture";
		}

		// Does this entity have any override that genuinely needs a unique MaterialInstance (i.e. one
		// not handled per-instance, e.g. BaseColor)?
		bool NeedsUniqueInstance(const MaterialOverridesComponent& ov)
		{
			for (const MaterialOverride& o : ov.Overrides)
			{
				if (!IsPerInstanceOverride(o))
				{
					return true;
				}
			}
			return false;
		}

		void ApplyOverrides(AssetManagerSingleton& assets,
		                    const MaterialOverridesComponent& ov,
		                    MaterialInstance& mi)
		{
			// Apply only the overrides that bake into the instance; per-instance ones (albedo) are
			// handled at draw time via the instance buffer, so skip them here.
			for (const MaterialOverride& o : ov.Overrides)
			{
				if (IsPerInstanceOverride(o))
				{
					continue;
				}
				if (o.Name == "BaseColor" && o.Type == MaterialOverrideType::Color)
				{
					mi.SetBaseColor(o.Color);
				}
			}
		}
	}

	void MaterialResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		for (const entt::entity e : FiniView<MaterialComponent>())
		{
			if (reg.valid(e) && reg.any_of<MaterialRuntimeComponent>(e))
			{
				reg.remove<MaterialRuntimeComponent>(e);
			}
		}

		std::unordered_set<entt::entity> dirty;

		for (auto e : InitView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : InitView<MaterialOverridesComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialOverridesComponent>())
			dirty.insert(e);
		for (auto e : FiniView<MaterialOverridesComponent>())
			dirty.insert(e);

		for (const entt::entity e : m_PendingResolve)
			dirty.insert(e);
		m_PendingResolve.clear();

		// Safety net (hot reload / async readiness): a runtime component whose instance is missing or was
		// resolved from a different handle re-resolves without waiting for a Changed event.
		for (auto view = reg.view<MaterialComponent>(); const entt::entity e : view)
		{
			const AssetHandle handle = reg.Read<MaterialComponent>(e).Material;
			auto& rt = reg.Ensure<MaterialRuntimeComponent>(e);
			if (rt.ResolvedFrom != handle.Value() || (!rt.Instance && handle.Value() != 0))
			{
				dirty.insert(e);
			}
		}

		for (const entt::entity e : dirty)
		{
			if (!reg.valid(e) || !reg.any_of<MaterialComponent>(e))
			{
				continue;
			}

			const AssetHandle handle = reg.Read<MaterialComponent>(e).Material;
			if (handle.Value() == 0)
			{
				reg.patch<MaterialRuntimeComponent>(e, [](MaterialRuntimeComponent& rt)
				                                    {
					rt.Instance.reset();
					rt.ResolvedFrom = 0;
					rt.Unique = false; });
				continue;
			}

			const MaterialOverridesComponent* ov = reg.try_get_const<MaterialOverridesComponent>(e);
			if (ov)
			{
				for (const MaterialOverride& o : ov->Overrides)
				{
					if (o.Type == MaterialOverrideType::Texture && o.Texture != 0)
					{
						(void)assets.GetTextureViewAsync(o.Texture);
					}
				}
			}

			const bool needsUnique = (ov && NeedsUniqueInstance(*ov));
			Ref<MaterialInstance> instance = needsUnique ? assets.CreateMaterialInstanceUnique(handle)
			                                             : assets.GetMaterialInstance(handle);
			if (!instance)
			{
				m_PendingResolve.insert(e); // shader/pipeline not ready yet
				continue;
			}
			if (needsUnique)
			{
				ApplyOverrides(assets, *ov, *instance);
			}

			reg.patch<MaterialRuntimeComponent>(e, [&](MaterialRuntimeComponent& rt)
			                                    {
				rt.Instance = instance;
				rt.ResolvedFrom = handle.Value();
				rt.Unique = needsUnique; });
		}
	}
}
