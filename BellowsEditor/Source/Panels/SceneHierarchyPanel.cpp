#include "SceneHierarchyPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ScriptEditorPanel.h"

#include "Bellows/Asset/AssetManager.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdio>

namespace Bellows {

	namespace {

		const char* EntityIcon(Entity entity)
		{
			if (entity.HasComponent<CameraComponent>()) return ICON_CAMERA;
			if (entity.HasComponent<DirectionalLightComponent>()) return ICON_SUN;
			if (entity.HasComponent<PointLightComponent>()) return ICON_LIGHTBULB;
			if (entity.HasComponent<SpotLightComponent>()) return ICON_FLASHLIGHT;
			if (entity.HasComponent<SkyLightComponent>()) return ICON_CLOUD;
			if (entity.HasComponent<PrefabComponent>()) return ICON_PACKAGE;
			if (entity.HasComponent<MeshRendererComponent>()) return ICON_BOX;
			if (entity.HasComponent<AudioSourceComponent>()) return ICON_VOLUME_2;
			if (entity.HasComponent<UICanvasComponent>()) return ICON_MONITOR;
			if (entity.HasComponent<TextComponent>()) return ICON_TYPE;
			if (entity.HasComponent<UIImageComponent>()) return ICON_IMAGE;
			if (entity.HasComponent<UIRectComponent>()) return ICON_SCAN;
			if (entity.HasComponent<ScriptComponent>()) return ICON_FILE_CODE;
			if (entity.HasComponent<RigidBodyComponent>()) return ICON_ATOM;
			return ICON_CIRCLE;
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

		// Search and create
		const float buttonWidth = ImGui::GetFrameHeight();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Search entities", &m_Filter);
		ImGui::SameLine();
		if (UI::IconButton(ICON_PLUS, "Create entity"))
			ImGui::OpenPopup("##CreateEntity");
		if (ImGui::BeginPopup("##CreateEntity"))
		{
			DrawCreateMenu({});
			ImGui::EndPopup();
		}
		ImGui::Spacing();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f * Theme::GetScale()));
		ImGui::BeginChild("##Tree");
		for (Entity root : scene->GetRootEntities())
			DrawEntityNode(root);

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

		if (scene->GetEntityCount() == 0)
			UI::CenteredText("The scene is empty.\nRight-click to create an entity.");

		ImGui::End();
	}

	bool SceneHierarchyPanel::MatchesFilter(Entity entity) const
	{
		if (m_Filter.empty())
			return true;
		std::string name = entity.GetName();
		std::string filter = m_Filter;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
		if (name.find(filter) != std::string::npos)
			return true;
		for (Entity child : entity.GetChildren())
		{
			if (MatchesFilter(child))
				return true;
		}
		return false;
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		if (!MatchesFilter(entity))
			return;

		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		const UUID uuid = entity.GetUUID();
		const std::vector<Entity> children = entity.GetChildren();
		const bool selected = m_Editor.IsSelected(entity);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanFullWidth
			| ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
		if (selected)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (!m_Filter.empty())
			ImGui::SetNextItemOpen(true);

		char id[24];
		std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(static_cast<uint64_t>(uuid)));
		ImGui::PushID(id);
		const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
		const bool open = ImGui::TreeNodeEx("##node", flags);
		ImGui::PopStyleVar();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
			m_Editor.Select(entity, ImGui::GetIO().KeyCtrl);
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && children.empty())
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
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(pending ? Theme::TextDisabled : Theme::Text));
			ImGui::TextUnformatted(entity.GetName().c_str());
			ImGui::PopStyleColor();
		}

		// A script badge at the end of the row opens the entity's script in the Script Editor.
		if (const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>(); script && !script->ScriptPath.empty())
		{
			const float badgeWidth = ImGui::GetFrameHeight();
			ImGui::SameLine(rowRight - badgeWidth);
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::FieldHovered));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Accent));
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
			ImGui::Separator();
			const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>();
			const bool editing = m_Editor.GetSceneState() == SceneState::Edit;
			if (ImGui::MenuItem(ICON_PAPERCLIP "  Attach Script...", nullptr, false, editing))
				m_Editor.GetScriptEditor().ShowAttachDialog(entity);
			if (script && !script->ScriptPath.empty())
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

		if (selected && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
		{
			m_RenamingEntity = uuid;
			m_RenameBuffer = entity.GetName();
			m_RenameFocusPending = true;
		}

		if (open && !children.empty())
		{
			for (Entity child : children)
			{
				if (child)
					DrawEntityNode(child);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
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
			// UI elements live under a canvas: inside the selected one, or in a new canvas.
			auto createUI = [&](const char* name) -> Entity
			{
				Entity uiParent = parent && scene->IsInsideUICanvas(parent) ? parent : Entity();
				if (!uiParent)
				{
					uiParent = scene->CreateChildEntity(parent, "Canvas");
					uiParent.AddComponent<UICanvasComponent>();
				}
				Entity entity = scene->CreateChildEntity(uiParent, name);
				entity.AddComponent<UIRectComponent>();
				return entity;
			};
			auto finish = [&](Entity entity, const char* name)
			{
				m_Editor.Select(entity);
				m_Editor.CommitChange(std::string("Create ") + name);
			};

			if (ImGui::MenuItem(ICON_MONITOR "  Canvas"))
			{
				Entity canvas = scene->CreateChildEntity(parent, "Canvas");
				canvas.AddComponent<UICanvasComponent>();
				finish(canvas, "canvas");
			}
			if (ImGui::MenuItem(ICON_IMAGE "  Panel"))
			{
				Entity panel = createUI("Panel");
				UIRectComponent& rect = panel.GetComponent<UIRectComponent>();
				SetUIRectSize(rect.Pivot, rect.OffsetMin, rect.OffsetMax, glm::vec2(400.0f, 300.0f));
				UIImageComponent& image = panel.AddComponent<UIImageComponent>();
				image.Color = glm::vec4(0.09f, 0.09f, 0.16f, 0.9f);
				image.CornerRadius = 12.0f;
				finish(panel, "panel");
			}
			if (ImGui::MenuItem(ICON_TYPE "  Text"))
			{
				Entity label = createUI("Label");
				UIRectComponent& rect = label.GetComponent<UIRectComponent>();
				SetUIRectSize(rect.Pivot, rect.OffsetMin, rect.OffsetMax, glm::vec2(300.0f, 60.0f));
				label.AddComponent<TextComponent>().Size = TextComponent::DefaultScreenSize;
				finish(label, "UI text");
			}
			ImGui::EndMenu();
		}
	}

}
