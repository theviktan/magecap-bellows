#pragma once

#include "Bellows/Scene/Entity.h"

#include <imgui.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace Bellows {

	class EditorLayer;

	// Tree of the active scene's entities with selection, drag-to-reparent, renaming and creation.
	// Top-level entities are shown under the scene's groups (Scene/SceneGroups.h), each a folder with an
	// icon, a color and a note; entities in no group follow them.
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

		// One line of the tree as the panel last drew it (editor automation reads these).
		enum class RowKind : uint8_t { Entity, Group, Note, Ungrouped };
		struct Row
		{
			RowKind Kind = RowKind::Entity;
			Entity Target;				// Entity rows
			UUID Group = 0;				// Group and Note rows: the group; Entity rows: the group it shows under
			int Depth = 0;
			bool HasChildren = false;
		};
		const std::vector<Row>& GetRows() const { return m_Rows; }
	private:
		// The ID ImGui keeps an entity's tree node (and whether it is open) under, in this window.
		static ImGuiID GetNodeID(UUID uuid);
		void CollectRows(Scene& scene);
		void CollectRows(Entity entity, int depth, UUID group);
		bool CollectMatchingRows(Entity entity, int depth, UUID group, const std::string& filter);
		void DrawEntityRow(const Row& row);
		void DrawGroupRow(const Row& row);
		void DrawNoteRow(const Row& row);
		void DrawUngroupedRow();
		void DrawMoveToGroupMenu();
		void DrawGroupSettings();
		void OpenGroupSettings(UUID group);
		// Files the top-level entities among 'entities' under 'group' (0 for none) as one undo step; an
		// entity with a parent moves to the top first when 'moveChildren' is set, and is skipped otherwise.
		void MoveToGroup(const std::vector<Entity>& entities, UUID group, bool moveChildren);
		UUID CreateGroup();
	private:
		EditorLayer& m_Editor;
		std::vector<Row> m_Rows;
		std::string m_Filter;
		UUID m_RenamingEntity = 0;
		std::string m_RenameBuffer;
		bool m_RenameFocusPending = false;
		bool m_Focused = false;

		std::unordered_set<UUID> m_ClosedGroups;
		UUID m_SettingsGroup = 0;			// The group the settings popup edits
		bool m_SettingsOpenPending = false;
		std::string m_GroupNameBuffer;
		std::string m_GroupNoteBuffer;
		std::string m_GroupNameError;
	};

}
