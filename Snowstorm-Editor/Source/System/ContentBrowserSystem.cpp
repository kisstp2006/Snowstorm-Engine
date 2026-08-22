#include "ContentBrowserSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Input/DroppedFiles.hpp"
#include "Singletons/EditorCommandsSingleton.hpp"
#include "Singletons/EditorSelectionSingleton.hpp"
#include "Service/EditorTheme.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace Snowstorm
{
	namespace
	{
		// Map a file extension (lowercase, with dot) to an asset type. None means "not importable".

		ImVec4 TypeColor(const AssetType t)
		{
			switch (t)
			{
			case AssetType::Mesh:
				return {0.30f, 0.65f, 1.00f, 1.0f}; // blue
			case AssetType::Texture:
				return {0.40f, 0.85f, 0.40f, 1.0f}; // green
			case AssetType::Material:
				return {1.00f, 0.65f, 0.20f, 1.0f}; // amber
			case AssetType::Shader:
				return {0.85f, 0.40f, 0.85f, 1.0f}; // magenta
			case AssetType::Scene:
				return {0.95f, 0.85f, 0.30f, 1.0f}; // yellow
			default:
				return {0.6f, 0.6f, 0.6f, 1.0f};
			}
		}
	}

	void ContentBrowserSystem::Rescan()
	{
		m_Entries.clear();

		// The scan (import step) lives in Core: AssetRegistry::Scan walks the ACTIVE project's asset
		// directory, gives every source a .meta + registry handle, and hands back the file list. After a
		// project switch the fresh World's ContentBrowserSystem rescans automatically (m_Scanned starts
		// false), and it must list the new project's content.
		auto& assets = m_World->GetSingleton<AssetManagerSingleton>();
		for (const auto& file : assets.ScanAssets())
		{
			Entry entry;
			entry.Path = file.Path.generic_string();
			entry.DisplayName = file.Path.filename().string();
			entry.Type = file.Type;
			m_Entries.push_back(std::move(entry));
		}

		std::ranges::sort(m_Entries, [](const Entry& a, const Entry& b)
		                  {
			if (a.Type != b.Type) return a.Type < b.Type;
			return a.DisplayName < b.DisplayName; });

		m_Scanned = true;
	}

	void ContentBrowserSystem::Execute(Timestep)
	{
		if (!m_Scanned || std::exchange(s_RescanRequested, false))
		{
			Rescan();
		}

		ImGui::Begin("Content Browser");

		const bool acceptsDrop = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const std::vector<std::filesystem::path> dropped = DroppedFiles::TakeAll();
		if (acceptsDrop && !dropped.empty())
		{
			auto& cmds = SingletonView<EditorCommandsSingleton>();
			auto& notify = SingletonView<EditorNotificationsSingleton>();
			uint32_t imported = 0;
			for (const std::filesystem::path& path : dropped)
			{
				if (cmds.ImportAsset && cmds.ImportAsset(path))
				{
					++imported;
				}
				else
				{
					notify.Push("Could not import " + path.filename().string(), EditorToastType::Error);
				}
			}
			if (imported > 0)
			{
				notify.Push("Imported " + std::to_string(imported) + " asset(s)", EditorToastType::Success);
				Rescan();
			}
		}

		// Files under assets/ are auto-imported on scan, so this panel just lists them. Use Rescan
		// after dropping in new files (until a file watcher makes even that unnecessary).
		if (ImGui::Button("Rescan"))
		{
			Rescan();
		}
		ImGui::SameLine();

		// New Material (#114): write a default .ssmat into the project's asset root, then rescan — the scan
		// auto-imports and registers it like any dropped-in file, so it appears in the Materials tab and the
		// inspector's picker with no further wiring. A fresh MaterialAsset{} is already a valid material
		// (DefaultLit shader, white base color, no textures); the user edits it via the material inspector.
		if (ImGui::Button("New Material"))
		{
			auto& notify = SingletonView<EditorNotificationsSingleton>();
			const std::filesystem::path root = Project::GetActive()->GetAssetDirectory();
			std::error_code ec;
			if (std::filesystem::exists(root, ec) || std::filesystem::create_directories(root, ec))
			{
				// Pick a non-clobbering name: NewMaterial.ssmat, then NewMaterial_1.ssmat, ...
				std::filesystem::path target = root / "NewMaterial.ssmat";
				for (int i = 1; std::filesystem::exists(target, ec); ++i)
				{
					target = root / ("NewMaterial_" + std::to_string(i) + ".ssmat");
				}
				if (MaterialAssetIO::Save(target, MaterialAsset{}))
				{
					Rescan(); // auto-imports + registers the new .ssmat
					notify.Push("Created " + target.filename().string(), EditorToastType::Success);
				}
				else
				{
					notify.Push("Failed to write " + target.filename().string(), EditorToastType::Error);
				}
			}
			else
			{
				notify.Push("Asset directory missing; can't create material", EditorToastType::Error);
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu files", m_Entries.size());

		// Search box (filters by filename substring, case-insensitive).
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##search", "Search...", m_Search, sizeof(m_Search));

		// Type filter tabs: "All" plus one tab per asset type.
		if (ImGui::BeginTabBar("##type_tabs"))
		{
			constexpr std::pair<const char*, AssetType> tabs[] = {
			    {"All", AssetType::None},
			    {"Meshes", AssetType::Mesh},
			    {"Textures", AssetType::Texture},
			    {"Materials", AssetType::Material},
			    {"Shaders", AssetType::Shader},
			    {"Scenes", AssetType::Scene},
			};
			for (const auto& [label, type] : tabs)
			{
				if (ImGui::BeginTabItem(label))
				{
					m_Filter = type;
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();

		// Lowercase search needle for case-insensitive matching.
		std::string needle = m_Search;
		std::ranges::transform(needle, needle.begin(), [](const unsigned char c)
		                       { return static_cast<char>(std::tolower(c)); });

		const auto passesFilter = [&](const Entry& e)
		{
			if (m_Filter != AssetType::None && e.Type != m_Filter)
			{
				return false;
			}
			if (!needle.empty())
			{
				std::string name = e.DisplayName;
				std::ranges::transform(name, name.begin(), [](const unsigned char c)
				                       { return static_cast<char>(std::tolower(c)); });
				if (name.find(needle) == std::string::npos)
				{
					return false;
				}
			}
			return true;
		};

		if (ImGui::BeginChild("##content_list"))
		{
			auto& assets = m_World->GetSingleton<AssetManagerSingleton>();
			auto& selection = SingletonView<EditorSelectionSingleton>();

			// m_Entries is sorted by (Type, Name), so walking it groups naturally. In the "All" view
			// emit a colored section header each time the type changes.
			AssetType currentSection = AssetType::None;

			for (int i = 0; i < static_cast<int>(m_Entries.size()); ++i)
			{
				const Entry& entry = m_Entries[i];
				if (!passesFilter(entry))
				{
					continue;
				}

				if (m_Filter == AssetType::None && entry.Type != currentSection)
				{
					currentSection = entry.Type;
					ImGui::PushStyleColor(ImGuiCol_Text, TypeColor(entry.Type));
					ImGui::SeparatorText(AssetTypeToString(entry.Type).c_str());
					ImGui::PopStyleColor();
				}

				ImGui::PushID(i);
				ImGui::Indent(8.0f);

				const bool isSelectedAsset = selection.SelectedAssetType == entry.Type &&
				                             selection.SelectedAsset != 0 &&
				                             assets.FindHandle(entry.Path, entry.Type) == selection.SelectedAsset;

				if (ImGui::Selectable(entry.DisplayName.c_str(), isSelectedAsset))
				{
					// Single-click selects the asset for the Properties inspector. Only handle-referenced
					// asset types (materials, textures, ...) — scenes are opened by path, not selected.
					// The handle already exists (auto-import ran on scan). Materials are the only type with
					// an inspector today; others just record the selection harmlessly.
					if (entry.Type != AssetType::Scene)
					{
						if (const AssetHandle h = assets.FindHandle(entry.Path, entry.Type); h != 0)
						{
							selection.SelectAsset(h, entry.Type);
						}
					}
				}

				if (entry.Type == AssetType::Scene && ImGui::IsItemHovered() &&
				    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					auto& cmds = SingletonView<EditorCommandsSingleton>();
					auto& notify = SingletonView<EditorNotificationsSingleton>();
					if (cmds.OpenScene)
					{
						const bool ok = cmds.OpenScene(entry.Path);
						notify.Push(ok ? "Opened " + entry.DisplayName : "Failed to open " + entry.DisplayName,
						            ok ? EditorToastType::Success : EditorToastType::Error);
					}
				}

				// Right-click a scene -> context menu. "Set as Startup Scene" writes the project's StartScene
				// (Godot's "Set as Main Scene" model). entry.Path is project-relative; the layer stores it as
				// the StartScene and re-saves the .ssproj.
				if (entry.Type == AssetType::Scene && ImGui::BeginPopupContextItem("scene_ctx"))
				{
					auto& cmds = SingletonView<EditorCommandsSingleton>();
					auto& notify = SingletonView<EditorNotificationsSingleton>();
					if (ImGui::MenuItem("Open Scene") && cmds.OpenScene)
					{
						const bool ok = cmds.OpenScene(entry.Path);
						notify.Push(ok ? "Opened " + entry.DisplayName : "Failed to open " + entry.DisplayName,
						            ok ? EditorToastType::Success : EditorToastType::Error);
					}
					if (ImGui::MenuItem("Set as Startup Scene") && cmds.SetStartupScene)
					{
						const bool ok = cmds.SetStartupScene(entry.Path);
						notify.Push(ok ? entry.DisplayName + " is now the startup scene" : "Failed to set startup scene",
						            ok ? EditorToastType::Success : EditorToastType::Error);
					}
					ImGui::EndPopup();
				}

				ImGui::Unindent(8.0f);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}
}
