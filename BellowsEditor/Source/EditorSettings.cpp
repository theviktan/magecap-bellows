#include "EditorSettings.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Serialization/JsonUtils.h"

namespace Bellows {

	void EditorSettings::AddRecentProject(const std::string& path)
	{
		std::erase(RecentProjects, path);
		RecentProjects.insert(RecentProjects.begin(), path);
		if (RecentProjects.size() > 12)
			RecentProjects.resize(12);
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

		JsonRead(json, "RecentProjects", settings.RecentProjects);
		JsonRead(json, "AgentServerEnabled", settings.AgentServerEnabled);
		JsonRead(json, "AgentServerPort", settings.AgentServerPort);
		JsonRead(json, "UIScale", settings.UIScale);
		JsonRead(json, "ShowGrid", settings.ShowGrid);
		JsonRead(json, "CameraSpeed", settings.CameraSpeed);
		return settings;
	}

	bool EditorSettings::Save() const
	{
		Json json;
		json["RecentProjects"] = RecentProjects;
		json["AgentServerEnabled"] = AgentServerEnabled;
		json["AgentServerPort"] = AgentServerPort;
		json["UIScale"] = UIScale;
		json["ShowGrid"] = ShowGrid;
		json["CameraSpeed"] = CameraSpeed;
		return FileSystem::WriteTextFile(GetConfigDirectory() / "EditorSettings.json", json.dump(1, '\t'));
	}

}
