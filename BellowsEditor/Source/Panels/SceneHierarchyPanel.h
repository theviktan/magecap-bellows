#pragma once

#include "Bellows/Scene/Entity.h"

#include <imgui.h>

#include <string>
#include <vector>

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
		// One line of the tree as it shows now.
		struct Row
		{
			Entity Target;
			int Depth = 0;
			bool HasChildren = false;
		};

		// The ID ImGui keeps an entity's tree node (and whether it is open) under, in this window.
		static ImGuiID GetNodeID(UUID uuid);
		void CollectRows(Entity entity, int depth);
		bool CollectMatchingRows(Entity entity, int depth, const std::string& filter);
		void DrawEntityRow(const Row& row);
	private:
		EditorLayer& m_Editor;
		std::vector<Row> m_Rows;
		std::string m_Filter;
		UUID m_RenamingEntity = 0;
		std::string m_RenameBuffer;
		bool m_RenameFocusPending = false;
		bool m_Focused = false;
	};

}
