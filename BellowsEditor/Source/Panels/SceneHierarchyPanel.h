#pragma once

#include "Bellows/Scene/Entity.h"

#include <string>

namespace Bellows {

	class EditorLayer;

	// Tree of the active scene's entities with selection, drag-to-reparent, renaming and creation.
	class SceneHierarchyPanel
	{
	public:
		explicit SceneHierarchyPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		// The panel is drawn as a closable tab; closing it clears *open (View > Panels shows it again).
		void OnImGuiRender(bool* open);
		bool IsFocused() const { return m_Focused; }

		// The "create entity" menu, shared with the main menu bar. New entities become children of 'parent'.
		void DrawCreateMenu(Entity parent);
	private:
		void DrawEntityNode(Entity entity);
		bool MatchesFilter(Entity entity) const;
	private:
		EditorLayer& m_Editor;
		std::string m_Filter;
		UUID m_RenamingEntity = 0;
		std::string m_RenameBuffer;
		bool m_RenameFocusPending = false;
		bool m_Focused = false;
	};

}
