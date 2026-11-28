#include "EditorSettings.h"

#include "Bellows/Core/BuildInfo.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <algorithm>
#include <cmath>
#include <chrono>

namespace Bellows {

	void EditorSettings::AddRecentProject(const std::string& path, int64_t now)
	{
		if (ProjectEntry* entry = FindProject(path))
		{
			entry->LastOpened = now;
			return;
		}

		ProjectEntry entry;
		entry.Path = path;
		entry.LastOpened = now;
		Projects.push_back(entry);

		// Over the limit, forget the project opened longest ago (favorites are kept).
		while (Projects.size() > MaxProjects)
		{
			auto oldest = Projects.end();
			for (auto it = Projects.begin(); it != Projects.end(); ++it)
			{
				if (!it->Favorite && (oldest == Projects.end() || it->LastOpened < oldest->LastOpened))
					oldest = it;
			}
			if (oldest == Projects.end())
				break;
			Projects.erase(oldest);
		}
	}

	ProjectEntry* EditorSettings::FindProject(const std::string& path)
	{
		auto it = std::find_if(Projects.begin(), Projects.end(), [&path](const ProjectEntry& entry) { return entry.Path == path; });
		return it == Projects.end() ? nullptr : &*it;
	}

	bool EditorSettings::RemoveProject(const std::string& path)
	{
		return std::erase_if(Projects, [&path](const ProjectEntry& entry) { return entry.Path == path; }) > 0;
	}

	std::vector<std::string> EditorSettings::GetRecentProjects(size_t count) const
	{
		std::vector<const ProjectEntry*> sorted;
		for (const ProjectEntry& entry : Projects)
		{
			if (entry.LastOpened > 0)
				sorted.push_back(&entry);
		}
		std::stable_sort(sorted.begin(), sorted.end(), [](const ProjectEntry* a, const ProjectEntry* b) { return a->LastOpened > b->LastOpened; });

		std::vector<std::string> paths;
		for (size_t i = 0; i < sorted.size() && i < count; i++)
			paths.push_back(sorted[i]->Path);
		return paths;
	}

	bool EditorSettings::WriteInstallInfo(const std::filesystem::path& executableDirectory)
	{
		const std::string extension = Platform::GetExecutableExtension();
		const std::filesystem::path directory = std::filesystem::absolute(executableDirectory).lexically_normal();
		Json json;
		json["Engine"] = "MageCap Bellows";
		json["EngineVersion"] = BW_VERSION;
		json["EngineCommit"] = BuildInfo::GetCommit();
		json["ForgeScriptVersion"] = FORGESCRIPT_VERSION;
		json["EditorPath"] = (directory / ("BellowsEditor" + extension)).string();
		json["RuntimePath"] = (directory / ("BellowsRuntime" + extension)).string();

		const std::filesystem::path forgescript = directory / ("forgescript" + extension);
		json["ForgeScriptPath"] = FileSystem::Exists(forgescript) ? forgescript.string() : std::string();

		// Packages keep the guides in "Docs" next to the editor; a build tree has them in the repository's "docs".
		std::string docs;
		std::filesystem::path search = directory;
		for (int depth = 0; depth < 5 && docs.empty(); depth++)
		{
			for (const char* folder : { "Docs", "docs" })
			{
				if (docs.empty() && FileSystem::Exists(search / folder / "ScriptingAPI.md"))
					docs = (search / folder).string();
			}
			if (search == search.parent_path())
				break;
			search = search.parent_path();
		}
		json["DocsDirectory"] = docs;

		const std::filesystem::path path = GetConfigDirectory() / "Install.json";
		const std::string text = json.dump(1, '\t');
		if (const std::optional<std::string> existing = FileSystem::ReadTextFile(path); existing && *existing == text)
			return true;
		return FileSystem::WriteTextFile(path, text);
	}

	int64_t EditorSettings::Now()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::filesystem::path EditorSettings::GetConfigDirectory()
	{
		// Tests and portable setups can redirect all editor state (settings, layout, agent session).
		if (const std::string overrideDirectory = Platform::GetEnvVar("BELLOWS_CONFIG_DIR"); !overrideDirectory.empty())
			return std::filesystem::path(overrideDirectory);

#if defined(BW_PLATFORM_WINDOWS)
		const std::string appData = Platform::GetEnvVar("APPDATA");
		std::filesystem::path base = appData.empty() ? Platform::GetExecutableDirectory() : std::filesystem::path(appData);
#else
		std::string configHome = Platform::GetEnvVar("XDG_CONFIG_HOME");
		if (configHome.empty())
		{
			const std::string home = Platform::GetEnvVar("HOME");
			configHome = home.empty() ? Platform::GetExecutableDirectory().string() : home + "/.config";
		}
		std::filesystem::path base = configHome;
#endif
		return base / "MageCapBellows";
	}

	EditorSettings EditorSettings::Load()
	{
		EditorSettings settings;
		const std::optional<std::string> text = FileSystem::ReadTextFile(GetConfigDirectory() / "EditorSettings.json");
		if (!text)
			return settings;
		const Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded())
			return settings;

