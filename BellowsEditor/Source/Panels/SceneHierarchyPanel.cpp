#include "SceneHierarchyPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ScriptEditorPanel.h"
#include "UITemplates.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Scene/SceneGroups.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace Bellows {

	namespace {

		std::string ToLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		const char* EntityIcon(Entity entity)
		{
			if (entity.HasComponent<CameraComponent>()) return ICON_CAMERA;
			if (entity.HasComponent<DirectionalLightComponent>()) return ICON_SUN;
			if (entity.HasComponent<PointLightComponent>()) return ICON_LIGHTBULB;
			if (entity.HasComponent<SpotLightComponent>()) return ICON_FLASHLIGHT;
			if (entity.HasComponent<SkyLightComponent>()) return ICON_CLOUD;
			if (entity.HasComponent<PrefabComponent>()) return ICON_PACKAGE;
			if (entity.HasComponent<NavAgentComponent>()) return ICON_FOOTPRINTS;
			if (entity.HasComponent<AnimatorComponent>()) return ICON_PERSON_STANDING;
			if (entity.HasComponent<MeshRendererComponent>()) return ICON_BOX;
			if (entity.HasComponent<ParticleEmitterComponent>()) return ICON_FLAME;
			if (entity.HasComponent<TrailComponent>()) return ICON_SPLINE;
			if (entity.HasComponent<DecalComponent>()) return ICON_STICKER;
			if (entity.HasComponent<AudioSourceComponent>()) return ICON_VOLUME_2;
			if (entity.HasComponent<UICanvasComponent>()) return ICON_MONITOR;
			if (entity.HasComponent<TextComponent>()) return ICON_TYPE;
			if (entity.HasComponent<UIButtonComponent>()) return ICON_MOUSE_POINTER_CLICK;
			if (entity.HasComponent<UIToggleComponent>()) return ICON_SQUARE_CHECK;
			if (entity.HasComponent<UISliderComponent>()) return ICON_SLIDERS_HORIZONTAL;
			if (entity.HasComponent<UIDropdownComponent>()) return ICON_CHEVRONS_UP_DOWN;
			if (entity.HasComponent<UITextInputComponent>()) return ICON_TEXT_CURSOR_INPUT;
			if (entity.HasComponent<UIScrollComponent>()) return ICON_LAYERS;
			if (entity.HasComponent<UILayoutComponent>()) return ICON_LAYOUT_GRID;
			if (entity.HasComponent<UIImageComponent>()) return ICON_IMAGE;
			if (entity.HasComponent<UIRectComponent>()) return ICON_SCAN;
			if (entity.HasComponent<ScriptComponent>()) return ICON_FILE_CODE;
			if (entity.HasComponent<RigidBodyComponent>()) return ICON_ATOM;
			return ICON_CIRCLE;
		}

		// The glyph for each of SceneGroups::GetIcons(), in the same order.
		const char* GroupIconGlyph(const std::string& icon)
		{
			static const std::unordered_map<std::string, const char*> s_Glyphs = {
				{ "folder", ICON_FOLDER }, { "mountain", ICON_MOUNTAIN }, { "anchor", ICON_ANCHOR }, { "rocket", ICON_ROCKET },
				{ "lightbulb", ICON_LIGHTBULB }, { "layout", ICON_LAYOUT }, { "music", ICON_MUSIC }, { "camera", ICON_CAMERA },
				{ "box", ICON_BOX }, { "user", ICON_USER }, { "users", ICON_USERS }, { "flag", ICON_FLAG },
				{ "star", ICON_STAR }, { "swords", ICON_SWORDS }, { "trees", ICON_TREES }, { "house", ICON_HOUSE },
			};
			auto it = s_Glyphs.find(icon);
			return it != s_Glyphs.end() ? it->second : ICON_FOLDER;
		}

		// The colors offered for a group's icon (MageCap's palette); scripts can set any color.
		struct GroupColor { const char* Name; glm::vec3 Value; };
		constexpr GroupColor c_GroupColors[] = {
			{ "Gray", { 0.553f, 0.580f, 0.690f } }, { "Blue", { 0.431f, 0.545f, 1.0f } }, { "Green", { 0.247f, 0.812f, 0.494f } },
			{ "Yellow", { 0.949f, 0.757f, 0.306f } }, { "Sand", { 0.949f, 0.651f, 0.353f } }, { "Purple", { 0.714f, 0.549f, 0.961f } },
			{ "Pink", { 0.780f, 0.482f, 0.839f } }, { "Red", { 0.898f, 0.329f, 0.310f } },
		};

		ImU32 ToColor(const glm::vec3& color, float alpha = 1.0f)
		{
			return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, alpha));
		}

		// Small gray icons at the end of an entity's row for what it has beyond the icon it shows.
		struct Badge { const char* Glyph; const char* Name; };
		std::vector<Badge> EntityBadges(Entity entity)
		{
			std::vector<Badge> badges;
			// Compared as text: the same literal in two places isn't always one pointer.
			const std::string_view shown = EntityIcon(entity);
			auto add = [&](bool has, const char* glyph, const char* name)
			{
				if (has && shown != glyph)
					badges.push_back({ glyph, name });
			};
			add(entity.HasComponent<RigidBodyComponent>(), ICON_ATOM, "Physics body");
			add(entity.HasComponent<AudioSourceComponent>(), ICON_VOLUME_2, "Sound");
			// A light's own icon already says it is one.
			const bool showsLight = shown == std::string_view(ICON_SUN) || shown == std::string_view(ICON_LIGHTBULB) || shown == std::string_view(ICON_FLASHLIGHT);
			add(!showsLight && (entity.HasComponent<PointLightComponent>() || entity.HasComponent<SpotLightComponent>()), ICON_LIGHTBULB, "Light");
			add(entity.HasComponent<AnimatorComponent>(), ICON_PERSON_STANDING, "Animation");
			add(entity.HasComponent<NavAgentComponent>(), ICON_FOOTPRINTS, "Walks the navmesh");
			add(entity.HasComponent<ParticleEmitterComponent>(), ICON_FLAME, "Particles");
			add(entity.HasComponent<TrailComponent>(), ICON_SPLINE, "Trail");
			add(entity.HasComponent<DecalComponent>(), ICON_STICKER, "Decal");
			return badges;
		}

		// A top-level entity's group, or 0 for none or a group the scene doesn't have.
		UUID ShownGroup(Scene& scene, Entity root)
		{
			const UUID group = root.GetComponent<TagComponent>().Group;
			return scene.FindGroup(group) ? group : UUID(0);
		}

		ImU32 EntityIconColor(Entity entity)
		{
			if (entity.HasComponent<CameraComponent>()) return IM_COL32(200, 205, 215, 255);
			if (entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>()
				|| entity.HasComponent<SpotLightComponent>()) return IM_COL32(250, 210, 90, 255);
			if (entity.HasComponent<SkyLightComponent>()) return IM_COL32(140, 190, 245, 255);
			if (entity.HasComponent<PrefabComponent>()) return Theme::Info;
			if (entity.HasComponent<MeshRendererComponent>()) return IM_COL32(150, 200, 255, 255);
			return Theme::TextMuted;
		}

	}

	void SceneHierarchyPanel::OnImGuiRender(bool* open)
	{
		if (!UI::BeginPanel("Hierarchy", open))
		{
			m_Focused = false;
			ImGui::End();
			return;
		}
		m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		if (!scene)
		{
			UI::CenteredText("No scene");
			ImGui::End();
			return;
		}

		DrawToolbar();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f * Theme::GetScale()));
		ImGui::BeginChild("##Tree");
		// Only the rows on screen are drawn, so a scene with tens of thousands of entities stays as fast
		// as a small one. The rows that are showing (inside open nodes, or matching the search) are
		// listed first; that walk skips closed subtrees.
		// Every row is one frame high, so the clipper can place the ones it skips.
		CollectRows(*scene);
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_Rows.size()));
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const Row& row = m_Rows[static_cast<size_t>(i)];
				switch (row.Kind)
				{
					case RowKind::Entity:		DrawEntityRow(row); break;
					case RowKind::Group:		DrawGroupRow(row); break;
					case RowKind::Note:			DrawNoteRow(row); break;
					case RowKind::Ungrouped:	DrawUngroupedRow(); break;
					case RowKind::Section:		DrawSectionRow(row); break;
					case RowKind::Heading:		DrawHeadingRow(row); break;
					case RowKind::Stack:		DrawStackRow(row); break;
				}
			}
		}
		clipper.End();

		// F2 renames the selected entity, wherever its row is scrolled to.
		if (Entity selected = m_Editor.GetPrimarySelection(); selected && m_Editor.GetSelection().size() == 1
			&& ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
		{
			m_RenamingEntity = selected.GetUUID();
			m_RenameBuffer = selected.GetName();
			m_RenameFocusPending = true;
		}

		// Dropping on empty space moves an entity to the root.
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::EntityPayload))
			{
				if (Entity dragged = scene->TryGetEntityByUUID(*static_cast<const uint64_t*>(payload->Data)))
				{
					scene->Unparent(dragged, true);
					m_Editor.CommitChange("Unparent");
				}
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::AssetPayload))
			{
				const std::string path(static_cast<const char*>(payload->Data));
				if (AssetManager::GetAssetType(path) == AssetType::Prefab)
					m_Editor.InstantiatePrefab(path, glm::vec3(0.0f));
				else if (AssetManager::GetAssetType(path) == AssetType::Mesh)
					m_Editor.CreateMeshEntity(path, glm::vec3(0.0f));
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_Editor.ClearSelection();
		if (ImGui::BeginPopupContextItem("##EmptyContext"))
		{
			DrawCreateMenu({});
			ImGui::EndPopup();
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		DrawGroupSettings();
		DrawOrganize();

		if (scene->GetEntityCount() == 0 && scene->GetGroups().empty())
			UI::CenteredText("The scene is empty.\nRight-click to create an entity.");

		ImGui::End();
	}

	ImGuiID SceneHierarchyPanel::GetNodeID(UUID uuid)
	{
		char id[24];
		std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(static_cast<uint64_t>(uuid)));
		ImGui::PushID(id);
		const ImGuiID nodeID = ImGui::GetID("##node");
		ImGui::PopID();
		return nodeID;
	}

	void SceneHierarchyPanel::DrawToolbar()
	{
		// Search, Organize, new group and create
		const float scale = Theme::GetScale();
		const float buttonWidth = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 3.0f * (buttonWidth + ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Search", &m_Filter);
		UI::Tooltip("Words match names. tag:enemy needs a tag, group:docking a group, and has: a kind of component: "
			"script, light, camera, physics, audio, mesh, ui, animation, nav, or tag for any tag.");
		ImGui::SameLine();
		if (UI::IconButton(ICON_SPARKLES, "Organize: suggests groups for lights, cameras, UI and sound, and shows the moves before making them"))
			OpenOrganize();
		ImGui::SameLine();
		if (UI::IconButton(ICON_FOLDER_PLUS, "New group: a folder to file top-level entities under"))
			OpenGroupSettings(CreateGroup());
		ImGui::SameLine();
		if (UI::IconButton(ICON_PLUS, "Create entity"))
			ImGui::OpenPopup("##CreateEntity");
		if (ImGui::BeginPopup("##CreateEntity"))
		{
			DrawCreateMenu({});
			ImGui::EndPopup();
		}

		// The view: groups and hierarchy, or every entity by kind or by tag.
		const char* views[] = { "Groups", "By type", "By tag" };
		const float viewWidth = (ImGui::GetContentRegionAvail().x - 2.0f * 2.0f * scale) / 3.0f;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * scale, ImGui::GetStyle().ItemSpacing.y));
		for (int i = 0; i < 3; i++)
		{
			if (i > 0)
				ImGui::SameLine();
			const bool active = static_cast<int>(m_View) == i;
			ImGui::PushStyleColor(ImGuiCol_Button, active ? Theme::ToVec4(Theme::PanelRaised) : Theme::ToVec4(Theme::Field));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(active ? Theme::Text : Theme::TextMuted));
			if (ImGui::Button(views[i], ImVec2(viewWidth, 0.0f)))
				m_View = static_cast<View>(i);
			ImGui::PopStyleColor(2);
		}
		ImGui::PopStyleVar();

		// Filter chips, each a "has:" on top of the search; they wrap when the panel is narrow.
		const float chipPadding = 9.0f * scale;
		const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		bool firstChip = true;
		auto chip = [&](const char* label, bool& on, const char* tooltip)
		{
			const float width = ImGui::CalcTextSize(label).x + 2.0f * chipPadding;
			if (!firstChip)
			{
				ImGui::SameLine(0.0f, 4.0f * scale);
				if (ImGui::GetCursorPosX() + width > rightEdge)
					ImGui::NewLine();
			}
			firstChip = false;
			ImGui::PushStyleColor(ImGuiCol_Button, on ? Theme::ToVec4(Theme::PanelRaised) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_Border, Theme::ToVec4(on ? Theme::BorderStrong : Theme::Border));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(on ? Theme::Text : Theme::TextMuted));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 99.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(chipPadding, 2.0f * scale));
			if (ImGui::Button(label))
				on = !on;
			ImGui::PopStyleVar(3);
			ImGui::PopStyleColor(3);
			UI::Tooltip(tooltip);
		};
		chip("Has script", m_ChipScript, "Only entities with a script");
		chip("Lights", m_ChipLights, "Only lights and skies");
		chip("Physics", m_ChipPhysics, "Only physics bodies");
		chip("Tagged", m_ChipTagged, "Only entities with a tag");

		const HierarchyLogic::Filter filter = HierarchyLogic::ParseFilter(m_Filter);
		if (!filter.Unknown.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
			ImGui::TextWrapped("has:%s isn't a kind the search knows: script, light, camera, physics, audio, mesh, ui, animation, nav, tag",
				filter.Unknown.front().c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
	}

	void SceneHierarchyPanel::CollectRows(Scene& scene)
	{
		m_Rows.clear();
		HierarchyLogic::Filter filter = HierarchyLogic::ParseFilter(m_Filter);
		if (m_ChipScript) filter.Has.push_back("script");
		if (m_ChipLights) filter.Has.push_back("light");
		if (m_ChipPhysics) filter.Has.push_back("physics");
		if (m_ChipTagged) filter.Has.push_back("tag");
		m_Searching = filter.IsActive();
		if (m_View == View::Groups)
			CollectGroupsView(scene, filter);
		else
			CollectListView(scene, filter);
	}

	void SceneHierarchyPanel::CollectGroupsView(Scene& scene, const HierarchyLogic::Filter& filter)
	{
		const HierarchyLogic::Filter* search = filter.IsActive() ? &filter : nullptr;

		// Pinned entities, wherever they are, listed at the top.
		std::vector<Entity> pinned;
		for (Entity entity : scene.GetAllEntities())
		{
			if (entity.GetComponent<TagComponent>().Pinned && (!search || HierarchyLogic::Matches(scene, entity, filter)))
				pinned.push_back(entity);
		}
		if (!pinned.empty())
		{
			m_Rows.push_back({ RowKind::Section, {}, 0, 0, false, "PINNED" });
			for (Entity entity : pinned)
			{
				Row row { RowKind::Entity, entity, ShownGroup(scene, entity), 0, false };
				row.Section = 1;
				m_Rows.push_back(row);
			}
			m_Rows.push_back({ RowKind::Section, {}, 0, 0, false, "SCENE" });
		}

		const std::vector<Entity> roots = scene.GetRootEntities();
		if (scene.GetGroups().empty())
		{
			CollectSiblings(roots, 0, 0, search);
			return;
		}

		// Each group with its entities (while searching, only groups with a match), then those in none.
		for (const SceneGroup& group : scene.GetGroups())
		{
			const size_t header = m_Rows.size();
			m_Rows.push_back({ RowKind::Group, {}, group.ID });
			const bool open = search || !m_ClosedGroups.contains(group.ID);
			if (open && !search && !group.Note.empty())
				m_Rows.push_back({ RowKind::Note, {}, group.ID });
			const size_t members = m_Rows.size();
			std::vector<Entity> inGroup;
			for (Entity root : roots)
			{
				if (ShownGroup(scene, root) == group.ID)
					inGroup.push_back(root);
			}
			m_Rows[header].HasChildren = !inGroup.empty();
			if (open)
				CollectSiblings(inGroup, 1, group.ID, search);
			if (search && m_Rows.size() == members)
				m_Rows.resize(header);
		}
		const size_t divider = m_Rows.size();
		m_Rows.push_back({ RowKind::Ungrouped });
		std::vector<Entity> loose;
		for (Entity root : roots)
		{
			if (!ShownGroup(scene, root).IsValid())
				loose.push_back(root);
		}
		CollectSiblings(loose, 0, 0, search);
		// The divider shows while searching only above a match.
		if (search && m_Rows.size() == divider + 1)
			m_Rows.resize(divider);
	}

	void SceneHierarchyPanel::CollectListView(Scene& scene, const HierarchyLogic::Filter& filter)
	{
		// Every entity, flat, under a heading for its kind or for each of its tags.
		std::vector<std::pair<std::string, std::vector<Entity>>> sections;
		auto sectionFor = [&](const std::string& name) -> std::vector<Entity>&
		{
			for (auto& [label, entities] : sections)
			{
				if (label == name)
					return entities;
			}
			return sections.emplace_back(name, std::vector<Entity>()).second;
		};
		const std::vector<Entity> all = scene.GetAllEntities();
		if (m_View == View::Type)
		{
			for (int kind = 0; kind <= static_cast<int>(HierarchyLogic::EntityKind::Other); kind++)
				sectionFor(HierarchyLogic::KindName(static_cast<HierarchyLogic::EntityKind>(kind)));
			for (Entity entity : all)
				sectionFor(HierarchyLogic::KindName(HierarchyLogic::Classify(entity))).push_back(entity);
		}
		else
		{
			// Tags in alphabetical order (any letter case), then the entities with none.
			std::vector<std::string> tags;
			for (Entity entity : all)
			{
				for (const std::string& tag : entity.GetComponent<TagComponent>().Tags)
				{
					if (std::find(tags.begin(), tags.end(), tag) == tags.end())
						tags.push_back(tag);
				}
			}
			std::sort(tags.begin(), tags.end(), [](const std::string& a, const std::string& b) { return ToLower(a) < ToLower(b); });
			for (const std::string& tag : tags)
				sectionFor(tag);
			for (Entity entity : all)
			{
				const std::vector<std::string>& entityTags = entity.GetComponent<TagComponent>().Tags;
				for (const std::string& tag : entityTags)
					sectionFor(tag).push_back(entity);
				if (entityTags.empty())
					sectionFor("No tags").push_back(entity);
			}
		}

		const bool searching = filter.IsActive();
		int section = 2;
		for (auto& [label, entities] : sections)
		{
			section++;
			std::vector<Entity> shown;
			for (Entity entity : entities)
			{
				if (!searching || HierarchyLogic::Matches(scene, entity, filter))
					shown.push_back(entity);
			}
			if (shown.empty())
				continue;
			const std::string key = (m_View == View::Type ? "type:" : "tag:") + label;
			Row heading { RowKind::Heading, {}, 0, 0, true, key };
			heading.Count = shown.size();
			m_Rows.push_back(heading);
			if (!searching && m_ClosedHeadings.contains(key))
				continue;
			for (Entity entity : shown)
			{
				Row row { RowKind::Entity, entity, ShownGroup(scene, entity), 1, false };
				row.Section = section;
				m_Rows.push_back(row);
			}
		}
	}

	void SceneHierarchyPanel::CollectSiblings(const std::vector<Entity>& siblings, int depth, UUID group, const HierarchyLogic::Filter* filter)
	{
		if (filter)
		{
			for (Entity sibling : siblings)
			{
				if (sibling)
					CollectMatchingRows(sibling, depth, group, *filter);
			}
			return;
		}
		size_t i = 0;
		while (i < siblings.size())
		{
			const std::string name = siblings[i] ? HierarchyLogic::StackName(siblings[i].GetName()) : std::string();
			size_t end = i + 1;
			while (!name.empty() && end < siblings.size() && siblings[end] && HierarchyLogic::StackName(siblings[end].GetName()) == name)
				end++;
			if (end - i >= HierarchyLogic::MinStackSize)
			{
				Row stack { RowKind::Stack, siblings[i], group, depth, true, name };
				stack.Count = end - i;
				m_Rows.push_back(stack);
				if (m_OpenStacks.contains(siblings[i].GetUUID()))
				{
					for (size_t k = i; k < end; k++)
						CollectRows(siblings[k], depth + 1, group);
				}
				i = end;
				continue;
			}
			if (siblings[i])
				CollectRows(siblings[i], depth, group);
			i++;
		}
	}

	void SceneHierarchyPanel::CollectRows(Entity entity, int depth, UUID group)
	{
		const std::vector<Entity> children = entity.GetChildren();
		m_Rows.push_back({ RowKind::Entity, entity, group, depth, !children.empty() });
		// The tree node keeps whether it is open in the window's storage, under its ID.
		if (children.empty() || ImGui::GetStateStorage()->GetInt(GetNodeID(entity.GetUUID()), 0) == 0)
			return;
		CollectSiblings(children, depth + 1, group, nullptr);
	}

	bool SceneHierarchyPanel::CollectMatchingRows(Entity entity, int depth, UUID group, const HierarchyLogic::Filter& filter)
	{
		// An entity shows when it matches or something under it does, with every row open.
		const size_t index = m_Rows.size();
		const std::vector<Entity> children = entity.GetChildren();
		m_Rows.push_back({ RowKind::Entity, entity, group, depth, !children.empty() });
		bool matched = HierarchyLogic::Matches(*entity.GetScene(), entity, filter);
		for (Entity child : children)
		{
			if (child)
				matched |= CollectMatchingRows(child, depth + 1, group, filter);
		}
		if (!matched)
			m_Rows.resize(index);
		return matched;
	}

	void SceneHierarchyPanel::DrawEntityRow(const Row& row)
	{
		Entity entity = row.Target;
		if (!entity)
			return;

		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		const UUID uuid = entity.GetUUID();
		const bool selected = m_Editor.IsSelected(entity);

		// Rows are drawn flat (no tree push), indented by their depth.
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanFullWidth
			| ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (selected)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (!row.HasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (m_Searching && row.Section == 0)
			ImGui::SetNextItemOpen(true);

		const float indent = static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing;
		if (indent > 0.0f)
			ImGui::Indent(indent);
		// An entity listed twice (Pinned, or under two tags) gets one ID per place it is listed.
		if (row.Section != 0)
			ImGui::PushID(row.Section);
		char id[24];
		std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(static_cast<uint64_t>(uuid)));
		ImGui::PushID(id);
		const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		ImGui::TreeNodeEx("##node", flags);
		ImGui::PopStyleVar();
		if (selected)
			ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Theme::SelectionRing, ImGui::GetStyle().FrameRounding);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
			m_Editor.Select(entity, ImGui::GetIO().KeyCtrl);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !row.HasChildren)
			m_Editor.FocusSelection();

		// Drag source / target for re-parenting
		if (ImGui::BeginDragDropSource())
		{
			const uint64_t payload = static_cast<uint64_t>(uuid);
			ImGui::SetDragDropPayload(UI::EntityPayload, &payload, sizeof(payload));
			ImGui::Text("%s  %s", EntityIcon(entity), entity.GetName().c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::EntityPayload))
			{
				Entity dragged = scene->TryGetEntityByUUID(*static_cast<const uint64_t*>(payload->Data));
				if (dragged && dragged != entity && scene->SetParent(dragged, entity, true))
					m_Editor.CommitChange("Reparent");
			}
			// Dropping a script from the Content panel attaches it, like dropping a script on a node in Godot.
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::AssetPayload))
			{
				const std::string path(static_cast<const char*>(payload->Data));
				if (AssetManager::GetAssetType(path) == AssetType::Script)
					m_Editor.AttachScript(entity, path);
			}
			ImGui::EndDragDropTarget();
		}

		// Label: icon + name (or rename field)
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(EntityIconColor(entity)));
		ImGui::TextUnformatted(EntityIcon(entity));
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (m_RenamingEntity == uuid)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (m_RenameFocusPending)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocusPending = false;
			}
			if (ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				if (!m_RenameBuffer.empty())
					entity.SetName(m_RenameBuffer);
				m_RenamingEntity = 0;
			}
			if (ImGui::IsItemDeactivated())
				m_RenamingEntity = 0;
		}
		else
		{
			const bool pending = scene->IsPendingDestroy(entity);
			const bool hidden = m_Editor.GetSceneState() == SceneState::Edit && scene->IsHiddenInEditor(entity);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(pending || hidden ? Theme::TextDisabled : (selected ? Theme::SelectionText : Theme::Text)));
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::PopStyleColor();
			// The flat views say which group an entity is in.
			if (m_View != View::Groups)
			{
				if (const SceneGroup* group = scene->FindGroup(row.Group))
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextDisabled));
					ImGui::Text("in %s", group->Name.c_str());
					ImGui::PopStyleColor();
				}
			}
		}

		// Badges at the end of the row say what else the entity has; the script badge opens its script.
		const float badgeWidth = ImGui::GetFrameHeight();
		const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>();
		const bool hasScript = script && !script->ScriptPath.empty();
		const std::vector<Badge> badges = EntityBadges(entity);
		float badgeX = rowRight - (hasScript ? badgeWidth : 0.0f);
		for (auto it = badges.rbegin(); it != badges.rend(); ++it)
		{
			const float glyphWidth = ImGui::CalcTextSize(it->Glyph).x;
			badgeX -= glyphWidth + 6.0f * Theme::GetScale();
			ImGui::SameLine(badgeX);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Neutral));
			ImGui::TextUnformatted(it->Glyph);
			ImGui::PopStyleColor();
			UI::Tooltip(it->Name);
		}
		if (hasScript)
		{
			ImGui::SameLine(rowRight - badgeWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::FieldHovered));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::NeutralBright));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 3.0f));	// Matches the tree row
			if (ImGui::Button(ICON_FILE_CODE "##script", ImVec2(badgeWidth, 0.0f)))
				m_Editor.GetScriptEditor().OpenScript(script->ScriptPath);
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
			const std::string tooltip = "Open " + script->ScriptPath;
			UI::Tooltip(tooltip.c_str());
		}

		if (ImGui::BeginPopupContextItem("##EntityContext"))
		{
			if (!selected)
				m_Editor.Select(entity);
			if (ImGui::MenuItem(ICON_PENCIL "  Rename", "F2"))
			{
				m_RenamingEntity = uuid;
				m_RenameBuffer = entity.GetName();
				m_RenameFocusPending = true;
			}
			if (ImGui::MenuItem(ICON_COPY "  Duplicate", "Ctrl+D"))
				m_Editor.DuplicateSelection();
			if (ImGui::MenuItem(ICON_FOCUS "  Focus", "F"))
				m_Editor.FocusSelection();
			if (ImGui::BeginMenu(ICON_PLUS "  Create Child"))
			{
				DrawCreateMenu(entity);
				ImGui::EndMenu();
			}
			if (entity.GetParent() && ImGui::MenuItem(ICON_ARROW_UP "  Move to Root"))
			{
				scene->Unparent(entity, true);
				m_Editor.CommitChange("Unparent");
			}
			if (ImGui::BeginMenu(ICON_FOLDER "  Move to Group"))
			{
				DrawMoveToGroupMenu();
				ImGui::EndMenu();
			}
			TagComponent& tag = entity.GetComponent<TagComponent>();
			if (ImGui::MenuItem(tag.Pinned ? ICON_PIN_OFF "  Unpin" : ICON_PIN "  Pin to Top"))
			{
				tag.Pinned = !tag.Pinned;
				m_Editor.CommitChange(tag.Pinned ? "Pin entity" : "Unpin entity");
			}
			ImGui::Separator();
			const bool editing = m_Editor.GetSceneState() == SceneState::Edit;
			if (ImGui::MenuItem(ICON_PAPERCLIP "  Attach Script...", nullptr, false, editing))
				m_Editor.GetScriptEditor().ShowAttachDialog(entity);
			if (hasScript)
			{
				if (ImGui::MenuItem(ICON_FILE_CODE "  Open Script"))
					m_Editor.GetScriptEditor().OpenScript(script->ScriptPath);
				if (ImGui::MenuItem(ICON_UNLINK "  Detach Script", nullptr, false, editing))
					m_Editor.DetachScript(entity);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_PACKAGE "  Save as Prefab"))
			{
				const std::string path = "Prefabs/" + entity.GetName() + ".bprefab";
				m_Editor.SavePrefab(entity, path);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_TRASH_2 "  Delete", "Del"))
				m_Editor.DeleteSelection();
			ImGui::EndPopup();
		}

		ImGui::PopID();
		if (row.Section != 0)
			ImGui::PopID();
		if (indent > 0.0f)
			ImGui::Unindent(indent);
	}

	void SceneHierarchyPanel::DrawGroupRow(const Row& row)
	{
		Scene& scene = *m_Editor.GetActiveScene();
		const SceneGroup* group = scene.FindGroup(row.Group);
		if (!group)
			return;
		const UUID id = group->ID;
		const float scale = Theme::GetScale();

		char label[24];
		std::snprintf(label, sizeof(label), "g%016llx", static_cast<unsigned long long>(static_cast<uint64_t>(id)));
		ImGui::PushID(label);
		const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		const bool open = !m_Filter.empty() || !m_ClosedGroups.contains(id);
		ImGui::SetNextItemOpen(open);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap
			| ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (!row.HasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::ToVec4(Theme::Hover));
		ImGui::TreeNodeEx("##group", flags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
		if (ImGui::IsItemToggledOpen() && m_Filter.empty())
		{
			if (open)
				m_ClosedGroups.insert(id);
			else
				m_ClosedGroups.erase(id);
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenGroupSettings(id);
		if (open && row.HasChildren)
			ImGui::GetWindowDrawList()->AddRectFilled(rowMin, rowMax, Theme::WithAlpha(Theme::PanelRaised, 150), Theme::FrameRadius * scale);

		// Dropping an entity here files it under the group (a child first moves to the top).
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::EntityPayload))
			{
				if (Entity dragged = scene.TryGetEntityByUUID(*static_cast<const uint64_t*>(payload->Data)))
					MoveToGroup({ dragged }, id, true);
			}
			ImGui::EndDragDropTarget();
		}

		// Icon on a tile of its color, the name, and how many top-level entities it holds.
		ImGui::SameLine(0.0f, 2.0f);
		const float tile = ImGui::GetFrameHeight() - 6.0f * scale;
		const ImVec2 tileMin = ImGui::GetCursorScreenPos() + ImVec2(0.0f, (ImGui::GetFrameHeight() - tile) * 0.5f);
		ImGui::GetWindowDrawList()->AddRectFilled(tileMin, tileMin + ImVec2(tile, tile), ToColor(group->Color, 0.18f), 5.0f * scale);
		const char* glyph = GroupIconGlyph(group->Icon);
		const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
		ImGui::GetWindowDrawList()->AddText(tileMin + ImVec2((tile - glyphSize.x) * 0.5f, (tile - glyphSize.y) * 0.5f), ToColor(group->Color), glyph);
		ImGui::Dummy(ImVec2(tile, ImGui::GetFrameHeight()));
		ImGui::SameLine(0.0f, 7.0f * scale);
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(group->HiddenInEditor ? Theme::TextMuted : Theme::Text));
		ImGui::TextUnformatted(group->Name.c_str());
		ImGui::PopStyleColor();
		ImGui::PopFont();
		size_t count = 0;
		for (Entity root : scene.GetRootEntities())
			count += ShownGroup(scene, root) == id ? 1 : 0;
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextDisabled));
		ImGui::Text("%zu", count);
		ImGui::PopStyleColor();
		if (!group->Note.empty() && !open)
			UI::Tooltip(group->Note.c_str());

		// The eye and the lock work in the editor only; the game always shows the group's entities.
		const float buttonWidth = ImGui::GetFrameHeight();
		const bool locked = group->LockedInEditor, hidden = group->HiddenInEditor;
		ImGui::SameLine(rowRight - 3.0f * buttonWidth);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(locked ? Theme::Text : Theme::TextDisabled));
		if (UI::IconButton(locked ? ICON_LOCK : ICON_UNLOCK, locked ? "Locked in the editor: clicking in the viewport doesn't select its entities. Click to unlock"
			: "Lock in the editor, so clicking in the viewport doesn't select its entities (the game is unaffected)"))
		{
			scene.FindGroup(id)->LockedInEditor = !locked;
			m_Editor.CommitChange(locked ? "Unlock group" : "Lock group");
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(rowRight - 2.0f * buttonWidth);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(hidden ? Theme::Text : Theme::TextDisabled));
		if (UI::IconButton(hidden ? ICON_EYE_OFF : ICON_EYE, hidden ? "Hidden in the editor's viewport. Click to show"
			: "Hide in the editor's viewport (the game still shows it)"))
		{
			scene.FindGroup(id)->HiddenInEditor = !hidden;
			m_Editor.CommitChange(hidden ? "Show group" : "Hide group");
		}
		ImGui::PopStyleColor();
		ImGui::SameLine(rowRight - buttonWidth);
		if (UI::IconButton(ICON_SETTINGS, "Group settings: name, note, icon and color"))
			OpenGroupSettings(id);

		if (ImGui::BeginPopupContextItem("##GroupContext", ImGuiPopupFlags_MouseButtonRight))
		{
			if (ImGui::MenuItem(ICON_SETTINGS "  Settings..."))
				OpenGroupSettings(id);
			if (ImGui::MenuItem(ICON_SCAN "  Select Its Entities", nullptr, false, count > 0))
			{
				m_Editor.ClearSelection();
				for (Entity member : scene.GetGroupEntities(id))
					m_Editor.Select(member, true);
			}
			if (ImGui::MenuItem(ICON_ARROW_UP "  Move Up", nullptr, false, &scene.GetGroups().front() != group))
			{
				const auto& groups = scene.GetGroups();
				const size_t index = static_cast<size_t>(group - groups.data());
				scene.MoveGroup(id, index - 1);
				m_Editor.CommitChange("Move group");
			}
			if (ImGui::MenuItem(ICON_ARROW_DOWN "  Move Down", nullptr, false, &scene.GetGroups().back() != group))
			{
				const auto& groups = scene.GetGroups();
				const size_t index = static_cast<size_t>(group - groups.data());
				scene.MoveGroup(id, index + 1);
				m_Editor.CommitChange("Move group");
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_TRASH_2 "  Delete Group"))
			{
				scene.DeleteGroup(id);
				m_ClosedGroups.erase(id);
				m_Editor.CommitChange("Delete group");
			}
			UI::TextMuted("Deleting a group keeps its entities.");
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	void SceneHierarchyPanel::DrawNoteRow(const Row& row)
	{
		const SceneGroup* group = m_Editor.GetActiveScene()->FindGroup(row.Group);
		if (!group)
			return;
		ImGui::PushID(static_cast<int>(static_cast<uint64_t>(group->ID) & 0x7FFFFFFF));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		ImGui::Indent(ImGui::GetStyle().IndentSpacing + ImGui::GetFrameHeight());
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
		ImGui::PushFont(Theme::GetFonts().Regular, ImGui::GetFontSize() * 0.9f);
		// One line, cut at the panel's edge; the whole note shows on hover.
		const ImVec2 position = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::PushClipRect(position, position + ImVec2(width, ImGui::GetFrameHeight()), true);
		ImGui::TextUnformatted(group->Note.c_str());
		ImGui::PopClipRect();
		ImGui::PopFont();
		ImGui::PopStyleColor();
		UI::Tooltip(group->Note.c_str());
		ImGui::Unindent(ImGui::GetStyle().IndentSpacing + ImGui::GetFrameHeight());
		ImGui::PopStyleVar();
		ImGui::PopID();
	}

	void SceneHierarchyPanel::DrawUngroupedRow()
	{
		// A thin labeled line between the groups and the entities in none; dropping here takes an entity out.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::InvisibleButton("##ungrouped", ImVec2(width, ImGui::GetFrameHeight()));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::EntityPayload))
			{
				if (Entity dragged = m_Editor.GetActiveScene()->TryGetEntityByUUID(*static_cast<const uint64_t*>(payload->Data)))
					MoveToGroup({ dragged }, 0, true);
			}
			ImGui::EndDragDropTarget();
		}
		ImFont* font = Theme::GetFonts().SemiBold;
		const float size = ImGui::GetFontSize() * 0.8f;
		const char* text = "NOT IN A GROUP";
		const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
		const float y = start.y + ImGui::GetFrameHeight() * 0.5f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddText(font, size, ImVec2(start.x + 6.0f * Theme::GetScale(), y - textSize.y * 0.5f), Theme::TextDisabled, text);
		drawList->AddLine(ImVec2(start.x + textSize.x + 14.0f * Theme::GetScale(), y), ImVec2(start.x + width, y), Theme::Border);
		ImGui::PopStyleVar();
	}

	void SceneHierarchyPanel::DrawSectionRow(const Row& row)
	{
		// A small label and a line, like the "Not in a group" one, but nothing can be dropped here.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
		ImFont* font = Theme::GetFonts().SemiBold;
		const float size = ImGui::GetFontSize() * 0.8f;
		const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, row.Label.c_str());
		const float y = start.y + ImGui::GetFrameHeight() * 0.5f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddText(font, size, ImVec2(start.x + 6.0f * Theme::GetScale(), y - textSize.y * 0.5f), Theme::TextDisabled, row.Label.c_str());
		drawList->AddLine(ImVec2(start.x + textSize.x + 14.0f * Theme::GetScale(), y), ImVec2(start.x + width, y), Theme::Border);
		ImGui::PopStyleVar();
	}

	void SceneHierarchyPanel::DrawHeadingRow(const Row& row)
	{
		// The flat views' headings: a kind or a tag, with how many entities it holds. The label is kept
		// with its view's prefix ("type:Lights") so each view remembers what is open.
		const std::string_view label = std::string_view(row.Label).substr(row.Label.find(':') + 1);
		ImGui::PushID(row.Label.c_str());
		const bool open = m_Searching || !m_ClosedHeadings.contains(row.Label);
		ImGui::SetNextItemOpen(open);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::ToVec4(Theme::Hover));
		ImGui::TreeNodeEx("##heading", ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap
			| ImGuiTreeNodeFlags_NoTreePushOnOpen);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		if (ImGui::IsItemToggledOpen() && !m_Searching)
		{
			if (open)
				m_ClosedHeadings.insert(row.Label);
			else
				m_ClosedHeadings.erase(row.Label);
		}
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::TextUnformatted(label.data(), label.data() + label.size());
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextDisabled));
		ImGui::Text("%zu", row.Count);
		ImGui::PopStyleColor();
		ImGui::PopID();
	}

	void SceneHierarchyPanel::DrawStackRow(const Row& row)
	{
		// A run of siblings with one numbered name, folded into one row; open it to see each.
		Entity first = row.Target;
		if (!first)
			return;
		const UUID key = first.GetUUID();
		const float indent = static_cast<float>(row.Depth) * ImGui::GetStyle().IndentSpacing;
		if (indent > 0.0f)
			ImGui::Indent(indent);
		char id[24];
		std::snprintf(id, sizeof(id), "s%016llx", static_cast<unsigned long long>(static_cast<uint64_t>(key)));
		ImGui::PushID(id);
		const bool open = m_OpenStacks.contains(key);
		ImGui::SetNextItemOpen(open);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		ImGui::TreeNodeEx("##stack", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding
			| ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_NoTreePushOnOpen);
		ImGui::PopStyleVar();
		if (ImGui::IsItemToggledOpen())
		{
			if (open)
				m_OpenStacks.erase(key);
			else
				m_OpenStacks.insert(key);
		}
		else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			// Clicking the row selects the whole run.
			Scene& scene = *m_Editor.GetActiveScene();
			Entity parent = first.GetParent();
			const std::vector<Entity> siblings = parent ? parent.GetChildren() : scene.GetRootEntities();
			if (!ImGui::GetIO().KeyCtrl)
				m_Editor.ClearSelection();
			auto it = std::find(siblings.begin(), siblings.end(), first);
			for (size_t taken = 0; it != siblings.end() && taken < row.Count; ++it, ++taken)
				m_Editor.Select(*it, true);
		}
		UI::Tooltip("A run of entities with one numbered name. Click to select them all; open it to see each one.");
		ImGui::SameLine(0.0f, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::NeutralBright));
		ImGui::TextUnformatted(ICON_LAYERS);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextUnformatted(row.Label.c_str());
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
		ImGui::Text("\xc3\x97%zu", row.Count);
		ImGui::PopStyleColor();
		ImGui::PopID();
		if (indent > 0.0f)
			ImGui::Unindent(indent);
	}

	void SceneHierarchyPanel::OpenOrganize()
	{
		m_OrganizeMoves = HierarchyLogic::SuggestOrganize(*m_Editor.GetActiveScene());
		m_OrganizeOpenPending = true;
	}

	void SceneHierarchyPanel::DrawOrganize()
	{
		if (m_OrganizeOpenPending)
		{
			ImGui::OpenPopup("Organize Scene##Hierarchy");
			m_OrganizeOpenPending = false;
		}
		const float scale = Theme::GetScale();
		ImGui::SetNextWindowSize(ImVec2(420.0f * scale, 0.0f));
		if (!ImGui::BeginPopup("Organize Scene##Hierarchy"))
			return;
		Scene& scene = *m_Editor.GetActiveScene();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::TextUnformatted(ICON_SPARKLES "  Organize scene");
		ImGui::PopFont();
		ImGui::Spacing();
		if (m_OrganizeMoves.empty())
		{
			ImGui::TextWrapped("Nothing to move: every light, camera, UI canvas and sound source at the top is already in a group.");
			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		ImGui::TextWrapped("Bellows suggests %zu %s. Nothing changes until you click Apply, and Ctrl+Z undoes it.",
			m_OrganizeMoves.size(), m_OrganizeMoves.size() == 1 ? "move" : "moves");
		ImGui::Spacing();
		constexpr size_t c_Shown = 8;
		for (size_t i = 0; i < m_OrganizeMoves.size() && i < c_Shown; i++)
		{
			const HierarchyLogic::OrganizeMove& move = m_OrganizeMoves[i];
			Entity entity = scene.TryGetEntityByUUID(move.Entity);
			if (!entity)
				continue;
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextDisabled));
			ImGui::TextUnformatted("\xe2\x86\x92");
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(ToColor(move.Color)));
			ImGui::TextUnformatted(GroupIconGlyph(move.Icon));
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::Text("%s%s", move.Group.c_str(), move.NewGroup ? " (new)" : "");
		}
		if (m_OrganizeMoves.size() > c_Shown)
			UI::TextMuted(("and " + std::to_string(m_OrganizeMoves.size() - c_Shown) + " more").c_str());
		ImGui::Spacing();
		ImGui::Separator();
		const float buttonWidth = 120.0f * scale;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2.0f * buttonWidth - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::SameLine();
		UI::PushPrimaryButtonColors();
		const std::string apply = "Apply " + std::to_string(m_OrganizeMoves.size());
		if (ImGui::Button(apply.c_str(), ImVec2(buttonWidth, 0.0f)))
		{
			if (HierarchyLogic::ApplyOrganize(scene, m_OrganizeMoves) > 0)
				m_Editor.CommitChange("Organize scene");
			ImGui::CloseCurrentPopup();
		}
		UI::PopPrimaryButtonColors();
		ImGui::EndPopup();
	}

	void SceneHierarchyPanel::DrawMoveToGroupMenu()
	{
		Scene& scene = *m_Editor.GetActiveScene();
		std::vector<Entity> selected;
		bool anyTopLevel = false;
		for (UUID uuid : m_Editor.GetSelection())
		{
			if (Entity entity = scene.TryGetEntityByUUID(uuid))
			{
				selected.push_back(entity);
				anyTopLevel |= !entity.GetParent();
			}
		}
		if (!anyTopLevel)
		{
			UI::TextMuted("A child is in its top-level parent's group.\nMove that one, or move this one to the root first.");
			return;
		}
		const UUID current = selected.size() == 1 ? ShownGroup(scene, selected.front()) : UUID(0);
		for (const SceneGroup& group : scene.GetGroups())
		{
			const std::string label = std::string(GroupIconGlyph(group.Icon)) + "  " + group.Name + "##" + std::to_string(static_cast<uint64_t>(group.ID));
			if (ImGui::MenuItem(label.c_str(), nullptr, current == group.ID))
			{
				MoveToGroup(selected, group.ID, false);
				return;
			}
		}
		if (!scene.GetGroups().empty())
			ImGui::Separator();
		if (ImGui::MenuItem(ICON_X "  No Group", nullptr, false, !scene.GetGroups().empty()))
			MoveToGroup(selected, 0, false);
		if (ImGui::MenuItem(ICON_FOLDER_PLUS "  New Group..."))
		{
			if (const UUID group = CreateGroup(); group.IsValid())
			{
				MoveToGroup(selected, group, false);
				OpenGroupSettings(group);
			}
		}
	}

	void SceneHierarchyPanel::MoveToGroup(const std::vector<Entity>& entities, UUID group, bool moveChildren)
	{
		Scene& scene = *m_Editor.GetActiveScene();
		bool changed = false;
		for (Entity entity : entities)
		{
			if (!entity)
				continue;
			if (entity.GetParent())
			{
				if (!moveChildren)
					continue;
				scene.Unparent(entity, true);
				changed = true;
			}
			if (entity.GetComponent<TagComponent>().Group != group)
			{
				scene.SetEntityGroup(entity, group);
				changed = true;
			}
		}
		if (group.IsValid())
			m_ClosedGroups.erase(group);
		if (changed)
			m_Editor.CommitChange(group.IsValid() ? "Move to group" : "Move out of group");
	}

	UUID SceneHierarchyPanel::CreateGroup()
	{
		Scene& scene = *m_Editor.GetActiveScene();
		if (scene.GetGroups().size() >= SceneGroups::MaxGroups)
		{
			BW_WARN("A scene can have at most {} groups", SceneGroups::MaxGroups);
			return 0;
		}
		std::string name = "New Group";
		for (int number = 2; scene.FindGroup(name); number++)
			name = "New Group " + std::to_string(number);
		const UUID id = scene.CreateGroup(name).ID;
		m_Editor.CommitChange("Create group");
		return id;
	}

	void SceneHierarchyPanel::OpenGroupSettings(UUID group)
	{
		const SceneGroup* found = m_Editor.GetActiveScene()->FindGroup(group);
		if (!found)
			return;
		m_SettingsGroup = group;
		m_GroupNameBuffer = found->Name;
		m_GroupNoteBuffer = found->Note;
		m_GroupNameError.clear();
		m_SettingsOpenPending = true;
	}

	void SceneHierarchyPanel::DrawGroupSettings()
	{
		if (m_SettingsOpenPending)
		{
			ImGui::OpenPopup("Group Settings##HierarchyGroup");
			m_SettingsOpenPending = false;
		}
		const float scale = Theme::GetScale();
		ImGui::SetNextWindowSize(ImVec2(360.0f * scale, 0.0f));
		if (!ImGui::BeginPopup("Group Settings##HierarchyGroup"))
			return;

		Scene& scene = *m_Editor.GetActiveScene();
		SceneGroup* group = scene.FindGroup(m_SettingsGroup);
		if (!group)
		{
			// Deleted, or undone away, while the popup was open.
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		const UUID id = group->ID;

		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::TextUnformatted("Group");
		ImGui::PopFont();
		ImGui::Spacing();

		const float labelWidth = 64.0f * scale;
		auto label = [&](const char* text)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(text);
			ImGui::SameLine(labelWidth);
			ImGui::SetNextItemWidth(-FLT_MIN);
		};

		label("Name");
		ImGui::InputText("##name", &m_GroupNameBuffer);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			try
			{
				scene.RenameGroup(id, m_GroupNameBuffer);
				m_GroupNameError.clear();
				m_GroupNameBuffer = scene.FindGroup(id)->Name;
				m_Editor.CommitChange("Rename group");
			}
			catch (const std::invalid_argument& error)
			{
				m_GroupNameError = error.what();
			}
			group = scene.FindGroup(id);
		}
		if (!m_GroupNameError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Error));
			ImGui::TextWrapped("%s", m_GroupNameError.c_str());
			ImGui::PopStyleColor();
		}

		label("Note");
		ImGui::InputTextWithHint("##note", "What this group is for", &m_GroupNoteBuffer);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			std::string note = SceneGroups::Trim(m_GroupNoteBuffer);
			while (SceneGroups::CheckNote(note))
				note.pop_back();
			group->Note = note;
			m_GroupNoteBuffer = note;
			m_Editor.CommitChange("Edit group note");
		}

		label("Icon");
		ImGui::NewLine();
		ImGui::SameLine(labelWidth);
		int column = 0;
		for (std::string_view icon : SceneGroups::GetIcons())
		{
			if (column == 8)
			{
				ImGui::NewLine();
				ImGui::SameLine(labelWidth);
				column = 0;
			}
			else if (column > 0)
				ImGui::SameLine();
			const std::string name(icon);
			ImGui::PushID(name.c_str());
			if (UI::IconButton(GroupIconGlyph(name), name.c_str(), group->Icon == icon))
			{
				group->Icon = name;
				m_Editor.CommitChange("Change group icon");
			}
			ImGui::PopID();
			column++;
		}

		label("Color");
		ImGui::NewLine();
		ImGui::SameLine(labelWidth);
		for (size_t i = 0; i < std::size(c_GroupColors); i++)
		{
			if (i > 0)
				ImGui::SameLine();
			const GroupColor& color = c_GroupColors[i];
			const bool current = glm::all(glm::lessThan(glm::abs(group->Color - color.Value), glm::vec3(0.002f)));
			const ImVec2 position = ImGui::GetCursorScreenPos();
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::ColorButton(color.Name, ImVec4(color.Value.r, color.Value.g, color.Value.b, 1.0f),
				ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
			{
				group->Color = color.Value;
				m_Editor.CommitChange("Change group color");
			}
			UI::Tooltip(color.Name);
			if (current)
				ImGui::GetWindowDrawList()->AddRect(position - ImVec2(2.0f, 2.0f) * scale,
					position + ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()) + ImVec2(2.0f, 2.0f) * scale, Theme::Text, 6.0f * scale, 1.5f * scale);
			ImGui::PopID();
		}

		ImGui::Spacing();
		UI::TextMuted(("Scripts reach it as group(\"" + group->Name + "\").").c_str());
		ImGui::Separator();
		if (ImGui::Button(ICON_TRASH_2 "  Delete Group"))
		{
			scene.DeleteGroup(id);
			m_ClosedGroups.erase(id);
			m_Editor.CommitChange("Delete group");
			ImGui::CloseCurrentPopup();
		}
		UI::Tooltip("Its entities stay where they are, in no group");
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 90.0f * scale);
		if (ImGui::Button("Done", ImVec2(90.0f * scale, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void SceneHierarchyPanel::DrawCreateMenu(Entity parent)
	{
		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		if (!scene)
			return;

		auto create = [&](const std::string& name) -> Entity
		{
			return m_Editor.CreateEntity(name, parent);
		};
		auto createMesh = [&](const char* mesh, const char* name)
		{
			Entity entity = parent ? scene->CreateChildEntity(parent, name) : scene->CreateEntity(name);
			entity.AddComponent<MeshRendererComponent>().MeshPath = mesh;
			if (!parent)
				entity.GetTransform().Translation = glm::vec3(m_Editor.GetCamera().GetFocusPoint().x, 0.5f, m_Editor.GetCamera().GetFocusPoint().z);
			m_Editor.Select(entity);
			m_Editor.CommitChange(std::string("Create ") + name);
		};

		if (ImGui::MenuItem(ICON_CIRCLE "  Empty Entity"))
			create("Entity");

		if (ImGui::BeginMenu(ICON_SHAPES "  3D Object"))
		{
			if (ImGui::MenuItem(ICON_BOX "  Cube")) createMesh("Builtin/Cube", "Cube");
			if (ImGui::MenuItem(ICON_CIRCLE "  Sphere")) createMesh("Builtin/Sphere", "Sphere");
			if (ImGui::MenuItem(ICON_SQUARE "  Plane")) createMesh("Builtin/Plane", "Plane");
			if (ImGui::MenuItem(ICON_CYLINDER "  Cylinder")) createMesh("Builtin/Cylinder", "Cylinder");
			if (ImGui::MenuItem(ICON_CONE "  Cone")) createMesh("Builtin/Cone", "Cone");
			if (ImGui::MenuItem(ICON_CIRCLE "  Capsule")) createMesh("Builtin/Capsule", "Capsule");
			if (ImGui::MenuItem(ICON_SQUARE "  Quad")) createMesh("Builtin/Quad", "Quad");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(ICON_LIGHTBULB "  Light"))
		{
			if (ImGui::MenuItem(ICON_SUN "  Directional Light"))
			{
				Entity entity = create("Directional Light");
				entity.AddComponent<DirectionalLightComponent>();
				entity.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-50.0f, 70.0f, 0.0f)));
				m_Editor.CommitChange("Create light");
			}
			if (ImGui::MenuItem(ICON_LIGHTBULB "  Point Light"))
			{
				Entity entity = create("Point Light");
				entity.AddComponent<PointLightComponent>();
				entity.GetTransform().Translation = glm::vec3(0.0f, 2.0f, 0.0f);
				m_Editor.CommitChange("Create light");
			}
			if (ImGui::MenuItem(ICON_FLASHLIGHT "  Spot Light"))
			{
				Entity entity = create("Spot Light");
				entity.AddComponent<SpotLightComponent>();
				entity.GetTransform().Translation = glm::vec3(0.0f, 3.0f, 0.0f);
				entity.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-90.0f, 0.0f, 0.0f)));
				m_Editor.CommitChange("Create light");
			}
			if (ImGui::MenuItem(ICON_CLOUD "  Sky Light"))
			{
				create("Sky").AddComponent<SkyLightComponent>();
				m_Editor.CommitChange("Create sky");
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(ICON_CAMERA "  Camera"))
		{
			Entity entity = create("Camera");
			entity.AddComponent<CameraComponent>().Primary = !scene->GetPrimaryCameraEntity();
			m_Editor.CommitChange("Create camera");
		}

		if (ImGui::BeginMenu(ICON_ATOM "  Physics Object"))
		{
			auto createPhysics = [&](const char* mesh, const char* name, int collider, RigidBodyType type)
			{
				Entity entity = parent ? scene->CreateChildEntity(parent, name) : scene->CreateEntity(name);
				entity.AddComponent<MeshRendererComponent>().MeshPath = mesh;
				entity.AddComponent<RigidBodyComponent>().Type = type;
				if (collider == 0) entity.AddComponent<BoxColliderComponent>();
				if (collider == 1) entity.AddComponent<SphereColliderComponent>();
				if (collider == 2) entity.AddComponent<CapsuleColliderComponent>();
				entity.GetTransform().Translation = glm::vec3(m_Editor.GetCamera().GetFocusPoint().x, 2.0f, m_Editor.GetCamera().GetFocusPoint().z);
				m_Editor.Select(entity);
				m_Editor.CommitChange(std::string("Create ") + name);
			};
			if (ImGui::MenuItem(ICON_BOX "  Dynamic Box")) createPhysics("Builtin/Cube", "Box", 0, RigidBodyType::Dynamic);
			if (ImGui::MenuItem(ICON_CIRCLE "  Dynamic Sphere")) createPhysics("Builtin/Sphere", "Ball", 1, RigidBodyType::Dynamic);
			if (ImGui::MenuItem(ICON_CIRCLE "  Dynamic Capsule")) createPhysics("Builtin/Capsule", "Capsule", 2, RigidBodyType::Dynamic);
			if (ImGui::MenuItem(ICON_SQUARE "  Static Box")) createPhysics("Builtin/Cube", "Wall", 0, RigidBodyType::Static);
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(ICON_FLAME "  Particles"))
		{
			create("Particles").AddComponent<ParticleEmitterComponent>();
			m_Editor.CommitChange("Create particles");
		}

		if (ImGui::MenuItem(ICON_SPLINE "  Trail"))
		{
			create("Trail").AddComponent<TrailComponent>();
			m_Editor.CommitChange("Create trail");
		}

		if (ImGui::MenuItem(ICON_STICKER "  Decal"))
		{
			create("Decal").AddComponent<DecalComponent>();
			m_Editor.CommitChange("Create decal");
		}

		if (ImGui::MenuItem(ICON_VOLUME_2 "  Audio Source"))
		{
			create("Audio Source").AddComponent<AudioSourceComponent>();
			m_Editor.CommitChange("Create audio source");
		}

		if (ImGui::MenuItem(ICON_TYPE "  Text"))
		{
			create("Text").AddComponent<TextComponent>();
			m_Editor.CommitChange("Create text");
		}

		if (ImGui::BeginMenu(ICON_MONITOR "  UI"))
		{
			struct Entry { UITemplate Kind; const char* Icon; };
			static constexpr Entry c_Entries[] = {
				{ UITemplate::Canvas, ICON_MONITOR }, { UITemplate::Panel, ICON_IMAGE }, { UITemplate::Text, ICON_TYPE },
				{ UITemplate::Button, ICON_MOUSE_POINTER_CLICK }, { UITemplate::Toggle, ICON_SQUARE_CHECK },
				{ UITemplate::Slider, ICON_SLIDERS_HORIZONTAL }, { UITemplate::Dropdown, ICON_CHEVRONS_UP_DOWN },
				{ UITemplate::TextInput, ICON_TEXT_CURSOR_INPUT }, { UITemplate::VerticalLayout, ICON_LAYOUT_GRID },
				{ UITemplate::ScrollList, ICON_LAYERS },
				{ UITemplate::MainMenu, ICON_LAYOUT_TEMPLATE }, { UITemplate::HUD, ICON_ACTIVITY },
			};
			for (const Entry& entry : c_Entries)
			{
				// Whole screens come after the single widgets.
				if (entry.Kind == UITemplate::MainMenu)
					ImGui::Separator();
				const std::string label = std::string(entry.Icon) + "  " + UITemplateToString(entry.Kind);
				if (ImGui::MenuItem(label.c_str()))
				{
					Entity created = CreateUITemplate(*scene, parent, entry.Kind);
					m_Editor.Select(created);
					m_Editor.CommitChange(std::string("Create ") + UITemplateToString(entry.Kind));
				}
			}
			ImGui::EndMenu();
		}
	}

}
