#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	// Per-user editor preferences, stored in the user's configuration folder
	// (Windows: %APPDATA%\MageCapBellows, Linux: ~/.config/MageCapBellows).
	struct EditorSettings
	{
		std::vector<std::string> RecentProjects;	// Most recent first
		bool AgentServerEnabled = true;
		uint16_t AgentServerPort = 7420;
		float UIScale = 0.0f;						// 0 = follow the monitor
		bool ShowGrid = true;
		float CameraSpeed = 6.0f;

		void AddRecentProject(const std::string& path);

		static std::filesystem::path GetConfigDirectory();
		static EditorSettings Load();
		bool Save() const;
	};

}