		if (auto projects = json.find("Projects"); projects != json.end() && projects->is_array())
		{
			for (const Json& item : *projects)
			{
				ProjectEntry entry;
				JsonRead(item, "Path", entry.Path);
				JsonRead(item, "LastOpened", entry.LastOpened);
				JsonRead(item, "Favorite", entry.Favorite);
				if (!entry.Path.empty() && !settings.FindProject(entry.Path))
					settings.Projects.push_back(entry);
			}
		}
		else
		{
			// Settings from before the Project Manager kept a plain list, most recent first.
			std::vector<std::string> recent;
			JsonRead(json, "RecentProjects", recent);
			const int64_t now = Now();
			for (size_t i = 0; i < recent.size(); i++)
				settings.AddRecentProject(recent[i], now - static_cast<int64_t>(i));
		}

		JsonRead(json, "AgentServerEnabled", settings.AgentServerEnabled);
		JsonRead(json, "AgentServerPort", settings.AgentServerPort);
		JsonRead(json, "UIScale", settings.UIScale);
		JsonRead(json, "ShowGrid", settings.ShowGrid);
		JsonRead(json, "ShowUI", settings.ShowUI);
		JsonRead(json, "UIView", settings.UIView);
		JsonRead(json, "UIViewWidth", settings.UIViewWidth);
		JsonRead(json, "UIViewHeight", settings.UIViewHeight);
		settings.UIViewWidth = std::clamp(settings.UIViewWidth, 64u, 8192u);
		settings.UIViewHeight = std::clamp(settings.UIViewHeight, 64u, 8192u);
		JsonRead(json, "CameraSpeed", settings.CameraSpeed);
		JsonRead(json, "Snap", settings.Snap);
		JsonRead(json, "TranslateSnap", settings.TranslateSnap);
		JsonRead(json, "RotateSnap", settings.RotateSnap);
		JsonRead(json, "ScaleSnap", settings.ScaleSnap);
		// A step of 0 (or a hand-edited NaN) would make the gizmo stick, so each is held to a sane range.
		const EditorSettings defaults;
		const auto step = [](float value, float minimum, float maximum, float fallback)
		{
			return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
		};
		settings.TranslateSnap = step(settings.TranslateSnap, 0.001f, 1000.0f, defaults.TranslateSnap);
		settings.RotateSnap = step(settings.RotateSnap, 0.1f, 180.0f, defaults.RotateSnap);
		settings.ScaleSnap = step(settings.ScaleSnap, 0.001f, 100.0f, defaults.ScaleSnap);
		JsonRead(json, "SelectionGlow", settings.SelectionGlow);
		JsonRead(json, "ShowContentFolderTree", settings.ShowContentFolderTree);
		JsonRead(json, "LastProjectLocation", settings.LastProjectLocation);
		JsonRead(json, "PlayInGameWindow", settings.PlayInGameWindow);
		if (auto panels = json.find("Panels"); panels != json.end())
		{
			JsonRead(*panels, "Hierarchy", settings.Panels.Hierarchy);
			JsonRead(*panels, "Inspector", settings.Panels.Inspector);
			JsonRead(*panels, "Content", settings.Panels.Content);
			JsonRead(*panels, "Console", settings.Panels.Console);
			JsonRead(*panels, "Debugger", settings.Panels.Debugger);
			JsonRead(*panels, "Settings", settings.Panels.Settings);
		}
		return settings;
	}

	bool EditorSettings::Save() const
	{
		Json json;
		Json projects = Json::array();
		for (const ProjectEntry& entry : Projects)
			projects.push_back({ { "Path", entry.Path }, { "LastOpened", entry.LastOpened }, { "Favorite", entry.Favorite } });
		json["Projects"] = projects;
		json["AgentServerEnabled"] = AgentServerEnabled;
		json["AgentServerPort"] = AgentServerPort;
		json["UIScale"] = UIScale;
		json["ShowGrid"] = ShowGrid;
		json["ShowUI"] = ShowUI;
		json["UIView"] = UIView;
		json["UIViewWidth"] = UIViewWidth;
		json["UIViewHeight"] = UIViewHeight;
		json["CameraSpeed"] = CameraSpeed;
		json["Snap"] = Snap;
		json["TranslateSnap"] = TranslateSnap;
		json["RotateSnap"] = RotateSnap;
		json["ScaleSnap"] = ScaleSnap;
		json["SelectionGlow"] = SelectionGlow;
		json["ShowContentFolderTree"] = ShowContentFolderTree;
		json["LastProjectLocation"] = LastProjectLocation;
		json["PlayInGameWindow"] = PlayInGameWindow;
		json["Panels"] = {
			{ "Hierarchy", Panels.Hierarchy },
			{ "Inspector", Panels.Inspector },
			{ "Content", Panels.Content },
			{ "Console", Panels.Console },
			{ "Debugger", Panels.Debugger },
			{ "Settings", Panels.Settings },
		};
		return FileSystem::WriteTextFile(GetConfigDirectory() / "EditorSettings.json", json.dump(1, '\t'));
	}

}
