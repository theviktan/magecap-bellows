#include "ProjectManager.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Core/BuildInfo.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Project/Project.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace Bellows {

	namespace {

		constexpr double c_RefreshInterval = 2.0;	// Seconds between checks for projects that went missing
		constexpr double c_StatusDuration = 6.0;	// Seconds a status message stays visible

		bool PrimaryButton(const char* label, const ImVec2& size, bool enabled = true)
		{
			ImGui::BeginDisabled(!enabled);
			UI::PushPrimaryButtonColors();
			ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
			const bool pressed = ImGui::Button(label, size);
			ImGui::PopFont();
			UI::PopPrimaryButtonColors();
			ImGui::EndDisabled();
			return pressed;
		}

		const char* SortName(ProjectSort sort)
		{
			switch (sort)
			{
				case ProjectSort::LastOpened:	return "Last Opened";
				case ProjectSort::Name:			return "Name";
				case ProjectSort::Path:			return "Path";
			}
			return "Last Opened";
		}

	}

	ProjectManager::ProjectManager(EditorLayer& editor)
		: m_Editor(editor)
	{
		m_SampleProject = FindSampleProject();
	}

	// -------------------------------------------------------------------------
	// Data
	// -------------------------------------------------------------------------

	void ProjectManager::Refresh()
	{
		m_LastRefresh = ImGui::GetTime();
		std::vector<ProjectListItem> items;
		for (const ProjectEntry& entry : m_Editor.GetSettings().Projects)
		{
			ProjectListItem item;
			item.Entry = entry;
			item.Name = ReadProjectName(entry.Path);
			item.Missing = item.Name.empty();
			if (item.Missing)
				item.Name = std::filesystem::path(entry.Path).stem().string();
			items.push_back(std::move(item));
		}
		m_Items = SortProjects(std::move(items), m_Sort, m_Filter);

		// Like Godot, the top project starts selected so Enter or Open works right away.
		if (!GetSelected() && !m_Items.empty())
			m_Selected = m_Items.front().Entry.Path;
	}

	const ProjectListItem* ProjectManager::GetSelected() const
	{
		auto it = std::find_if(m_Items.begin(), m_Items.end(), [this](const ProjectListItem& item) { return item.Entry.Path == m_Selected; });
		return it == m_Items.end() ? nullptr : &*it;
	}

	void ProjectManager::SetStatus(const std::string& message, bool error)
	{
		m_Status = message;
		m_StatusIsError = error;
		m_StatusTime = ImGui::GetTime();
	}

	std::filesystem::path ProjectManager::FindSampleProject() const
	{
		// Packages ship Projects/FeatureTest next to the editor; a build tree has it in the repository.
		std::filesystem::path directory = Platform::GetExecutableDirectory();
		for (int depth = 0; depth < 5 && !directory.empty(); depth++)
		{
			const std::filesystem::path sample = directory / "Projects" / "FeatureTest";
			if (FileSystem::Exists(sample))
			{
				const std::vector<std::filesystem::path> found = FindProjects(sample, 0);
				if (!found.empty())
					return found.front();
			}
			if (directory == directory.parent_path())
				break;
			directory = directory.parent_path();
		}
		return {};
	}

	std::filesystem::path ProjectManager::GetDefaultLocation() const
	{
		const EditorSettings& settings = m_Editor.GetSettings();
		if (!settings.LastProjectLocation.empty())
			return settings.LastProjectLocation;
#if defined(BW_PLATFORM_WINDOWS)
		const std::string home = Platform::GetEnvVar("USERPROFILE");
#else
		const std::string home = Platform::GetEnvVar("HOME");
#endif
		if (home.empty())
			return std::filesystem::current_path();
		const std::filesystem::path documents = std::filesystem::path(home) / "Documents";
		return (FileSystem::Exists(documents) ? documents : std::filesystem::path(home)) / "Bellows Projects";
	}

	void ProjectManager::OpenProject(const std::string& path)
	{
		if (!FileSystem::Exists(path))
		{
			SetStatus("That project is missing. It may have been moved or deleted.", true);
			return;
		}
		// Opening replaces this window, so it happens after the frame.
		Application::Get().SubmitToMainThread([this, path]
		{
			if (!m_Editor.OpenProject(path))
				SetStatus("The project could not be opened. The Console has the details.", true);
		});
	}

	void ProjectManager::ImportProject()
	{
		const std::string file = UI::OpenFileDialog("Import a project", { "MageCap Bellows project", "*.bproject" }, GetDefaultLocation().string());
		if (file.empty())
			return;
		if (std::filesystem::path(file).extension() != Project::FileExtension)
		{
			SetStatus("Choose a .bproject file.", true);
			return;
		}
		OpenProject(file);
	}

	void ProjectManager::ScanFolder()
	{
		const std::string folder = UI::SelectFolderDialog("Scan a folder for projects", GetDefaultLocation().string());
		if (folder.empty())
			return;

		EditorSettings& settings = m_Editor.GetSettings();
		size_t added = 0;
		const std::vector<std::filesystem::path> found = FindProjects(folder);
		for (const std::filesystem::path& project : found)
		{
			const std::string path = std::filesystem::absolute(project).string();
			if (settings.FindProject(path))
				continue;
			ProjectEntry entry;
			entry.Path = path;
			settings.Projects.push_back(entry);
			added++;
		}
		settings.Save();
		Invalidate();
		if (found.empty())
			SetStatus("No projects were found in that folder.", true);
		else
			SetStatus("Found " + std::to_string(found.size()) + (found.size() == 1 ? " project" : " projects") + ", " + std::to_string(added) + " new.");
	}

	// -------------------------------------------------------------------------
	// UI
	// -------------------------------------------------------------------------

	void ProjectManager::OnImGuiRender()
	{
		if (m_LastRefresh < 0.0 || ImGui::GetTime() - m_LastRefresh > c_RefreshInterval)
			Refresh();

		const float scale = Theme::GetScale();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::ToVec4(Theme::Background));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##ProjectManager", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		const float headerHeight = 64.0f * scale;
		DrawHeader(headerHeight);

		const float padding = 24.0f * scale;
		const float actionsWidth = 210.0f * scale;
		const ImVec2 size = ImGui::GetWindowSize();
		ImGui::SetCursorPos(ImVec2(padding, headerHeight + padding * 0.75f));
		ImGui::BeginChild("##Main", ImVec2(size.x - actionsWidth - padding * 3.0f, size.y - headerHeight - padding * 1.75f));
		DrawToolbar();
		ImGui::Spacing();
		if (m_Editor.GetSettings().Projects.empty())
			DrawEmptyState();
		else
			DrawList();
		ImGui::EndChild();

		ImGui::SetCursorPos(ImVec2(size.x - actionsWidth - padding, headerHeight + padding * 0.75f));
		ImGui::BeginChild("##Actions", ImVec2(actionsWidth, size.y - headerHeight - padding * 1.75f));
		DrawActions();
		ImGui::EndChild();

		ImGui::End();
		DrawDialogs();
	}

	void ProjectManager::DrawHeader(float height)
	{
		const float scale = Theme::GetScale();
		const ImVec2 position = ImGui::GetWindowPos();
		const float width = ImGui::GetWindowWidth();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(position, ImVec2(position.x + width, position.y + height), Theme::Panel);
		drawList->AddLine(ImVec2(position.x, position.y + height), ImVec2(position.x + width, position.y + height), Theme::Border);

		const ImVec2 markCenter(position.x + 42.0f * scale, position.y + height * 0.5f);
		if (const Ref<Texture2D>& logo = m_Editor.GetLogo())
		{
			const float half = 20.0f * scale;
			drawList->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(logo->GetHandle())),
				ImVec2(markCenter.x - half, markCenter.y - half), ImVec2(markCenter.x + half, markCenter.y + half));
		}
		else
		{
			drawList->AddCircleFilled(markCenter, 17.0f * scale, Theme::AccentSubtle, 40);
			drawList->AddCircle(markCenter, 17.0f * scale, Theme::Accent, 40, 1.5f * scale);
			const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FLAME);
			drawList->AddText(ImVec2(markCenter.x - iconSize.x * 0.5f, markCenter.y - iconSize.y * 0.5f), Theme::Accent, ICON_FLAME);
		}

		ImFont* bold = Theme::GetFonts().Bold;
		const float titleSize = Theme::BaseFontSize * scale * 1.35f;
		drawList->AddText(bold, titleSize, ImVec2(position.x + 72.0f * scale, position.y + height * 0.5f - titleSize - 1.0f * scale), Theme::Text, "MageCap Bellows");
		drawList->AddText(ImVec2(position.x + 72.0f * scale, position.y + height * 0.5f + 2.0f * scale), Theme::TextMuted, "Project Manager");

		const std::string version = "Version " + BuildInfo::GetVersionText();
		const float versionWidth = ImGui::CalcTextSize(version.c_str()).x;
		drawList->AddText(ImVec2(position.x + width - versionWidth - 24.0f * scale, position.y + height * 0.5f - ImGui::GetFontSize() * 0.5f),
			Theme::TextDisabled, version.c_str());
	}

	void ProjectManager::DrawToolbar()
	{
		const float scale = Theme::GetScale();
		const float height = ImGui::GetFrameHeight() + 6.0f * scale;

		if (PrimaryButton(ICON_FOLDER_PLUS "  New Project", ImVec2(0.0f, height)))
			ShowNewProjectDialog();
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_INPUT "  Import", ImVec2(0.0f, height)))
			ImportProject();
		UI::Tooltip("Open an existing .bproject file and add it to the list");
		ImGui::SameLine();
		if (ImGui::Button(ICON_SEARCH "  Scan", ImVec2(0.0f, height)))
			ScanFolder();
		UI::Tooltip("Find every project inside a folder and add them to the list");
		ImGui::SameLine();
		// Updates: the accent Update button while a new version waits, else a check (as Help, Check for Updates).
		if (m_Editor.IsUpdateOffered())
		{
			if (PrimaryButton(ICON_DOWNLOAD "  Update", ImVec2(0.0f, height)))
				m_Editor.ShowUpdateDialog();
			UI::Tooltip("A new version of MageCap Bellows is available: see what's new and install it");
		}
		else
		{
			ImGui::BeginDisabled(m_Editor.GetUpdateChecker().IsBusy());
			if (ImGui::Button(ICON_REFRESH_CW "  Check for Updates", ImVec2(0.0f, height)))
				m_Editor.CheckForUpdates(true);
			ImGui::EndDisabled();
			UI::Tooltip("Look for a newer version of MageCap Bellows");
		}

		// Search and sort on the right.
		const float sortWidth = 150.0f * scale;
		const float searchWidth = std::min(320.0f * scale, ImGui::GetContentRegionAvail().x - sortWidth - 40.0f * scale);
		ImGui::SameLine(ImGui::GetWindowWidth() - searchWidth - sortWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y + 3.0f * scale));
		ImGui::SetNextItemWidth(searchWidth);
		if (ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Filter projects", &m_Filter))
			Invalidate();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(sortWidth);
		if (ImGui::BeginCombo("##sort", SortName(m_Sort)))
		{
			for (ProjectSort sort : { ProjectSort::LastOpened, ProjectSort::Name, ProjectSort::Path })
			{
				if (ImGui::Selectable(SortName(sort), m_Sort == sort))
				{
					m_Sort = sort;
					Invalidate();
				}
			}
			ImGui::EndCombo();
		}
		UI::Tooltip("Sort by");
		ImGui::PopStyleVar();

		if (!m_Status.empty() && ImGui::GetTime() - m_StatusTime < c_StatusDuration)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(m_StatusIsError ? Theme::Warning : Theme::Success));
			ImGui::TextUnformatted(m_Status.c_str());
			ImGui::PopStyleColor();
		}
	}

	void ProjectManager::DrawList()
	{
		const float scale = Theme::GetScale();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(Theme::Panel));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f * scale);
		ImGui::BeginChild("##Projects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		if (m_Items.empty())
			UI::CenteredText("No projects match the filter.");

		const int64_t now = EditorSettings::Now();
		std::string toggleFavorite;
		for (const ProjectListItem& item : m_Items)
		{
			ImGui::PushID(item.Entry.Path.c_str());
			const bool selected = item.Entry.Path == m_Selected;
			const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
			const float rowHeight = 60.0f * scale;
			const float rowWidth = ImGui::GetContentRegionAvail().x;

			if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight)))
			{
				m_Selected = item.Entry.Path;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					OpenProject(item.Entry.Path);
			}
			if (ImGui::BeginPopupContextItem("##ProjectContext"))
			{
				m_Selected = item.Entry.Path;
				if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Open", nullptr, false, !item.Missing))
					OpenProject(item.Entry.Path);
				if (ImGui::MenuItem(ICON_STAR "  Favorite", nullptr, item.Entry.Favorite))
					toggleFavorite = item.Entry.Path;
				if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Show in File Manager", nullptr, false, !item.Missing))
					Platform::OpenWithDefaultApplication(std::filesystem::path(item.Entry.Path).parent_path());
				if (ImGui::MenuItem(ICON_COPY "  Copy Path"))
					ImGui::SetClipboardText(item.Entry.Path.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem(ICON_X "  Remove from List"))
				{
					m_Editor.GetSettings().RemoveProject(item.Entry.Path);
					m_Editor.GetSettings().Save();
					Invalidate();
				}
				ImGui::EndPopup();
			}

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 nameColor = item.Missing ? Theme::TextDisabled : Theme::Text;

			// Favorite star (a button over the row)
			ImGui::SetCursorScreenPos(ImVec2(rowPosition.x + 8.0f * scale, rowPosition.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f));
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::FieldHovered));
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(item.Entry.Favorite ? Theme::Warning : Theme::TextDisabled));
			if (ImGui::Button(ICON_STAR "##favorite", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
				toggleFavorite = item.Entry.Path;
			ImGui::PopStyleColor(3);
			UI::Tooltip(item.Entry.Favorite ? "Favorite (click to unpin)" : "Pin to the top as a favorite");

			// Project tile, name and folder
			const ImVec2 tileMin(rowPosition.x + 44.0f * scale, rowPosition.y + 12.0f * scale);
			const ImVec2 tileMax(tileMin.x + 36.0f * scale, tileMin.y + 36.0f * scale);
			drawList->AddRectFilled(tileMin, tileMax, item.Missing ? Theme::Field : Theme::PanelRaised, 7.0f * scale);
			const ImVec2 iconSize = ImGui::CalcTextSize(ICON_PACKAGE);
			drawList->AddText(ImVec2((tileMin.x + tileMax.x - iconSize.x) * 0.5f, (tileMin.y + tileMax.y - iconSize.y) * 0.5f),
				item.Missing ? Theme::TextDisabled : Theme::NeutralBright, ICON_PACKAGE);

			const float textX = tileMax.x + 14.0f * scale;
			drawList->AddText(Theme::GetFonts().SemiBold, Theme::BaseFontSize * scale * 1.05f, ImVec2(textX, rowPosition.y + 11.0f * scale), nameColor, item.Name.c_str());
			const std::string folder = std::filesystem::path(item.Entry.Path).parent_path().string();
			const ImVec4 clip(textX, rowPosition.y, rowPosition.x + rowWidth - 150.0f * scale, rowPosition.y + rowHeight);
			drawList->AddText(Theme::GetFonts().Regular, Theme::BaseFontSize * scale * 0.9f, ImVec2(textX, rowPosition.y + 33.0f * scale),
				Theme::TextMuted, folder.c_str(), nullptr, 0.0f, &clip);

			// When it was last opened, or that it is missing
			const std::string when = item.Missing ? std::string(ICON_TRIANGLE_ALERT "  Missing") : FormatTimeAgo(item.Entry.LastOpened, now);
			const float whenWidth = ImGui::CalcTextSize(when.c_str()).x;
			drawList->AddText(ImVec2(rowPosition.x + rowWidth - whenWidth - 16.0f * scale, rowPosition.y + (rowHeight - ImGui::GetFontSize()) * 0.5f),
				item.Missing ? Theme::Warning : Theme::TextDisabled, when.c_str());

			ImGui::SetCursorScreenPos(ImVec2(rowPosition.x, rowPosition.y + rowHeight));
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
			ImGui::PopID();
		}

		// Keyboard: Enter opens the selected project.
		if (ImGui::IsWindowFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
		{
			if (const ProjectListItem* item = GetSelected(); item && !item->Missing)
				OpenProject(item->Entry.Path);
		}
		ImGui::EndChild();

		if (!toggleFavorite.empty())
		{
			if (ProjectEntry* entry = m_Editor.GetSettings().FindProject(toggleFavorite))
			{
				entry->Favorite = !entry->Favorite;
				m_Editor.GetSettings().Save();
				Invalidate();
			}
		}
	}

	void ProjectManager::DrawEmptyState()
	{
		const float scale = Theme::GetScale();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float blockWidth = 440.0f * scale;
		ImGui::SetCursorPos(ImVec2(std::max((available.x - blockWidth) * 0.5f, 0.0f), ImGui::GetCursorPosY() + available.y * 0.25f));
		ImGui::BeginGroup();
		ImGui::PushFont(Theme::GetFonts().SemiBold, Theme::BaseFontSize * scale * 1.3f);
		ImGui::TextUnformatted("No projects yet");
		ImGui::PopFont();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + blockWidth);
		UI::TextMuted("Create a new project to start a game, or use Import to open a project you already have. "
			"Every project you create or open is listed here.");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Spacing();
		// Same text as the toolbar's button in the same window, so it needs its own ID.
		if (PrimaryButton(ICON_FOLDER_PLUS "  New Project##EmptyState", ImVec2(0.0f, ImGui::GetFrameHeight() + 6.0f * scale)))
			ShowNewProjectDialog();
		if (!m_SampleProject.empty())
		{
			ImGui::SameLine();
			if (ImGui::Button(ICON_GAMEPAD_2 "  Open the Sample Project", ImVec2(0.0f, ImGui::GetFrameHeight() + 6.0f * scale)))
				OpenProject(m_SampleProject.string());
			UI::Tooltip("Feature Test: a scene that uses every component, with Python scripts");
		}
		ImGui::EndGroup();
	}

	void ProjectManager::DrawActions()
	{
		const float scale = Theme::GetScale();
		const ImVec2 buttonSize(-FLT_MIN, ImGui::GetFrameHeight() + 6.0f * scale);
		const ProjectListItem* selected = GetSelected();
		const bool canOpen = selected && !selected->Missing;

		UI::TextMuted(selected ? "Selected project" : "Select a project");
		ImGui::Spacing();
		if (PrimaryButton(ICON_FOLDER_OPEN "  Open", buttonSize, canOpen))
			OpenProject(selected->Entry.Path);
		ImGui::BeginDisabled(!canOpen);
		if (ImGui::Button(ICON_EXTERNAL_LINK "  Show in File Manager", buttonSize))
			Platform::OpenWithDefaultApplication(std::filesystem::path(selected->Entry.Path).parent_path());
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!selected);
		if (ImGui::Button(selected && selected->Entry.Favorite ? ICON_STAR "  Unfavorite" : ICON_STAR "  Favorite", buttonSize))
		{
			if (ProjectEntry* entry = m_Editor.GetSettings().FindProject(selected->Entry.Path))
			{
				entry->Favorite = !entry->Favorite;
				m_Editor.GetSettings().Save();
				Invalidate();
			}
		}
		if (ImGui::Button(ICON_X "  Remove from List", buttonSize))
		{
			m_Editor.GetSettings().RemoveProject(selected->Entry.Path);
			m_Editor.GetSettings().Save();
			SetStatus("Removed from the list. The project's files were not touched.");
			Invalidate();
		}
		ImGui::EndDisabled();
		UI::Tooltip("Forgets the project here; its files stay on disk");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const bool anyMissing = std::any_of(m_Items.begin(), m_Items.end(), [](const ProjectListItem& item) { return item.Missing; });
		ImGui::BeginDisabled(!anyMissing);
		if (ImGui::Button(ICON_TRASH_2 "  Remove Missing", buttonSize))
		{
			EditorSettings& settings = m_Editor.GetSettings();
			std::erase_if(settings.Projects, [](const ProjectEntry& entry) { return !FileSystem::Exists(entry.Path); });
			settings.Save();
			Invalidate();
		}
		ImGui::EndDisabled();
		UI::Tooltip("Forgets every project whose folder was moved or deleted");

		if (!m_SampleProject.empty() && !m_Editor.GetSettings().Projects.empty())
		{
			ImGui::Spacing();
			if (ImGui::Button(ICON_GAMEPAD_2 "  Sample Project", buttonSize))
				OpenProject(m_SampleProject.string());
			UI::Tooltip("Feature Test: a scene that uses every component, with Python scripts");
		}
	}

	// -------------------------------------------------------------------------
	// New Project dialog
	// -------------------------------------------------------------------------

	void ProjectManager::ShowNewProjectDialog()
	{
		m_NewName = "My Game";
		m_NewLocation = GetDefaultLocation().string();
		m_NewCreateFolder = true;
		m_NewEmptyScene = false;
		m_OpenNewProject = true;
	}

	void ProjectManager::DrawDialogs()
	{
		DrawNewProjectDialog();
	}

	void ProjectManager::DrawNewProjectDialog()
	{
		if (m_OpenNewProject)
		{
			ImGui::OpenPopup("New Project");
			m_OpenNewProject = false;
		}

		const float scale = Theme::GetScale();
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(560.0f * scale, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 16.0f * scale));
		const bool open = ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::PopStyleVar();
		if (!open)
			return;

		UI::TextMuted("Project name");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##name", &m_NewName);

		ImGui::Spacing();
		UI::TextMuted("Location");
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
		ImGui::InputText("##location", &m_NewLocation);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_OPEN, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
		{
			const std::string chosen = UI::SelectFolderDialog("Choose where to put the project", m_NewLocation);
			if (!chosen.empty())
				m_NewLocation = chosen;
		}
		UI::Tooltip("Browse...");
		ImGui::Checkbox("Create a folder for the project", &m_NewCreateFolder);

		// Template cards
		ImGui::Spacing();
		UI::TextMuted("Start with");
		struct Template { const char* Title; const char* Description; bool Empty; };
		const Template templates[] = {
			{ ICON_BOX "  3D Starter", "A camera, sunlight, sky, a ground plane and a cube, ready to play.", false },
			{ ICON_SQUARE "  Empty", "An empty scene. Add everything yourself.", true },
		};
		const float cardWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		for (int i = 0; i < 2; i++)
		{
			const Template& card = templates[i];
			if (i > 0)
				ImGui::SameLine();
			ImGui::PushID(i);
			const bool selected = m_NewEmptyScene == card.Empty;
			const ImVec2 cardPosition = ImGui::GetCursorScreenPos();
			const float cardHeight = 74.0f * scale;
			if (ImGui::Selectable("##card", selected, ImGuiSelectableFlags_None, ImVec2(cardWidth, cardHeight)))
				m_NewEmptyScene = card.Empty;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRect(cardPosition, ImVec2(cardPosition.x + cardWidth, cardPosition.y + cardHeight), selected ? Theme::Accent : Theme::Border,
				6.0f * scale, selected ? 1.5f * scale : 1.0f);
			drawList->AddText(Theme::GetFonts().SemiBold, Theme::BaseFontSize * scale, ImVec2(cardPosition.x + 12.0f * scale, cardPosition.y + 10.0f * scale),
				selected ? Theme::AccentHovered : Theme::Text, card.Title);
			const ImVec4 clip(cardPosition.x, cardPosition.y, cardPosition.x + cardWidth - 10.0f * scale, cardPosition.y + cardHeight);
			drawList->AddText(Theme::GetFonts().Regular, Theme::BaseFontSize * scale * 0.9f, ImVec2(cardPosition.x + 12.0f * scale, cardPosition.y + 34.0f * scale),
				Theme::TextMuted, card.Description, nullptr, cardWidth - 24.0f * scale, &clip);
			ImGui::PopID();
		}

		// What will happen, like Godot's colored hint under the path.
		ImGui::Spacing();
		const NewProjectCheck check = CheckNewProjectLocation(m_NewName, std::filesystem::path(m_NewLocation), m_NewCreateFolder);
		const ImU32 color = check.Status == NewProjectCheck::Level::Ok ? Theme::Success
			: (check.Status == NewProjectCheck::Level::Warning ? Theme::Warning : Theme::Error);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(color));
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(check.Message.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		if (!check.ProjectFolder.empty())
		{
			ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", check.ProjectFolder.string().c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopFont();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const bool valid = check.Status != NewProjectCheck::Level::Error;
		if (PrimaryButton("Create & Open", ImVec2(140.0f * scale, 0.0f), valid))
		{
			const std::filesystem::path folder = check.ProjectFolder;
			std::string name = m_NewName;
			while (!name.empty() && name.back() == ' ')
				name.pop_back();
			while (!name.empty() && name.front() == ' ')
				name.erase(name.begin());
			const bool emptyScene = m_NewEmptyScene;

			EditorSettings& settings = m_Editor.GetSettings();
			settings.LastProjectLocation = m_NewCreateFolder ? m_NewLocation : std::filesystem::path(m_NewLocation).parent_path().string();
			settings.Save();
			ImGui::CloseCurrentPopup();
			// Creating opens the project, which replaces this window, so it happens after the frame.
			Application::Get().SubmitToMainThread([this, folder, name, emptyScene]
			{
				if (!m_Editor.NewProject(folder, name, emptyScene))
					SetStatus("The project could not be created in '" + folder.string() + "'. The Console has the details.", true);
			});
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f * scale, 0.0f)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

}
