#pragma once

namespace Bellows {

	class EditorLayer;

	// Scene (physics, lighting, rendering), project (export) and editor preferences.
	class SettingsPanel
	{
	public:
		explicit SettingsPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		void OnImGuiRender();
	private:
		void DrawSceneSettings();
		void DrawProjectSettings();
		void DrawEditorSettings();
	private:
		EditorLayer& m_Editor;
		// Files are written when the user finishes an edit, not on every frame of a drag.
		bool m_ProjectDirty = false;
		bool m_SettingsDirty = false;
	};

}
