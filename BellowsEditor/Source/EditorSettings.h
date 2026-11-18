#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	// A project known to the Project Manager.
	struct ProjectEntry
	{
		std::string Path;			// The .bproject file
		int64_t LastOpened = 0;		// Seconds since the Unix epoch; 0 = never opened here
		bool Favorite = false;		// Favorites stay at the top of the list
	};

	// Which docked panels are shown (View > Panels). Closed panels come back from that menu.
	struct EditorPanelVisibility
	{
		bool Hierarchy = true;
		bool Inspector = true;
		bool Content = true;
		bool Console = true;
		bool Debugger = true;
		bool Settings = true;
	};

	// Per-user editor preferences, stored in the user's configuration folder
	// (Windows: %APPDATA%\MageCapBellows, Linux: ~/.config/MageCapBellows).
	struct EditorSettings
	{
		std::vector<ProjectEntry> Projects;
		bool AgentServerEnabled = true;
		uint16_t AgentServerPort = 7420;
		float UIScale = 0.0f;						// 0 = follow the monitor
		bool ShowGrid = true;
		float CameraSpeed = 6.0f;
		EditorPanelVisibility Panels;
		bool ShowContentFolderTree = true;
		std::string LastProjectLocation;			// Where the last new project was created
		bool PlayInGameWindow = true;				// Play runs the game in its own window (false: in the viewport)

		static constexpr size_t MaxProjects = 200;

		// Adds the project (or moves it to the top of the recent list) with 'now' as its last opened time.
		void AddRecentProject(const std::string& path, int64_t now);
		ProjectEntry* FindProject(const std::string& path);
		bool RemoveProject(const std::string& path);
		// The most recently opened projects, newest first.
		std::vector<std::string> GetRecentProjects(size_t count) const;

		// Writes Install.json to the configuration folder so other tools (MageCap Forge) can find this
		// editor: its path, the engine and ForgeScript versions, the forgescript tool and the docs.
		// Only rewritten when something changed. Returns false if it could not be written.
		static bool WriteInstallInfo(const std::filesystem::path& executableDirectory);

		static int64_t Now();
		static std::filesystem::path GetConfigDirectory();
		static EditorSettings Load();
		bool Save() const;
	};

}
