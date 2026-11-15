#pragma once

#include "Bellows/Core/Base.h"

#include <filesystem>
#include <string>

namespace Bellows {

	struct ProjectConfig
	{
		std::string Name = "Untitled";
		std::string StartScene = "Scenes/Main.bscene";	// Relative to the asset directory
		std::string AssetDirectory = "Assets";			// Relative to the project folder

		// Settings used by exported games
		uint32_t WindowWidth = 1600;
		uint32_t WindowHeight = 900;
		bool Fullscreen = false;
		bool VSync = true;
	};

	// A game project: a folder containing '<Name>.bproject' and an asset directory.
	class Project
	{
	public:
		static constexpr const char* FileExtension = ".bproject";

		// Creates the folder structure, a default scene and the project file.
		static Ref<Project> Create(const std::filesystem::path& directory, const std::string& name);
		// Loads a .bproject file (or the first .bproject inside a folder) and makes it active.
		static Ref<Project> Load(const std::filesystem::path& path);
		bool Save();

		static Ref<Project> GetActive() { return s_ActiveProject; }
		static void SetActive(const Ref<Project>& project);

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }

		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		std::filesystem::path GetProjectFilePath() const;
		std::filesystem::path GetAssetDirectory() const;
		// Per-project editor data (layout, agent session); not part of exported games.
		std::filesystem::path GetEditorDataDirectory() const { return m_ProjectDirectory / ".bellows"; }
	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;

		static Ref<Project> s_ActiveProject;
	};

}
