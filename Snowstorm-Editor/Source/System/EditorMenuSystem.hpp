#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorNotificationsSingleton;

	class EditorMenuSystem final : public System
	{
	public:
		explicit EditorMenuSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Open/SaveAs scene via native file dialogs, then hand off to EditorCommandsSingleton.
		// Shared by the File menu items and the Ctrl+O / Ctrl+Shift+S shortcuts so both paths
		// behave (and toast) identically.
		void OpenSceneAction(EditorNotificationsSingleton& notify);
		void SaveSceneAction(EditorNotificationsSingleton& notify);
		void SaveSceneAsAction(EditorNotificationsSingleton& notify);

		void DrawImportModelPopup(EditorNotificationsSingleton& notify);

		// New Project modal: separate Name + Location fields (Location has a "..." browse-folder
		// button), mirroring Hazel's UI_ShowNewProjectPopup. The actual project directory becomes
		// Location/Name (a fresh subfolder Create scaffolds), not the browsed folder itself.
		void DrawNewProjectPopup(EditorNotificationsSingleton& notify);

		// Keyboard & mouse shortcut reference window (Help menu). Keep its contents in sync with the
		// actual bindings whenever a shortcut is added or changed — see CLAUDE.md.
		void DrawShortcutsWindow();

		// Project-scoped settings, i.e. what lives in the .ssproj rather than in the CVar config: today
		// the physics layers + their collision matrix. Same place every engine puts it (Unity Project
		// Settings > Physics > Layer Collision Matrix, Unreal Project Settings > Collision, Godot Project
		// Settings > Layer Names), and deliberately NOT the "Settings" panel, which is render tuning.
		void DrawProjectSettingsWindow(EditorNotificationsSingleton& notify);

		bool m_ShowImportPopup = false;
		bool m_ShowNewProjectPopup = false;
		bool m_ShowShortcuts = false;
		bool m_ShowProjectSettings = false;
		char m_ImportPathBuffer[512] = {};
		char m_NewProjectNameBuffer[128] = {};
		char m_NewProjectLocationBuffer[512] = {};
		char m_NewLayerNameBuffer[64] = {};
	};
}
