#include "ContentBrowserPanel.h"

#include "AssetFiles.h"
#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ScriptEditorPanel.h"
#include "ScriptDocument.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cmath>
#include <optional>

namespace Bellows {

	namespace {

		constexpr double c_RefreshInterval = 1.0;	// Seconds between folder rescans
#if defined(BW_PLATFORM_WINDOWS)
		constexpr const char* c_TrashName = "Recycle Bin";
#else
		constexpr const char* c_TrashName = "Trash";
#endif

	}

	void ContentBrowserPanel::SetCurrentDirectory(const std::filesystem::path& directory)
	{
		m_CurrentDirectory = directory;
		m_LastRefreshTime = -1.0;
		m_SelectedAsset.clear();
		m_RevealCurrentFolder = true;
	}

	void ContentBrowserPanel::Refresh()
	{
		m_Items.clear();
		m_FailedThumbnails.clear();
		m_Subfolders.clear();
		std::error_code error;
		if (!std::filesystem::is_directory(m_CurrentDirectory, error))
			m_CurrentDirectory = AssetManager::GetAssetDirectory();

		for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory, error))
		{
			Item item;
			item.Path = entry.path();
			item.Name = entry.path().filename().string();
			item.IsDirectory = entry.is_directory(error);
			if (item.Name.empty() || item.Name.front() == '.' || item.Path.extension() == ".tmp" || AssetFiles::IsCompanionFile(item.Path))
				continue;
			item.AssetPath = AssetManager::GetAssetPath(item.Path);
			if (!item.IsDirectory && AssetManager::GetAssetType(item.AssetPath) == AssetType::None)
				continue;
			m_Items.push_back(std::move(item));
		}
		std::sort(m_Items.begin(), m_Items.end(), [](const Item& a, const Item& b)
		{
			if (a.IsDirectory != b.IsDirectory)
				return a.IsDirectory;
			return a.Name < b.Name;
		});
		m_LastRefreshTime = ImGui::GetTime();
	}

	std::filesystem::path ContentBrowserPanel::MakeUniquePath(const std::string& stem, const std::string& extension) const
	{
		std::filesystem::path path = m_CurrentDirectory / (stem + extension);
		for (int suffix = 1; FileSystem::Exists(path); suffix++)
			path = m_CurrentDirectory / (stem + std::to_string(suffix) + extension);
		return path;
	}

	void ContentBrowserPanel::ImportFiles(const std::vector<std::filesystem::path>& paths)
	{
		for (const std::filesystem::path& sourcePath : paths)
		{
			const std::string source = sourcePath.string();
			std::filesystem::path target = m_CurrentDirectory / sourcePath.filename();
			if (sourcePath == target)
				continue;
			std::error_code error;
			if (std::filesystem::is_directory(sourcePath, error))
			{
				if (!FileSystem::CopyDirectory(sourcePath, target))
					BW_ERROR("Could not import folder '{}'", source);
				continue;
			}
			if (FileSystem::Exists(target))
				target = MakeUniquePath(sourcePath.stem().string(), sourcePath.extension().string());
			std::filesystem::copy_file(sourcePath, target, error);
			if (error)
				BW_ERROR("Could not import '{}': {}", source, error.message());
			else
				BW_INFO("Imported '{}'", AssetManager::GetAssetPath(target));
		}
		m_LastRefreshTime = -1.0;
	}

	void ContentBrowserPanel::OnImGuiRender(bool* open)
	{
		if (!UI::BeginPanel("Content", open))
		{
			ImGui::End();
			return;
		}
		if (AssetManager::GetAssetDirectory().empty())
		{
			UI::CenteredText("No project");
			ImGui::End();
			return;
		}
		if (m_CurrentDirectory.empty())
			m_CurrentDirectory = AssetManager::GetAssetDirectory();
		if (m_LastRefreshTime < 0.0 || ImGui::GetTime() - m_LastRefreshTime > c_RefreshInterval)
			Refresh();

		const float scale = Theme::GetScale();

		// Toolbar
		bool& showFolders = m_Editor.GetSettings().ShowContentFolderTree;
		if (UI::IconButton(ICON_PANEL_LEFT, showFolders ? "Hide the folder tree" : "Show the folder tree", showFolders))
			showFolders = !showFolders;
		ImGui::SameLine();
		std::error_code error;
		const bool atRoot = m_CurrentDirectory == AssetManager::GetAssetDirectory() || std::filesystem::equivalent(m_CurrentDirectory, AssetManager::GetAssetDirectory(), error);
		ImGui::BeginDisabled(atRoot);
		if (UI::IconButton(ICON_ARROW_LEFT, "Up one folder"))
			SetCurrentDirectory(m_CurrentDirectory.parent_path());
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (UI::IconButton(ICON_PLUS, "Create asset"))
			ImGui::OpenPopup("##CreateAsset");
		if (ImGui::BeginPopup("##CreateAsset"))
		{
			DrawCreateMenu();
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (UI::IconButton(ICON_DOWNLOAD, "Import files..."))
		{
			const std::string file = UI::OpenFileDialog("Import asset", { "Assets", "*.gltf *.glb *.png *.jpg *.jpeg *.tga *.bmp *.hdr *.wav *.mp3 *.flac *.ogg *.forge *.py", "All files", "*" });
			if (!file.empty())
				ImportFiles({ std::filesystem::path(reinterpret_cast<const char8_t*>(file.c_str())) });
		}
		ImGui::SameLine();
		DrawBreadcrumbs();

		const float filterWidth = 200.0f * scale;
		ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - filterWidth - 120.0f * scale));
		ImGui::SetNextItemWidth(filterWidth);
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Filter", &m_Filter);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderFloat("##size", &m_ThumbnailSize, 56.0f, 160.0f, "", ImGuiSliderFlags_AlwaysClamp) && !std::isfinite(m_ThumbnailSize))
			m_ThumbnailSize = 84.0f;
		UI::Tooltip("Thumbnail size");
		ImGui::Separator();

		// Folder tree on the left (like Godot's FileSystem dock), asset grid on the right.
		const bool split = showFolders && ImGui::BeginTable("##ContentSplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX);
		if (split)
		{
			ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 190.0f * scale);
			ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::BeginChild("##Folders");
			DrawFolderNode(AssetManager::GetAssetDirectory(), "Assets");
			ImGui::EndChild();
			ImGui::TableSetColumnIndex(1);
		}

		// Grid
		ImGui::BeginChild("##Grid");
		const float cellSize = m_ThumbnailSize * scale;
		const float spacing = 10.0f * scale;
		const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cellSize + spacing)));
		int column = 0;
		std::string lowerFilter = m_Filter;
		std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
		for (const Item& item : m_Items)
		{
			if (!lowerFilter.empty())
			{
				std::string lowerName = item.Name;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.find(lowerFilter) == std::string::npos)
					continue;
			}
			if (column > 0)
				ImGui::SameLine(0.0f, spacing);
			DrawItem(item, cellSize);
			column = (column + 1) % columns;
		}
		if (m_Items.empty())
			UI::CenteredText("This folder is empty.\nDrop files here or right-click to create assets.");

		if (ImGui::BeginPopupContextWindow("##BackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawCreateMenu();
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Show in File Manager"))
				Platform::OpenWithDefaultApplication(m_CurrentDirectory);
			ImGui::EndPopup();
		}
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
			m_SelectedAsset.clear();
		ImGui::EndChild();

		// Entities dragged here become prefabs.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::EntityPayload))
			{
				if (Entity entity = m_Editor.GetActiveScene()->TryGetEntityByUUID(*static_cast<const uint64_t*>(payload->Data)))
				{
					const std::filesystem::path path = MakeUniquePath(entity.GetName(), ".bprefab");
					m_Editor.SavePrefab(entity, AssetManager::GetAssetPath(path));
					m_LastRefreshTime = -1.0;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (split)
			ImGui::EndTable();

		DrawDialogs();
		ImGui::End();
	}

	const std::vector<std::filesystem::path>& ContentBrowserPanel::GetSubfolders(const std::filesystem::path& folder)
	{
		// Listed once per rescan (Refresh clears the cache), not every frame.
		auto [it, inserted] = m_Subfolders.try_emplace(folder.string());
		if (inserted)
		{
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(folder, error))
			{
				std::error_code entryError;
				const std::string name = entry.path().filename().string();
				if (entry.is_directory(entryError) && !name.empty() && name.front() != '.')
					it->second.push_back(entry.path());
			}
			std::sort(it->second.begin(), it->second.end());
		}
		return it->second;
	}

	void ContentBrowserPanel::DrawFolderNode(const std::filesystem::path& folder, const std::string& label)
	{
		const std::vector<std::filesystem::path>& children = GetSubfolders(folder);
		// Both paths are built from the asset folder, so a plain comparison is enough (no disk access per frame).
		const bool current = folder.lexically_normal() == m_CurrentDirectory.lexically_normal();

		// Folders on the way to the current one open, so navigating elsewhere reveals it in the tree.
		const std::filesystem::path relative = m_CurrentDirectory.lexically_relative(folder);
		const bool containsCurrent = !current && !relative.empty() && *relative.begin() != "..";
		if (containsCurrent && m_RevealCurrentFolder)
			ImGui::SetNextItemOpen(true);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanFullWidth
			| ImGuiTreeNodeFlags_FramePadding;
		if (current)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (folder == AssetManager::GetAssetDirectory())
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		ImGui::PushID(folder.string().c_str());
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
		const bool open = ImGui::TreeNodeEx("##folder", flags);
		ImGui::PopStyleVar();
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
			SetCurrentDirectory(folder);

		// Assets dropped on a folder move into it.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::AssetPayload))
				MoveAsset(static_cast<const char*>(payload->Data), folder);
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("##FolderContext"))
		{
			if (ImGui::MenuItem(ICON_FOLDER_PLUS "  New Folder"))
			{
				SetCurrentDirectory(folder);
				const std::filesystem::path path = MakeUniquePath("New Folder", "");
				FileSystem::CreateDirectories(path);
				m_RenamePath = path;
				m_RenameBuffer = path.filename().string();
				m_OpenRename = true;
			}
			if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Show in File Manager"))
				Platform::OpenWithDefaultApplication(folder);
			ImGui::EndPopup();
		}

		ImGui::SameLine(0.0f, 2.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(IM_COL32(232, 184, 92, 255)));
		ImGui::TextUnformatted(open && !children.empty() ? ICON_FOLDER_OPEN : ICON_FOLDER);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextUnformatted(label.c_str());

		if (open)
		{
			for (const std::filesystem::path& child : children)
				DrawFolderNode(child, child.filename().string());
			ImGui::TreePop();
		}
		ImGui::PopID();

		if (folder == AssetManager::GetAssetDirectory())
			m_RevealCurrentFolder = false;
	}

	void ContentBrowserPanel::MoveAsset(const std::string& assetPath, const std::filesystem::path& folder)
	{
		const std::filesystem::path source = AssetManager::GetAbsolutePath(assetPath);
		if (source.parent_path() == folder || source == folder)
			return;
		std::string error;
		if (!AssetFiles::Move(source, folder, error))
			BW_ERROR("Could not move '{}': {}", source.filename().string(), error);
		AssetManager::Invalidate(assetPath);
		m_LastRefreshTime = -1.0;
	}

	void ContentBrowserPanel::DrawBreadcrumbs()
	{
		const std::filesystem::path root = AssetManager::GetAssetDirectory();
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, root, error);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
		if (ImGui::Button(ICON_FOLDER "  Assets"))
			SetCurrentDirectory(root);
		std::filesystem::path accumulated = root;
		int index = 0;
		for (const auto& part : relative)
		{
			if (part == ".")
				continue;
			accumulated /= part;
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_CHEVRON_RIGHT);
			ImGui::SameLine();
			ImGui::PushID(index++);
			if (ImGui::Button(part.string().c_str()))
				SetCurrentDirectory(accumulated);
			ImGui::PopID();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}

	void ContentBrowserPanel::DrawItem(const Item& item, float size)
	{
		const float scale = Theme::GetScale();
		const AssetType type = item.IsDirectory ? AssetType::None : AssetManager::GetAssetType(item.AssetPath);
		const bool selected = !item.IsDirectory && item.AssetPath == m_SelectedAsset;
		const float labelHeight = ImGui::GetTextLineHeight() * 2.0f + 6.0f * scale;

		ImGui::PushID(item.Name.c_str());
		ImGui::BeginGroup();
		const ImVec2 position = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##item", ImVec2(size, size + labelHeight));
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImVec2 max(position.x + size, position.y + size + labelHeight);
		if (selected || hovered)
			drawList->AddRectFilled(position, max, selected ? Theme::AccentMuted : Theme::FieldHovered, 6.0f * scale);

		// Thumbnail: textures show themselves, everything else gets a typed icon tile.
		const ImVec2 tileMin(position.x + 6.0f * scale, position.y + 6.0f * scale);
		const ImVec2 tileMax(position.x + size - 6.0f * scale, position.y + size - 6.0f * scale);
		bool drewThumbnail = false;
		if (type == AssetType::Texture && !m_FailedThumbnails.contains(item.AssetPath))
		{
			Ref<Texture2D> texture = AssetManager::GetTexture(item.AssetPath, TextureColorSpace::SRGB);
			if (!texture)
				m_FailedThumbnails.insert(item.AssetPath);
			if (texture)
			{
				const float aspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(std::max(texture->GetHeight(), 1u));
				ImVec2 imageMin = tileMin, imageMax = tileMax;
				const float tileSize = tileMax.x - tileMin.x;
				if (aspect > 1.0f)
				{
					const float inset = (tileSize - tileSize / aspect) * 0.5f;
					imageMin.y += inset;
					imageMax.y -= inset;
				}
				else
				{
					const float inset = (tileSize - tileSize * aspect) * 0.5f;
					imageMin.x += inset;
					imageMax.x -= inset;
				}
				drawList->AddImageRounded(ImTextureRef(reinterpret_cast<ImTextureID>(texture->GetHandle())), imageMin, imageMax,
					ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, 4.0f * scale);
				drewThumbnail = true;
			}
		}
		// ForgeScript files carry the language's own icon, which brings its own tile.
		const Ref<Texture2D>& forgeScriptIcon = m_Editor.GetForgeScriptIcon();
		if (!drewThumbnail && forgeScriptIcon && type == AssetType::Script && GetScriptLanguage(item.Path) == ScriptLanguage::ForgeScript)
		{
			const float inset = (tileMax.x - tileMin.x) * 0.1f;
			drawList->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(forgeScriptIcon->GetHandle())),
				ImVec2(tileMin.x + inset, tileMin.y + inset), ImVec2(tileMax.x - inset, tileMax.y - inset));
			drewThumbnail = true;
		}
		if (!drewThumbnail)
		{
			const ImU32 color = item.IsDirectory ? IM_COL32(214, 170, 90, 255) : UI::AssetColor(type);
			drawList->AddRectFilled(tileMin, tileMax, Theme::Field, 6.0f * scale);
			const char* icon = item.IsDirectory ? ICON_FOLDER : UI::AssetIcon(type);
			const float iconSize = (tileMax.x - tileMin.x) * 0.42f;
			ImFont* font = ImGui::GetFont();
			const ImVec2 textSize = font->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
			drawList->AddText(font, iconSize, ImVec2((tileMin.x + tileMax.x - textSize.x) * 0.5f, (tileMin.y + tileMax.y - textSize.y) * 0.5f), color, icon);
		}

		// Label: up to two lines, clipped.
		const ImVec4 clip(position.x + 4.0f * scale, position.y + size, max.x - 4.0f * scale, max.y);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(position.x + 6.0f * scale, position.y + size), Theme::Text,
			item.Name.c_str(), nullptr, size - 12.0f * scale, &clip);

		if (hovered)
			UI::Tooltip(item.IsDirectory ? item.Name.c_str() : item.AssetPath.c_str());

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !item.IsDirectory)
			m_SelectedAsset = item.AssetPath;
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenItem(item);

		if (!item.IsDirectory && ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(UI::AssetPayload, item.AssetPath.c_str(), item.AssetPath.size() + 1);
			ImGui::Text("%s  %s", UI::AssetIcon(type), item.Name.c_str());
			ImGui::EndDragDropSource();
		}

		// Assets can be dropped onto folders to move them.
		if (item.IsDirectory && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::AssetPayload))
				MoveAsset(static_cast<const char*>(payload->Data), item.Path);
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##ItemContext"))
		{
			if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Open"))
				OpenItem(item);
			if (type == AssetType::Prefab && ImGui::MenuItem(ICON_PLUS "  Instantiate in Scene"))
				m_Editor.InstantiatePrefab(item.AssetPath, m_Editor.GetCamera().GetFocusPoint());
			if (type == AssetType::Mesh && ImGui::MenuItem(ICON_PLUS "  Add to Scene"))
				m_Editor.CreateMeshEntity(item.AssetPath, m_Editor.GetCamera().GetFocusPoint());
			if (type == AssetType::Mesh && ImGui::BeginMenu(ICON_ROTATE_3D "  Front Faces"))
			{
				// Blender and most glTF models face +Z; Bellows' forward is -Z.
				const ModelFront current = AssetManager::GetMeshImportSettings(item.AssetPath).Front;
				if (ImGui::MenuItem("-Z (as made)", nullptr, current == ModelFront::NegativeZ))
					m_Editor.SetModelFront(item.AssetPath, ModelFront::NegativeZ);
				if (ImGui::MenuItem("+Z (turn to face forward)", nullptr, current == ModelFront::PositiveZ))
					m_Editor.SetModelFront(item.AssetPath, ModelFront::PositiveZ);
				ImGui::EndMenu();
			}
			if (type == AssetType::Script && ImGui::MenuItem(ICON_EXTERNAL_LINK "  Open in External Editor"))
				Platform::OpenForEditing(item.Path);
			if (type == AssetType::Script && m_Editor.GetPrimarySelection() && ImGui::MenuItem(ICON_PAPERCLIP "  Attach to Selected Entity"))
				m_Editor.AttachScript(m_Editor.GetPrimarySelection(), item.AssetPath);
			if (ImGui::MenuItem(ICON_PENCIL "  Rename"))
			{
				m_RenamePath = item.Path;
				m_RenameBuffer = item.Name;
				m_OpenRename = true;
			}
			if (ImGui::MenuItem(ICON_COPY "  Copy Path"))
				ImGui::SetClipboardText(item.IsDirectory ? item.Path.string().c_str() : item.AssetPath.c_str());
			if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Show in File Manager"))
				Platform::OpenWithDefaultApplication(item.IsDirectory ? item.Path : item.Path.parent_path());
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_TRASH_2 "  Delete"))
			{
				m_DeletePath = item.Path;
				m_OpenDelete = true;
			}
			ImGui::EndPopup();
		}

		ImGui::EndGroup();
		ImGui::PopID();
	}

	void ContentBrowserPanel::OpenItem(const Item& item)
	{
		if (item.IsDirectory)
		{
			SetCurrentDirectory(item.Path);
			return;
		}
		switch (AssetManager::GetAssetType(item.AssetPath))
		{
			case AssetType::Scene:
			{
				const std::string path = item.AssetPath;
				Application::Get().SubmitToMainThread([this, path] { m_Editor.OpenScene(path); });
				break;
			}
			case AssetType::Script:
				m_Editor.GetScriptEditor().OpenScript(item.Path);
				break;
			case AssetType::Audio:
			case AssetType::Texture:
				Platform::OpenWithDefaultApplication(item.Path);
				break;
			default:
				m_SelectedAsset = item.AssetPath;
				break;
		}
	}

	void ContentBrowserPanel::DrawCreateMenu()
	{
		if (ImGui::MenuItem(ICON_FOLDER_PLUS "  Folder"))
		{
			const std::filesystem::path path = MakeUniquePath("New Folder", "");
			FileSystem::CreateDirectories(path);
			m_RenamePath = path;
			m_RenameBuffer = path.filename().string();
			m_OpenRename = true;
			m_LastRefreshTime = -1.0;
		}
		ImGui::Separator();
		for (ScriptLanguage language : { ScriptLanguage::ForgeScript, ScriptLanguage::Python })
		{
			const std::string label = std::string(ICON_FILE_CODE "  ") + GetScriptLanguageName(language) + " Script";
			if (ImGui::MenuItem(label.c_str()))
			{
				const std::filesystem::path path = MakeUniquePath("NewScript", GetScriptExtension(language));
				if (FileSystem::WriteTextFile(path, MakeScriptSource(language, ScriptTemplate::Default, path.stem().string())))
				{
					m_SelectedAsset = AssetManager::GetAssetPath(path);
					m_Editor.GetScriptEditor().OpenScript(path);
				}
				m_LastRefreshTime = -1.0;
			}
		}
		if (ImGui::MenuItem(ICON_PALETTE "  Material"))
		{
			const std::filesystem::path path = MakeUniquePath("NewMaterial", ".bmat");
			const std::string assetPath = AssetManager::GetAssetPath(path);
			AssetManager::SaveMaterial(CreateRef<Material>(path.stem().string()), assetPath);
			m_SelectedAsset = assetPath;
			m_Editor.ClearSelection();
			m_LastRefreshTime = -1.0;
		}
		if (ImGui::MenuItem(ICON_CLAPPERBOARD "  Scene"))
		{
			const std::filesystem::path path = MakeUniquePath("NewScene", ".bscene");
			Scene scene(path.stem().string());
			SceneSerializer::Serialize(scene, path);
			m_LastRefreshTime = -1.0;
		}
	}

	void ContentBrowserPanel::DrawDialogs()
	{
		const float scale = Theme::GetScale();
		if (m_OpenRename)
		{
			ImGui::OpenPopup("Rename Asset");
			m_OpenRename = false;
			m_RenameError.clear();
			m_RenameFailedName.clear();
		}
		ImGui::SetNextWindowSize(ImVec2(380.0f * scale, 0.0f));
		if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::SetNextItemWidth(-FLT_MIN);
			const bool enter = ImGui::InputText("##name", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

			// Why a name can't be used is shown here, as it is typed, and the dialog stays open.
			const std::optional<std::string> problem = AssetFiles::CheckNewName(m_RenameBuffer);
			const std::string reason = problem ? *problem : m_RenameBuffer == m_RenameFailedName ? m_RenameError : std::string();
			if (!reason.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Error));
				ImGui::TextWrapped("This name can't be used: %s.", reason.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
			ImGui::BeginDisabled(problem.has_value());
			const bool confirmed = ImGui::Button("Rename", ImVec2(110.0f * scale, 0.0f)) || (enter && !problem);
			ImGui::EndDisabled();
			if (confirmed)
			{
				bool renamed = true;
				if (m_RenameBuffer != m_RenamePath.filename().string())
				{
					AssetManager::Invalidate(AssetManager::GetAssetPath(m_RenamePath));
					std::string error;
					renamed = AssetFiles::Rename(m_RenamePath, m_RenameBuffer, error);
					if (!renamed)
					{
						BW_ERROR("Could not rename '{}': {}", m_RenamePath.filename().string(), error);
						m_RenameError = error;
						m_RenameFailedName = m_RenameBuffer;
					}
				}
				m_LastRefreshTime = -1.0;
				if (renamed)
					ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(110.0f * scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (m_OpenDelete)
		{
			ImGui::OpenPopup("Delete Asset");
			m_OpenDelete = false;
		}
		ImGui::SetNextWindowSize(ImVec2(420.0f * scale, 0.0f));
		if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped("Move '%s' to the %s? You can restore it from there.", m_DeletePath.filename().string().c_str(), c_TrashName);
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::Error));
			if (ImGui::Button(ICON_TRASH_2 "  Delete", ImVec2(110.0f * scale, 0.0f)))
			{
				AssetManager::Invalidate(AssetManager::GetAssetPath(m_DeletePath));
				std::string error;
				if (!AssetFiles::Delete(m_DeletePath, error))
					BW_ERROR("Could not move '{}' to the {}: {}. Nothing was deleted.", m_DeletePath.filename().string(), c_TrashName, error);
				else if (!error.empty())
					BW_WARN("Moved '{}' to the {}, but {}", m_DeletePath.filename().string(), c_TrashName, error);
				if (m_SelectedAsset == AssetManager::GetAssetPath(m_DeletePath))
					m_SelectedAsset.clear();
				m_LastRefreshTime = -1.0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(110.0f * scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

}
