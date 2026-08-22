#include "AssetImportPanel.hpp"

#include "Service/EditorTheme.hpp"

#include <Snowstorm/Assets/AssetManagerSingleton.hpp>
#include <Snowstorm/Assets/AssetMeta.hpp>
#include <Snowstorm/Components/ComponentRegistry.hpp>

#include <imgui.h>
#include <rttr/type>

namespace Snowstorm
{
	void DrawAssetImportInspector(AssetManagerSingleton& assets, const AssetHandle handle, const AssetType type)
	{
		const AssetMetadata* meta = assets.GetMetadata(handle);
		if (!meta)
		{
			EditorTheme::WarningBanner("ASSET NOT IN REGISTRY");
			return;
		}

		EditorTheme::SectionHeader(AssetTypeToString(type).c_str());
		ImGui::TextUnformatted(meta->Path.generic_string().c_str());
		ImGui::TextDisabled("GUID %s", handle.ToString().c_str());
		ImGui::Spacing();

		// Per-panel edit buffer: edits accumulate here and only hit the .meta on Reimport, so a half-way
		// toggle never re-cooks (cooking a model is seconds).
		static AssetHandle s_EditingHandle{0};
		static ImportSettings s_Working;
		if (s_EditingHandle != handle)
		{
			s_EditingHandle = handle;
			s_Working = assets.Registry().GetImportSettings(handle);
		}

		// Only the block that applies: the reflected struct for this asset type.
		rttr::instance block = (type == AssetType::Texture) ? rttr::instance(s_Working.Texture) : rttr::instance(s_Working.Mesh);
		const rttr::type blockType = block.get_derived_type();
		if (BeginPropertyTable("##import"))
		{
			for (const auto& prop : blockType.get_properties())
			{
				RenderProperty(prop, block);
			}
			EndPropertyTable();
		}

		const bool dirty = !(s_Working == assets.Registry().GetImportSettings(handle));
		ImGui::Spacing();
		ImGui::BeginDisabled(!dirty);
		if (ImGui::Button("Reimport", ImVec2(-FLT_MIN, 0.0f)))
		{
			if (!assets.ReimportAsset(handle, s_Working))
			{
				SS_CORE_ERROR("Reimport failed for '{}'.", meta->Path.string());
			}
		}
		ImGui::EndDisabled();
		if (!dirty)
		{
			ImGui::TextDisabled("Import settings are up to date.");
		}
	}
}
