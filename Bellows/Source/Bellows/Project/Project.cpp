#include "bwpch.h"
#include "Bellows/Project/Project.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Project/ProjectExporter.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	Ref<Project> Project::s_ActiveProject;

	namespace {

		// A new project starts with a lit, framed scene so the first thing users see looks right.
		void WriteDefaultScene(const std::filesystem::path& path)
		{
			Scene scene("Main");

			Entity camera = scene.CreateEntity("Main Camera");
			camera.AddComponent<CameraComponent>();
			camera.GetTransform().Translation = { 0.0f, 2.5f, 7.0f };
			camera.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-15.0f, 0.0f, 0.0f)));

			Entity sun = scene.CreateEntity("Sun");
			sun.AddComponent<DirectionalLightComponent>();
			sun.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-50.0f, 70.0f, 0.0f)));

			Entity sky = scene.CreateEntity("Sky");
			sky.AddComponent<SkyLightComponent>();

			Entity ground = scene.CreateEntity("Ground");
			ground.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Plane";
			ground.GetTransform().Scale = { 20.0f, 1.0f, 20.0f };
			ground.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
			ground.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.01f, 0.5f };

			Entity cube = scene.CreateEntity("Cube");
			cube.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Cube";
			cube.GetTransform().Translation = { 0.0f, 0.5f, 0.0f };

			SceneSerializer::Serialize(scene, path);
		}

	}

	void Project::SetActive(const Ref<Project>& project)
	{
		s_ActiveProject = project;
		AssetManager::SetAssetDirectory(project ? project->GetAssetDirectory() : std::filesystem::path());
	}

	std::filesystem::path Project::GetAssetDirectory() const
	{
		return m_ProjectDirectory / m_Config.AssetDirectory;
	}

	Ref<Project> Project::Create(const std::filesystem::path& directory, const std::string& name)
	{
		if (name.empty())
		{
			BW_CORE_ERROR("A project needs a name");
			return nullptr;
		}

		Ref<Project> project = CreateRef<Project>();
		project->m_ProjectDirectory = std::filesystem::absolute(directory);
		project->m_Config.Name = name;
		// The file is named after the cleaned name: the name itself may hold anything ("../../Escape",
		// "Tëst?"), and a file named after it could land outside the folder or not be allowed at all.
		project->m_ProjectFileName = ProjectExporter::SanitizeName(name) + FileExtension;
		project->m_Config.InputActions = InputMap::GetDefaultActions();
		// New projects turn +Z-front models (Blender's and glTF's convention) to face forward; projects
		// made before 0.5.0 have no setting and keep -Z, so nothing already placed turns around.
		project->m_Config.ModelFrontFaces = ModelFront::PositiveZ;

		const std::filesystem::path assets = project->GetAssetDirectory();
		for (const char* folder : { "Scenes", "Scripts", "Models", "Materials", "Textures", "Environments", "Audio", "Prefabs" })
		{
			if (!FileSystem::CreateDirectories(assets / folder))
			{
				BW_CORE_ERROR("Could not create project folder '{}'", (assets / folder).string());
				return nullptr;
			}
		}

		const std::filesystem::path startScene = assets / project->m_Config.StartScene;
		if (!FileSystem::Exists(startScene))
			WriteDefaultScene(startScene);

		if (!project->Save())
			return nullptr;
		SetActive(project);
		return project;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		std::filesystem::path projectFile = path;
		std::error_code error;
		if (std::filesystem::is_directory(path, error))
		{
			projectFile.clear();
			for (const auto& entry : std::filesystem::directory_iterator(path, error))
			{
				if (entry.path().extension() == FileExtension)
				{
					projectFile = entry.path();
					break;
				}
			}
			if (projectFile.empty())
			{
				BW_CORE_ERROR("No {} file in '{}'", FileExtension, path.string());
				return nullptr;
			}
		}

		const std::optional<std::string> text = FileSystem::ReadTextFile(projectFile);
		if (!text)
		{
			BW_CORE_ERROR("Project file '{}' could not be read", projectFile.string());
			return nullptr;
		}
		const Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			BW_CORE_ERROR("Project file '{}' is not valid JSON", projectFile.string());
			return nullptr;
		}

		Ref<Project> project = CreateRef<Project>();
		project->m_ProjectDirectory = std::filesystem::absolute(projectFile).parent_path();
		project->m_ProjectFileName = projectFile.filename();
		ProjectConfig& config = project->m_Config;
		config.Name = projectFile.stem().string();
		JsonRead(json, "Name", config.Name);
		JsonRead(json, "StartScene", config.StartScene);
		JsonRead(json, "AssetDirectory", config.AssetDirectory);
		if (auto it = json.find("Runtime"); it != json.end())
		{
			JsonRead(*it, "WindowWidth", config.WindowWidth);
			JsonRead(*it, "WindowHeight", config.WindowHeight);
			JsonRead(*it, "Fullscreen", config.Fullscreen);
			JsonRead(*it, "VSync", config.VSync);
		}
		if (auto it = json.find("Audio"); it != json.end())
		{
			for (const std::string& bus : AudioEngine::GetBusNames())
				JsonRead(*it, bus.c_str(), config.BusVolumes[bus]);
		}
		if (auto input = json.find("Input"); input != json.end())
		{
			if (auto actions = input->find("Actions"); actions != input->end() && actions->is_object())
			{
				for (const auto& [action, bindings] : actions->items())
				{
					std::vector<InputBinding>& list = config.InputActions[action];
					if (!bindings.is_array())
						continue;
					for (const Json& binding : bindings)
					{
						std::optional<InputBinding> parsed = binding.is_string() ? InputBinding::Parse(binding.get<std::string>()) : std::nullopt;
						if (parsed)
							list.push_back(*parsed);
						else
							BW_CORE_WARN("Project '{}': input action '{}' has an unknown binding {}", config.Name, action, binding.dump());
					}
				}
			}
		}
		if (auto it = json.find("Import"); it != json.end())
		{
			std::string front = ModelFrontToString(config.ModelFrontFaces);
			JsonRead(*it, "ModelFrontFaces", front);
			if (const std::optional<ModelFront> parsed = ModelFrontFromString(front))
				config.ModelFrontFaces = *parsed;
			else
				BW_CORE_WARN("Project '{}': ModelFrontFaces is \"{}\"; it must be \"-Z\" or \"+Z\"", config.Name, front);
		}
		// The game UI navigates with these, so every project has them.
		InputMap::AddMissingUIActions(config.InputActions);

		SetActive(project);
		BW_CORE_INFO("Opened project '{}' ({})", config.Name, project->m_ProjectDirectory.string());
		return project;
	}

	void Project::ApplyAudioSettings() const
	{
		for (const std::string& bus : AudioEngine::GetBusNames())
		{
			auto it = m_Config.BusVolumes.find(bus);
			AudioEngine::SetBusVolume(bus, it != m_Config.BusVolumes.end() ? it->second : 1.0f);
			AudioEngine::SetBusMuted(bus, false);
		}
	}

	void Project::ApplyInputSettings() const
	{
		InputMap::SetActions(m_Config.InputActions);
	}

	void Project::ApplyImportSettings() const
	{
		AssetManager::SetDefaultModelFront(m_Config.ModelFrontFaces);
	}

	bool Project::Save()
	{
		Json json;
		json["Name"] = m_Config.Name;
		json["Engine"] = "MageCap Bellows " BW_VERSION;
		json["StartScene"] = m_Config.StartScene;
		json["AssetDirectory"] = m_Config.AssetDirectory;
		Json& runtime = json["Runtime"];
		runtime["WindowWidth"] = m_Config.WindowWidth;
		runtime["WindowHeight"] = m_Config.WindowHeight;
		runtime["Fullscreen"] = m_Config.Fullscreen;
		runtime["VSync"] = m_Config.VSync;
		Json& audio = json["Audio"];
		for (const auto& [bus, volume] : m_Config.BusVolumes)
			audio[bus] = volume;
		json["Import"]["ModelFrontFaces"] = ModelFrontToString(m_Config.ModelFrontFaces);
		Json& actions = json["Input"]["Actions"];
		actions = Json::object();
		for (const auto& [action, bindings] : m_Config.InputActions)
		{
			Json& list = actions[action];
			list = Json::array();
			for (const InputBinding& binding : bindings)
				list.push_back(binding.ToString());
		}

		if (!FileSystem::WriteTextFile(GetProjectFilePath(), json.dump(1, '\t')))
		{
			BW_CORE_ERROR("Failed to save project '{}'", GetProjectFilePath().string());
			return false;
		}
		return true;
	}

}
