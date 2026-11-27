#pragma once

#include "HierarchyLogic.h"

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

		// How the panel lists the scene: the groups and hierarchy, or every entity by kind or by tag.
		enum class View : uint8_t { Groups, Type, Tag };
		View GetView() const { return m_View; }
		void SetView(View view) { m_View = view; }
		const std::string& GetSearch() const { return m_Filter; }
		void SetSearch(std::string search) { m_Filter = std::move(search); }
		// Opens Organize's preview of the moves it suggests.
		void OpenOrganize();

		// One line of the tree as the panel last drew it (editor automation reads these).
		enum class RowKind : uint8_t { Entity, Group, Note, Ungrouped, Section, Heading, Stack };
		struct Row
		{
			RowKind Kind = RowKind::Entity;
			Entity Target;				// Entity rows; a Stack row's first entity
			UUID Group = 0;				// Group and Note rows: the group; Entity rows: the group it shows under
			int Depth = 0;
			bool HasChildren = false;
			std::string Label;			// Section and Heading rows; a Stack row's shared name
			size_t Count = 0;			// Heading and Stack rows: how many entities they hold
			int Section = 0;			// Tells apart rows of one entity listed twice (Pinned, the By tag view)
		};
		const std::vector<Row>& GetRows() const { return m_Rows; }
	private:
		// The ID ImGui keeps an entity's tree node (and whether it is open) under, in this window.
		static ImGuiID GetNodeID(UUID uuid);
		void DrawToolbar();
		void CollectRows(Scene& scene);
		void CollectGroupsView(Scene& scene, const HierarchyLogic::Filter& filter);
		void CollectListView(Scene& scene, const HierarchyLogic::Filter& filter);
		// Siblings in order; runs of numbered names ("Crate 1", "Crate 2", ...) fold into a stack row.
		void CollectSiblings(const std::vector<Entity>& siblings, int depth, UUID group, const HierarchyLogic::Filter* filter);
		void CollectRows(Entity entity, int depth, UUID group);
		bool CollectMatchingRows(Entity entity, int depth, UUID group, const HierarchyLogic::Filter& filter);
		void DrawEntityRow(const Row& row);
		void DrawGroupRow(const Row& row);
		void DrawNoteRow(const Row& row);
		void DrawUngroupedRow();
		void DrawSectionRow(const Row& row);
		void DrawHeadingRow(const Row& row);
		void DrawStackRow(const Row& row);
		void DrawOrganize();
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

		View m_View = View::Groups;
		bool m_Searching = false;					// The search or a chip narrows the rows (all open)
		// The filter chips: each adds a "has:" to the search.
		bool m_ChipScript = false, m_ChipLights = false, m_ChipPhysics = false, m_ChipTagged = false;
		std::unordered_set<UUID> m_ClosedGroups;
		std::unordered_set<std::string> m_ClosedHeadings;
		std::unordered_set<UUID> m_OpenStacks;		// By the first entity of the stack
		bool m_OrganizeOpenPending = false;
		std::vector<HierarchyLogic::OrganizeMove> m_OrganizeMoves;
		UUID m_SettingsGroup = 0;			// The group the settings popup edits
		bool m_SettingsOpenPending = false;
		std::string m_GroupNameBuffer;
		std::string m_GroupNoteBuffer;
		std::string m_GroupNameError;
	};

}
