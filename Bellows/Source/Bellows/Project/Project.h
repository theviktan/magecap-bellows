#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/InputMap.h"

#include <filesystem>
#include <map>
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

		// Starting volume of each audio bus ("Master", "Music", "Effects"); scripts may change them while playing.
		std::map<std::string, float> BusVolumes = { { "Master", 1.0f }, { "Music", 1.0f }, { "Effects", 1.0f } };

		// Named input actions scripts ask for (is_action_down("jump")); new projects get InputMap's defaults.
		InputActionMap InputActions;

		// Which way the front of a model with no '.bimport' file faces (see MeshImportSettings). New
		// projects start at +Z; a project file without the setting (made before 0.5.0) means -Z.
		ModelFront ModelFrontFaces = ModelFront::NegativeZ;
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
		// Sets the audio buses to the project's starting volumes, unmuted (at game and play start).
		void ApplyAudioSettings() const;
		// Makes the project's input actions the ones scripts see.
		void ApplyInputSettings() const;
		// Hands ModelFrontFaces to the AssetManager, for models without their own import settings.
		void ApplyImportSettings() const;

		static Ref<Project> GetActive() { return s_ActiveProject; }
		static void SetActive(const Ref<Project>& project);

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }

		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		// The file the project was loaded from (or created as); renaming the project keeps this file.
		std::filesystem::path GetProjectFilePath() const { return m_ProjectDirectory / m_ProjectFileName; }
		std::filesystem::path GetAssetDirectory() const;
		// Per-project editor data (layout, agent session); not part of exported games.
		std::filesystem::path GetEditorDataDirectory() const { return m_ProjectDirectory / ".bellows"; }
	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;
		std::filesystem::path m_ProjectFileName;

		static Ref<Project> s_ActiveProject;
	};

}
