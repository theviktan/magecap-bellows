#include "EditorLayer.h"

#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/DebuggerPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ScriptEditorPanel.h"
#include "Panels/SettingsPanel.h"
#include "Panels/StatisticsPanel.h"
#include "ProjectManager.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Core/BuildInfo.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>

namespace Bellows {

	EditorLayer* EditorLayer::s_Instance = nullptr;

	namespace {

		glm::vec4 Rgba(ImU32 color)
		{
			const ImVec4 v = ImGui::ColorConvertU32ToFloat4(color);
			return glm::vec4(v.x, v.y, v.z, v.w);
		}

		// Asset path for an absolute or project-relative input.
		std::string ToAssetPath(const std::string& path)
		{
			const std::filesystem::path candidate(reinterpret_cast<const char8_t*>(path.c_str()));
			if (candidate.is_absolute())
			{
				const std::string relative = AssetManager::GetAssetPath(candidate);
				return relative.empty() ? path : relative;
			}
			return path;
		}

	}

	EditorLayer::EditorLayer(EditorOptions options)
		: Layer("EditorLayer"), m_Options(std::move(options))
	{
		s_Instance = this;
	}

	EditorLayer::~EditorLayer()
	{
		if (s_Instance == this)
			s_Instance = nullptr;
	}

	void EditorLayer::OnAttach()
	{
		m_Settings = EditorSettings::Load();
		if (!EditorSettings::WriteInstallInfo(Platform::GetExecutableDirectory()))
			BW_CORE_WARN("Could not write Install.json; other tools will not find this editor automatically");
		if (m_Options.AgentServerPort != 0)
			m_Settings.AgentServerPort = m_Options.AgentServerPort;

		Application& application = Application::Get();
		float scale = m_Settings.UIScale;
		if (scale <= 0.0f)
			scale = application.GetWindow() ? application.GetWindow()->GetContentScale() : 1.0f;
		Theme::Apply(Platform::GetExecutableDirectory() / "Resources", scale);

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigWindowsMoveFromTitleBarOnly = true;
		io.ConfigDragClickToInputText = true;
		m_LayoutPath = EditorSettings::GetConfigDirectory() / "EditorLayout.ini";
		if (!m_Options.Headless && FileSystem::Exists(m_LayoutPath))
		{
			ImGui::LoadIniSettingsFromDisk(m_LayoutPath.string().c_str());
			m_LayoutInitialized = true;
		}

		m_ViewportRenderer = CreateScope<SceneRenderer>();
		m_Camera.SetFlySpeed(m_Settings.CameraSpeed);

		m_HierarchyPanel = CreateScope<SceneHierarchyPanel>(*this);
		m_InspectorPanel = CreateScope<InspectorPanel>(*this);
		m_ContentBrowserPanel = CreateScope<ContentBrowserPanel>(*this);
		m_ConsolePanel = CreateScope<ConsolePanel>(*this);
		m_SettingsPanel = CreateScope<SettingsPanel>(*this);
		m_StatisticsPanel = CreateScope<StatisticsPanel>(*this);
		m_ScriptEditorPanel = CreateScope<ScriptEditorPanel>(*this);
		m_DebuggerPanel = CreateScope<DebuggerPanel>(*this);
		// Linear: the UI draws texture values as they are, so decoding sRGB here would darken the icons.
		const std::filesystem::path iconFolder = Platform::GetExecutableDirectory() / "Resources" / "Icons";
		if (FileSystem::Exists(iconFolder / "Bellows.png"))
			m_Logo = Texture2D::LoadFromFile(iconFolder / "Bellows.png", TextureColorSpace::Linear);
		if (FileSystem::Exists(iconFolder / "ForgeScript.png"))
			m_ForgeScriptIcon = Texture2D::LoadFromFile(iconFolder / "ForgeScript.png", TextureColorSpace::Linear);
		m_ProjectManager = CreateScope<ProjectManager>(*this);

		// A script stops inside the game's update; the game pauses once that update has finished.
		m_ScriptDebugger.SetStopCallback([this](const ForgeScript::DebugStop&) { m_DebugStopPending = true; });
		m_ScriptDebugger.Connect(ScriptEngine::GetForgeScriptRuntime());

		Scripting::SetStartedFromEditor(true);
		ScriptEngine::SetQuitHandler([this](int exitCode)
		{
			// Games quitting during play mode just stop playing; automation can quit the editor.
			if (m_SceneState != SceneState::Edit)
				Application::Get().SubmitToMainThread([this] { Stop(); });
			else
				RequestExit(exitCode);
		});
		ScriptEngine::SetScreenshotHandler([this] { return CaptureGameView(); });
		ScriptEngine::SetLoadSceneHandler([this](const std::string& path)
		{
			if (!AssetManager::Exists(path))
				return false;
			if (m_SceneState != SceneState::Edit)
				m_PendingSceneLoad = path;
			else
				Application::Get().SubmitToMainThread([this, path] { OpenScene(path); });
			return true;
		});

		if (!m_Options.ProjectPath.empty())
			OpenProject(m_Options.ProjectPath);

		if (!m_Options.DisableAgentServer && m_Settings.AgentServerEnabled)
			StartAgentServer();

		m_StartupScriptPending = !m_Options.StartupScript.empty();
		UpdateWindowTitle();
	}

	void EditorLayer::OnDetach()
	{
		if (m_SceneState != SceneState::Edit)
			Stop();
		StopGameWindow();
		m_ScriptEditorPanel->SaveAll();
		m_AgentServer.Stop();
		SaveLayout();
		m_Settings.CameraSpeed = m_Camera.GetFlySpeed();
		m_Settings.Save();

		ScriptEngine::SetQuitHandler(nullptr);
		ScriptEngine::SetLoadSceneHandler(nullptr);
		ScriptEngine::SetScreenshotHandler(nullptr);
		Scripting::SetStartedFromEditor(false);
		ScriptEngine::SetContextScene(nullptr);
		m_ScriptDebugger.Connect(nullptr);
		m_ScriptDebugger.SetStopCallback(nullptr);

		m_HierarchyPanel.reset();
		m_InspectorPanel.reset();
		m_ContentBrowserPanel.reset();
		m_ConsolePanel.reset();
		m_SettingsPanel.reset();
		m_StatisticsPanel.reset();
		m_DebuggerPanel.reset();
		m_Logo.reset();
		m_ForgeScriptIcon.reset();
		m_RuntimeScene.reset();
		m_EditorScene.reset();
		m_ScreenshotRenderer.reset();
		m_ViewportRenderer.reset();
		Project::SetActive(nullptr);
	}

	void EditorLayer::SaveLayout()
	{
		if (m_Options.Headless || !ImGui::GetCurrentContext())
			return;
		FileSystem::CreateDirectories(m_LayoutPath.parent_path());
		ImGui::SaveIniSettingsToDisk(m_LayoutPath.string().c_str());
	}

	// -------------------------------------------------------------------------
	// Project and scene management
	// -------------------------------------------------------------------------

	bool EditorLayer::NewProject(const std::filesystem::path& directory, const std::string& name, bool emptyScene)
	{
		if (m_SceneState != SceneState::Edit)
			Stop();
		Ref<Project> project = Project::Create(directory, name);
		if (!project)
			return false;
		if (emptyScene)
		{
			// The "Empty" template: replace the starter scene with one that has nothing in it.
			const std::filesystem::path startScene = project->GetAssetDirectory() / project->GetConfig().StartScene;
			Scene scene(std::filesystem::path(project->GetConfig().StartScene).stem().string());
			if (!SceneSerializer::Serialize(scene, startScene))
				BW_WARN("Could not write an empty start scene; the project keeps the starter scene");
		}
		return OpenProject(project->GetProjectFilePath());
	}

	void EditorLayer::CloseProject()
	{
		if (!m_Project)
			return;
		if (m_SceneState != SceneState::Edit)
			Stop();
		StopGameWindow();
		m_ScriptEditorPanel->SaveAll();
		m_ScriptEditorPanel->CloseAll();
		// Breakpoints name the project's scripts.
		m_ScriptDebugger.ClearBreakpoints();
		SaveLayout();

		m_Selection.clear();
		m_UndoHistory.Clear();
		ScriptEngine::SetContextScene(nullptr);
		m_RuntimeScene.reset();
		m_EditorScene.reset();
		m_PendingSceneLoad.reset();
		m_ScenePath.clear();
		m_CurrentSnapshot.clear();
		m_SavedSnapshot.clear();
		m_Project.reset();
		Project::SetActive(nullptr);
		InputMap::SetActions({});
		m_ProjectManager->Invalidate();
		UpdateWindowTitle();
		BW_INFO("Closed the project");
	}

	bool EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (m_SceneState != SceneState::Edit)
			Stop();
		m_ScriptEditorPanel->SaveAll();

		Ref<Project> project = Project::Load(path);
		if (!project)
		{
			m_DialogMessage = "Could not open the project at '" + path.string() + "'.";
			return false;
		}
		m_Project = project;
		m_Settings.AddRecentProject(project->GetProjectFilePath().string(), EditorSettings::Now());
		m_ProjectManager->Invalidate();
		m_Settings.Save();
		FileSystem::CreateDirectories(project->GetEditorDataDirectory());

		m_ContentBrowserPanel->SetCurrentDirectory(project->GetAssetDirectory());
		m_ScriptEditorPanel->CloseAll();
		project->ApplyAudioSettings();
		project->ApplyInputSettings();
		project->ApplyImportSettings();

		const std::string startScene = project->GetConfig().StartScene;
		if (!startScene.empty() && AssetManager::Exists(startScene))
			OpenScene(startScene);
		else
			NewScene("Main");
		return true;
	}

	void EditorLayer::NewScene(const std::string& name)
	{
		if (m_SceneState != SceneState::Edit)
			Stop();

		m_EditorScene = CreateRef<Scene>(name);
		Entity camera = m_EditorScene->CreateEntity("Main Camera");
		camera.AddComponent<CameraComponent>();
		camera.GetTransform().Translation = { 0.0f, 2.5f, 7.0f };
		camera.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-15.0f, 0.0f, 0.0f)));
		Entity sun = m_EditorScene->CreateEntity("Sun");
		sun.AddComponent<DirectionalLightComponent>();
		sun.GetTransform().SetRotationEuler(glm::radians(glm::vec3(-50.0f, 70.0f, 0.0f)));
		m_EditorScene->CreateEntity("Sky").AddComponent<SkyLightComponent>();

		m_ScenePath.clear();
		ResetEditorStateForScene();
		m_SavedSnapshot.clear();	// Unsaved until written to disk
	}

	bool EditorLayer::OpenScene(const std::string& path)
	{
		if (m_SceneState != SceneState::Edit)
			Stop();

		const std::string assetPath = ToAssetPath(path);
		Ref<Scene> scene = CreateRef<Scene>();
		if (!SceneSerializer::Deserialize(*scene, AssetManager::GetAbsolutePath(assetPath)))
		{
			m_DialogMessage = "Could not open the scene '" + assetPath + "'.";
			return false;
		}
		m_EditorScene = scene;
		m_ScenePath = assetPath;
		ResetEditorStateForScene();
		BW_INFO("Opened scene '{}'", assetPath);
		return true;
	}

	void EditorLayer::ResetEditorStateForScene()
	{
		m_Selection.clear();
		m_UndoHistory.Clear();
		m_CurrentSnapshot = SerializeEditorScene();
		m_SavedSnapshot = m_CurrentSnapshot;
		ScriptEngine::SetContextScene(m_EditorScene.get());

		// Frame the scene's primary camera view, so the first look matches the game.
		RenderCamera gameCamera;
		if (m_EditorScene->GetPrimaryCamera(16.0f / 9.0f, gameCamera))
		{
			const glm::mat4 world = glm::inverse(gameCamera.View);
			const glm::vec3 position = glm::vec3(world[3]);
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
			m_Camera.SetPose(position + forward * -2.0f + glm::vec3(0.0f, 1.0f, 0.0f), position + forward * 8.0f);
		}
		UpdateWindowTitle();
	}

	bool EditorLayer::SaveScene()
	{
		if (!m_EditorScene || !m_Project)
			return false;
		if (m_ScenePath.empty())
		{
			m_DialogPath = "Scenes/" + m_EditorScene->GetName() + ".bscene";
			m_ShowSaveSceneAsDialog = true;
			return false;
		}
		return SaveSceneAs(m_ScenePath);
	}

	bool EditorLayer::SaveSceneAs(const std::string& path)
	{
		if (!m_EditorScene || !m_Project)
			return false;

		std::string assetPath = ToAssetPath(path);
		if (std::filesystem::path(assetPath).extension() != ".bscene")
			assetPath += ".bscene";

		const std::string name = std::filesystem::path(assetPath).stem().string();
		if (m_EditorScene->GetName() == "Untitled" || m_ScenePath.empty())
			m_EditorScene->SetName(name);

		if (!SceneSerializer::Serialize(*m_EditorScene, AssetManager::GetAbsolutePath(assetPath)))
		{
			m_DialogMessage = "Could not save the scene to '" + assetPath + "'.";
			return false;
		}
		m_ScenePath = assetPath;
		m_CurrentSnapshot = SerializeEditorScene();
		m_SavedSnapshot = m_CurrentSnapshot;
		UpdateWindowTitle();
		BW_INFO("Saved scene '{}'", assetPath);
		return true;
	}

	bool EditorLayer::IsSceneDirty() const
	{
		return m_CurrentSnapshot != m_SavedSnapshot;
	}

	std::string EditorLayer::SerializeEditorScene() const
	{
		return m_EditorScene ? SceneSerializer::SerializeToJson(*m_EditorScene).dump() : std::string();
	}

	void EditorLayer::UpdateWindowTitle()
	{
		Window* window = Application::Get().GetWindow();
		if (!window)
			return;
		std::string title = "MageCap Bellows";
		if (m_Project)
		{
			title = m_Project->GetConfig().Name + "  -  ";
			title += m_ScenePath.empty() ? std::string("Untitled scene") : m_ScenePath;
			if (IsSceneDirty())
				title += " *";
			title += "  -  MageCap Bellows";
		}
		if (window->GetTitle() != title)
			window->SetTitle(title);
	}

	// -------------------------------------------------------------------------
	// Undo
	// -------------------------------------------------------------------------

	void EditorLayer::CommitChange(const std::string& description)
	{
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		std::string current = SerializeEditorScene();
		if (current == m_CurrentSnapshot)
			return;
		m_UndoHistory.Push({ std::move(m_CurrentSnapshot), m_Selection, description });
		m_CurrentSnapshot = std::move(current);
		UpdateWindowTitle();
	}

	void EditorLayer::LoadSnapshot(const UndoHistory::Snapshot& snapshot)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		const Json json = Json::parse(snapshot.SceneJson, nullptr, false);
		if (json.is_discarded() || !SceneSerializer::DeserializeFromJson(*scene, json))
		{
			BW_CORE_ERROR("Undo: the stored scene state could not be restored");
			return;
		}
		m_EditorScene = scene;
		m_CurrentSnapshot = snapshot.SceneJson;
		m_Selection.clear();
		for (UUID uuid : snapshot.Selection)
		{
			if (m_EditorScene->TryGetEntityByUUID(uuid))
				m_Selection.push_back(uuid);
		}
		ScriptEngine::SetContextScene(m_EditorScene.get());
		UpdateWindowTitle();
	}

	void EditorLayer::Undo()
	{
		if (m_SceneState != SceneState::Edit)
			return;
		UndoHistory::Snapshot current { m_CurrentSnapshot, m_Selection, {} };
		if (m_UndoHistory.Undo(current))
			LoadSnapshot(current);
	}

	void EditorLayer::Redo()
	{
		if (m_SceneState != SceneState::Edit)
			return;
		UndoHistory::Snapshot current { m_CurrentSnapshot, m_Selection, {} };
		if (m_UndoHistory.Redo(current))
			LoadSnapshot(current);
	}

	// -------------------------------------------------------------------------
	// Play mode
	// -------------------------------------------------------------------------

	void EditorLayer::Play()
	{
		if (!m_EditorScene || m_SceneState != SceneState::Edit)
			return;
		CommitChange("Edit");
		// Scripts load from disk, so the game runs what is in the Script Editor.
		m_ScriptEditorPanel->SaveAll();
		// Every play starts from the project's mixer, whatever the last session's scripts changed.
		m_Project->ApplyAudioSettings();
		m_Project->ApplyInputSettings();
		m_Project->ApplyImportSettings();

		m_RuntimeScene = Scene::Copy(m_EditorScene);
		m_RuntimeScene->OnViewportResize(static_cast<uint32_t>(m_ViewportSize.x), static_cast<uint32_t>(m_ViewportSize.y));
		m_SceneState = SceneState::Play;
		Input::ResetState();
		ScriptEngine::ResetErrorCount();
		m_RuntimeScene->OnRuntimeStart();
		// Saving a script during play swaps in its new code (hot reload).
		m_ScriptWatcher.Start(m_Project->GetAssetDirectory(), { ".py", ".forge" });
		m_ScriptWatchElapsed = 0.0f;
		BW_INFO("Play mode started");
	}

	void EditorLayer::PlayInPreferredMode()
	{
		if (m_Settings.PlayInGameWindow)
			PlayInGameWindow(false);
		else
			Play();
	}

	bool EditorLayer::PlayInGameWindow(bool startScene, bool headless, uint64_t frames)
	{
		if (!m_Project || !m_EditorScene)
			return false;
		// One game at a time.
		if (m_SceneState != SceneState::Edit)
			Stop();
		// The game loads scripts from disk, so it runs what is in the Script Editor.
		m_ScriptEditorPanel->SaveAll();

		const ProjectConfig& config = m_Project->GetConfig();
		GameWindowSession::Options options;
		options.RuntimeProgram = Platform::GetExecutableDirectory() / ("BellowsRuntime" + Platform::GetExecutableExtension());
		options.ProjectFile = m_Project->GetProjectFilePath();
		options.Title = config.Name + " (DEBUG)";
		options.Headless = headless;
		options.Frames = frames;
		// The open scene plays as it is in the editor, unsaved changes included.
		if (!startScene || m_ScenePath == config.StartScene)
		{
			const std::filesystem::path sceneFile = m_Project->GetEditorDataDirectory() / "PlayScene.bscene";
			std::error_code error;
			std::filesystem::create_directories(sceneFile.parent_path(), error);
			if (!SceneSerializer::Serialize(*m_EditorScene, sceneFile))
			{
				BW_ERROR("Could not save the scene for the game window to '{}'", sceneFile.string());
				return false;
			}
			options.SceneFile = sceneFile;
		}
		if (m_ScriptDebugger.GetBreakpointCount() > 0)
			BW_WARN("Breakpoints stop the game only when it plays in the editor viewport (Game > Play in Editor Viewport)");
		return m_GameWindow.Start(options);
	}

	void EditorLayer::StopGameWindow()
	{
		m_GameWindow.Stop();
	}

	void EditorLayer::Stop()
	{
		if (m_SceneState == SceneState::Edit)
			return;
		m_ScriptWatcher.Stop();
		// The runtime ends a stop when play stops (the held code does not go on).
		m_DebugStopPending = false;
		m_PausedByDebugger = false;
		if (m_RuntimeScene)
		{
			// Like 'when destroyed', 'game quitting' runs to the end: code held at a breakpoint does not go
			// on, and the handlers do not stop at breakpoints.
			m_ScriptDebugger.EndSession();
			Scripting::EmitGameQuitting();
			m_RuntimeScene->OnRuntimeStop();
		}
		m_RuntimeScene.reset();
		m_SceneState = SceneState::Edit;
		m_PendingSceneLoad.reset();
		Input::ResetState();
		ScriptEngine::SetContextScene(m_EditorScene.get());

		std::erase_if(m_Selection, [this](UUID uuid) { return !m_EditorScene->TryGetEntityByUUID(uuid); });
		BW_INFO("Play mode stopped");
	}

	void EditorLayer::SetPaused(bool paused)
	{
		if (m_SceneState == SceneState::Edit || !m_RuntimeScene)
			return;
		if (!paused && m_ScriptDebugger.IsStopped())
		{
			// A held script would keep every ForgeScript still; resuming the game resumes it.
			m_PausedByDebugger = true;
			ResumeScripts(DebugAction::Continue);
			return;
		}
		m_RuntimeScene->SetPaused(paused);
		m_SceneState = paused ? SceneState::Pause : SceneState::Play;
		if (!paused)
			m_PausedByDebugger = false;
	}

	void EditorLayer::Step(uint32_t frames)
	{
		// While a script is stopped no ForgeScript runs, so stepping frames would only move physics.
		if (m_SceneState == SceneState::Pause && m_RuntimeScene && !m_ScriptDebugger.IsStopped())
			m_RuntimeScene->Step(frames);
	}

	void EditorLayer::DrawLogo(float size) const
	{
		if (m_Logo)
		{
			ImGui::Image(ImTextureRef(reinterpret_cast<ImTextureID>(m_Logo->GetHandle())), ImVec2(size, size));
			return;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Accent));
		ImGui::TextUnformatted(ICON_FLAME);
		ImGui::PopStyleColor();
	}

	// -------------------------------------------------------------------------
	// ForgeScript debugger
	// -------------------------------------------------------------------------

	bool EditorLayer::ResumeScripts(DebugAction action)
	{
		bool resumed = false;
		switch (action)
		{
			case DebugAction::Continue: resumed = m_ScriptDebugger.Continue(); break;
			case DebugAction::StepOver: resumed = m_ScriptDebugger.StepOver(); break;
			case DebugAction::StepIn:   resumed = m_ScriptDebugger.StepIn(); break;
			case DebugAction::StepOut:  resumed = m_ScriptDebugger.StepOut(); break;
		}
		if (!resumed)
			return false;
		// The script runs right away and may stop again before Continue or the step returns.
		if (m_DebugStopPending)
			HandleDebugStop();
		else
			ResumeAfterDebugStop();
		return true;
	}

	bool EditorLayer::PauseScripts()
	{
		if (m_SceneState == SceneState::Edit)
			return false;
		return m_ScriptDebugger.Pause();
	}

	void EditorLayer::HandleDebugStop()
	{
		m_DebugStopPending = false;
		if (!m_ScriptDebugger.IsStopped() || !m_RuntimeScene)
			return;
		if (m_SceneState == SceneState::Play)
		{
			m_RuntimeScene->SetPaused(true);
			m_SceneState = SceneState::Pause;
			m_PausedByDebugger = true;
		}
		const ForgeScript::DebugStop& stop = m_ScriptDebugger.GetStop();
		BW_INFO("Stopped at {} line {}", stop.File, stop.Line);
		if (!stop.File.empty() && AssetManager::Exists(stop.File))
			m_ScriptEditorPanel->OpenScript(stop.File, static_cast<size_t>(stop.Line));
		m_Settings.Panels.Debugger = true;
		m_DebuggerPanel->Focus();
	}

	void EditorLayer::ResumeAfterDebugStop()
	{
		if (m_ScriptDebugger.IsStopped() || !m_PausedByDebugger)
			return;
		m_PausedByDebugger = false;
		if (m_SceneState == SceneState::Pause && m_RuntimeScene)
		{
			m_RuntimeScene->SetPaused(false);
			m_SceneState = SceneState::Play;
		}
	}

	// -------------------------------------------------------------------------
	// Selection and commands
	// -------------------------------------------------------------------------

	void EditorLayer::Select(Entity entity, bool additive)
	{
		if (!additive)
			m_Selection.clear();
		if (!entity)
			return;
		const UUID uuid = entity.GetUUID();
		auto it = std::find(m_Selection.begin(), m_Selection.end(), uuid);
		if (it != m_Selection.end())
		{
			if (additive)
				m_Selection.erase(it);
			return;
		}
		m_Selection.push_back(uuid);
	}

	void EditorLayer::SetSelection(const std::vector<UUID>& selection)
	{
		m_Selection.clear();
		for (UUID uuid : selection)
		{
			if (GetActiveScene() && GetActiveScene()->TryGetEntityByUUID(uuid))
				m_Selection.push_back(uuid);
		}
	}

	void EditorLayer::ClearSelection()
	{
		m_Selection.clear();
	}

	Entity EditorLayer::GetPrimarySelection() const
	{
		if (m_Selection.empty() || !GetActiveScene())
			return {};
		return GetActiveScene()->TryGetEntityByUUID(m_Selection.back());
	}

	bool EditorLayer::IsSelected(Entity entity) const
	{
		return entity && std::find(m_Selection.begin(), m_Selection.end(), entity.GetUUID()) != m_Selection.end();
	}

	Entity EditorLayer::CreateEntity(const std::string& name, Entity parent)
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene)
			return {};
		Entity entity = scene->CreateChildEntity(parent, name);
		Select(entity);
		CommitChange("Create " + name);
		return entity;
	}

	Entity EditorLayer::CreatePrimitive(const std::string& meshPath, const std::string& name)
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene)
			return {};
		Entity entity = scene->CreateEntity(name);
		entity.AddComponent<MeshRendererComponent>().MeshPath = meshPath;
		// Place it where the camera looks, on the ground.
		glm::vec3 position = m_Camera.GetFocusPoint();
		position.y = std::max(position.y, 0.5f);
		entity.GetTransform().Translation = position;
		Select(entity);
		CommitChange("Create " + name);
		return entity;
	}

	bool EditorLayer::SetModelFront(const std::string& meshPath, ModelFront front)
	{
		if (AssetManager::IsBuiltinPath(meshPath) || AssetManager::GetAssetType(meshPath) != AssetType::Mesh || !AssetManager::Exists(meshPath))
			return false;
		MeshImportSettings settings = AssetManager::GetMeshImportSettings(meshPath);
		settings.Front = front;
		if (!settings.Save(AssetManager::GetAbsolutePath(meshPath)))
		{
			BW_ERROR("Could not save the import settings of '{}'", meshPath);
			return false;
		}

		AssetManager::Invalidate(meshPath);
		ReloadPlacedMeshes(meshPath);
		return true;
	}

	void EditorLayer::ReloadPlacedMeshes(const std::string& meshPath)
	{
		// Placed copies hold the mesh they loaded; clearing its path makes them load it again.
		for (const Ref<Scene>& scene : { m_EditorScene, m_RuntimeScene })
		{
			if (!scene)
				continue;
			for (entt::entity handle : scene->GetAllEntitiesWith<MeshRendererComponent>())
			{
				MeshRendererComponent& renderer = Entity(handle, scene.get()).GetComponent<MeshRendererComponent>();
				if (meshPath.empty() || renderer.MeshPath == meshPath)
					renderer.LoadedMeshPath.clear();
			}
		}
	}

	Entity EditorLayer::CreateMeshEntity(const std::string& meshPath, const glm::vec3& position)
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene)
			return {};
		Entity entity = scene->CreateEntity(std::filesystem::path(meshPath).stem().string());
		entity.AddComponent<MeshRendererComponent>().MeshPath = meshPath;
		entity.GetTransform().Translation = position;
		Select(entity);
		CommitChange("Add " + meshPath);
		return entity;
	}

	Entity EditorLayer::InstantiatePrefab(const std::string& prefabPath, const glm::vec3& position)
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene)
			return {};
		Entity entity = SceneSerializer::InstantiatePrefab(*scene, prefabPath);
		if (!entity)
			return {};
		entity.GetTransform().Translation = position;
		Select(entity);
		CommitChange("Instantiate " + prefabPath);
		return entity;
	}

	void EditorLayer::DeleteSelection()
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene || m_Selection.empty())
			return;
		for (UUID uuid : m_Selection)
		{
			if (Entity entity = scene->TryGetEntityByUUID(uuid))
				scene->DestroyEntity(entity);
		}
		m_Selection.clear();
		CommitChange("Delete");
	}

	void EditorLayer::DuplicateSelection()
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene || m_Selection.empty())
			return;
		std::vector<UUID> copies;
		for (UUID uuid : m_Selection)
		{
			Entity entity = scene->TryGetEntityByUUID(uuid);
			// Skip entities whose ancestor is also selected (it is copied with the ancestor).
			bool ancestorSelected = false;
			for (Entity parent = entity ? entity.GetParent() : Entity(); parent; parent = parent.GetParent())
				ancestorSelected |= IsSelected(parent);
			if (entity && !ancestorSelected)
				copies.push_back(scene->DuplicateEntity(entity).GetUUID());
		}
		m_Selection = copies;
		CommitChange("Duplicate");
	}

	void EditorLayer::FocusSelection()
	{
		Entity entity = GetPrimarySelection();
		if (!entity)
			return;
		const Ref<Scene>& scene = GetActiveScene();
		const glm::mat4 world = scene->GetWorldTransform(entity);
		AABB bounds;
		if (auto* meshRenderer = entity.TryGetComponent<MeshRendererComponent>(); meshRenderer && meshRenderer->RuntimeMesh)
			bounds = meshRenderer->RuntimeMesh->GetBoundingBox().Transformed(world);
		if (!bounds.IsValid())
			bounds = AABB(glm::vec3(world[3]) - 0.5f, glm::vec3(world[3]) + 0.5f);
		m_Camera.Focus(bounds.GetCenter(), glm::length(bounds.GetExtents()));
	}

	bool EditorLayer::AttachScript(Entity entity, const std::string& scriptPath)
	{
		if (!entity || m_SceneState != SceneState::Edit)
			return false;
		if (AssetManager::GetAssetType(scriptPath) != AssetType::Script)
		{
			BW_ERROR("'{}' is not a script (.forge or .py)", scriptPath);
			return false;
		}
		ScriptComponent& script = entity.AddOrReplaceComponent<ScriptComponent>();
		script.ScriptPath = scriptPath;
		script.ClassName.clear();
		script.Fields.clear();
		CommitChange("Attach Script");
		return true;
	}

	void EditorLayer::DetachScript(Entity entity)
	{
		if (!entity || m_SceneState != SceneState::Edit || !entity.HasComponent<ScriptComponent>())
			return;
		entity.RemoveComponent<ScriptComponent>();
		CommitChange("Detach Script");
	}

	bool EditorLayer::SavePrefab(Entity entity, const std::string& assetPath)
	{
		if (!entity)
			return false;
		std::string path = ToAssetPath(assetPath);
		if (std::filesystem::path(path).extension() != ".bprefab")
			path += ".bprefab";
		const bool saved = SceneSerializer::SavePrefab(entity, AssetManager::GetAbsolutePath(path));
		if (saved)
			BW_INFO("Saved prefab '{}'", path);
		return saved;
	}

	// -------------------------------------------------------------------------
	// Frame update
	// -------------------------------------------------------------------------

	bool EditorLayer::IsViewportInputActive() const
	{
		return m_SceneState != SceneState::Edit && (m_ViewportFocused || m_Options.Headless);
	}

	void EditorLayer::OnUpdate(Timestep timestep)
	{
		m_LastFrameTime = timestep.GetSeconds();
		// The game window's log lines reach the Console every frame.
		m_GameWindow.Update();

		// Agent requests run at the next frame boundary, once per frame, and never nested
		// inside one another (a request calling run_frames must not run other requests meanwhile).
		if (m_AgentServer.IsRunning() && !m_AgentRequestsQueued && !m_ProcessingAgentRequests)
		{
			m_AgentRequestsQueued = true;
			RunAtFrameBoundary([this]
			{
				m_AgentRequestsQueued = false;
				m_ProcessingAgentRequests = true;
				m_AgentServer.ProcessRequests();
				m_ProcessingAgentRequests = false;
			});
		}

		if (m_StartupScriptPending && Application::Get().GetFrameCount() >= 2)
		{
			m_StartupScriptPending = false;
			RunAtFrameBoundary([this] { RunStartupScript(); });
		}

		if (m_PendingSceneLoad && m_SceneState != SceneState::Edit)
		{
			// bellows.load_scene() during play loads into the running scene, never the edited one; entities
			// marked Keep Across Scenes stay.
			const std::string path = *m_PendingSceneLoad;
			m_PendingSceneLoad.reset();
			const std::optional<Json> json = SceneSerializer::ReadSceneFile(AssetManager::GetAbsolutePath(path));
			if (json && m_RuntimeScene->ChangeScene(*json, path))
				m_Selection.clear();
			else
				BW_CORE_ERROR("Play: could not load scene '{}'", path);
		}

		Input::SetEnabled(IsViewportInputActive());
		Input::SetViewportRect(m_ViewportPosition, m_ViewportSize);

		if (m_ScriptWatcher.IsWatching())
		{
			m_ScriptWatchElapsed += timestep.GetSeconds();
			if (m_ScriptWatchElapsed >= c_ScriptWatchInterval)
			{
				m_ScriptWatchElapsed = 0.0f;
				ReloadChangedScripts();
			}
		}

		if (m_SceneState == SceneState::Edit)
		{
			if (m_EditorScene)
				m_EditorScene->OnUpdateEditor(timestep);
		}
		else if (m_RuntimeScene)
		{
			m_RuntimeScene->OnUpdateRuntime(timestep);
		}
		if (m_DebugStopPending)
			HandleDebugStop();

		// The UI view looks through the game camera when there is one, so flying the editor camera would do nothing.
		const bool uiViewOwnsCamera = IsInUIView() && m_EditorScene && m_EditorScene->GetPrimaryCameraEntity();
		const bool cameraAllowed = !uiViewOwnsCamera && (m_SceneState == SceneState::Edit || m_FreeCameraInPlay || !IsUsingGameCamera());
		m_CameraControlActive = cameraAllowed && m_Camera.OnUpdate(timestep, m_ViewportHovered && !ImGuizmo::IsUsing(), m_ViewportMouseWheel);
		m_ViewportMouseWheel = 0.0f;

		RenderViewport();
	}

	void EditorLayer::ReloadChangedScripts()
	{
		// Scripts saved while one is stopped reload once it goes on (the watcher keeps the changes).
		if (m_ScriptDebugger.IsStopped())
			return;
		for (const std::string& path : m_ScriptWatcher.Poll())
		{
			if (ScriptEngine::ReloadScripts(path))
				BW_INFO("Reloaded {}", path);
			else
				BW_WARN("{} was not reloaded; the game keeps running the old code", path);
		}
	}

	bool EditorLayer::IsUsingGameCamera() const
	{
		if (m_SceneState == SceneState::Edit || m_FreeCameraInPlay || !m_RuntimeScene)
			return false;
		return static_cast<bool>(m_RuntimeScene->GetPrimaryCameraEntity());
	}

	bool EditorLayer::IsInUIView() const
	{
		return m_Settings.UIView && m_SceneState == SceneState::Edit;
	}

	void EditorLayer::SetViewportSizeOverride(uint32_t width, uint32_t height)
	{
		m_ViewportSizeOverride = glm::uvec2(width, height);
	}

	void EditorLayer::RenderViewport()
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene || !m_ViewportRenderer)
			return;

		const bool uiView = IsInUIView();
		uint32_t width = static_cast<uint32_t>(std::max(m_ViewportSize.x, 1.0f));
		uint32_t height = static_cast<uint32_t>(std::max(m_ViewportSize.y, 1.0f));
		if (m_ViewportSizeOverride.x > 0 && m_ViewportSizeOverride.y > 0)
		{
			width = m_ViewportSizeOverride.x;
			height = m_ViewportSizeOverride.y;
		}
		else if (uiView)
		{
			// The game's screen size, so the UI is laid out as players will see it.
			width = m_Settings.UIViewWidth;
			height = m_Settings.UIViewHeight;
		}
		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		m_ViewportRenderer->SetViewportSize(width, height);
		scene->OnViewportResize(width, height);
		m_Camera.SetViewportSize(static_cast<float>(width), static_cast<float>(height));

		RenderCamera camera = m_Camera.GetRenderCamera();
		if (IsUsingGameCamera() || uiView)
			scene->GetPrimaryCamera(aspect, camera);	// The UI view keeps the editor camera when there is no game camera

		// Through the game camera the viewport shows exactly what the player sees.
		m_ViewportShowsGameView = IsUsingGameCamera();
		SceneRendererSettings& settings = m_ViewportRenderer->GetSettings();
		settings.ShowGrid = m_Settings.ShowGrid && m_SceneState == SceneState::Edit && !uiView;
		settings.ShowSelectionOutline = !m_ViewportShowsGameView && !uiView;
		settings.ShowUI = m_ViewportShowsGameView || uiView || m_Settings.ShowUI;

		std::vector<uint32_t> selectedIDs;
		for (UUID uuid : m_Selection)
		{
			if (Entity entity = scene->TryGetEntityByUUID(uuid))
			{
				selectedIDs.push_back(entity.GetRenderID());
				for (Entity child : scene->GetAllEntities())
				{
					if (child.IsDescendantOf(entity))
						selectedIDs.push_back(child.GetRenderID());
				}
			}
		}
		m_ViewportRenderer->SetSelectedEntities(selectedIDs);

		scene->OnRender(*m_ViewportRenderer, camera, [this](SceneRenderer& renderer) { SubmitEditorOverlays(renderer); });
	}

	void EditorLayer::SubmitEditorOverlays(SceneRenderer& renderer)
	{
		if (IsUsingGameCamera() || IsInUIView())
			return;

		const Ref<Scene>& scene = GetActiveScene();
		const glm::vec4 colliderColor = Rgba(IM_COL32(110, 220, 140, 220));
		const glm::vec4 lightColor = Rgba(IM_COL32(250, 210, 90, 200));
		const glm::vec4 cameraColor = Rgba(IM_COL32(200, 205, 215, 200));

		auto drawColliders = [&](Entity entity)
		{
			glm::vec3 translation(0.0f), scale(1.0f);
			glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
			Math::DecomposeTransform(scene->GetWorldTransform(entity), translation, rotation, scale);
			const glm::mat4 rigid = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);

			if (auto* box = entity.TryGetComponent<BoxColliderComponent>())
				renderer.SubmitWireBox(rigid * glm::translate(glm::mat4(1.0f), box->Offset * scale), box->HalfExtents * scale, colliderColor);
			if (auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
			{
				const glm::vec3 center = glm::vec3(rigid * glm::vec4(sphere->Offset * scale, 1.0f));
				renderer.SubmitWireSphere(center, sphere->Radius * std::max({ scale.x, scale.y, scale.z }), colliderColor);
			}
			if (auto* capsule = entity.TryGetComponent<CapsuleColliderComponent>())
			{
				renderer.SubmitWireCapsule(rigid * glm::translate(glm::mat4(1.0f), capsule->Offset * scale),
					capsule->Radius * std::max(scale.x, scale.z), capsule->HalfHeight * scale.y, colliderColor);
			}
		};

		if (m_ShowColliders)
		{
			for (Entity entity : scene->GetAllEntities())
			{
				if (entity.HasComponent<BoxColliderComponent>() || entity.HasComponent<SphereColliderComponent>() || entity.HasComponent<CapsuleColliderComponent>())
					drawColliders(entity);
			}
		}

		for (UUID uuid : m_Selection)
		{
			Entity entity = scene->TryGetEntityByUUID(uuid);
			if (!entity)
				continue;
			const glm::mat4 world = scene->GetWorldTransform(entity);
			const glm::vec3 position(world[3]);
			const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));

			if (!m_ShowColliders)
				drawColliders(entity);

			if (auto* light = entity.TryGetComponent<PointLightComponent>())
				renderer.SubmitWireSphere(position, light->Range, lightColor);

			if (auto* light = entity.TryGetComponent<SpotLightComponent>())
			{
				const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
				const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
				const float radius = std::tan(light->OuterAngle) * light->Range;
				const glm::vec3 center = position + forward * light->Range;
				constexpr int segments = 32;
				for (int i = 0; i < segments; i++)
				{
					const float a0 = static_cast<float>(i) / segments * glm::two_pi<float>();
					const float a1 = static_cast<float>(i + 1) / segments * glm::two_pi<float>();
					const glm::vec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * radius;
					const glm::vec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * radius;
					renderer.SubmitLine(p0, p1, lightColor);
					if (i % 8 == 0)
						renderer.SubmitLine(position, p0, lightColor);
				}
			}

			if (entity.HasComponent<DirectionalLightComponent>())
			{
				const glm::vec3 right = glm::normalize(glm::vec3(world[0]));
				const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
				for (int i = -1; i <= 1; i++)
				{
					const glm::vec3 start = position + right * (0.3f * static_cast<float>(i)) + up * (0.15f * static_cast<float>(i * i));
					renderer.SubmitLine(start, start + forward * 2.0f, lightColor);
				}
			}

			if (auto* camera = entity.TryGetComponent<CameraComponent>())
			{
				const float aspect = m_ViewportSize.x / std::max(m_ViewportSize.y, 1.0f);
				const glm::mat4 projection = camera->GetProjection(aspect);
				glm::vec3 translation(0.0f), scale(1.0f);
				glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
				Math::DecomposeTransform(world, translation, rotation, scale);
				const glm::mat4 view = glm::inverse(glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation));
				// Draw the frustum out to a short preview distance.
				const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
				std::array<glm::vec3, 8> corners;
				for (int i = 0; i < 8; i++)
				{
					const glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
					glm::vec4 point = inverseViewProjection * ndc;
					corners[i] = glm::vec3(point) / point.w;
				}
				for (int i = 4; i < 8; i++)
					corners[i] = corners[i - 4] + glm::normalize(corners[i] - corners[i - 4]) * std::min(5.0f, camera->FarClip);
				constexpr int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
				for (const auto& edge : edges)
					renderer.SubmitLine(corners[edge[0]], corners[edge[1]], cameraColor);
			}
		}
	}

	Image EditorLayer::CaptureViewport()
	{
		RenderViewport();
		return Renderer::ReadbackTexture(m_ViewportRenderer->GetFinalImage());
	}

	std::optional<Image> EditorLayer::CaptureGameView()
	{
		// The image on screen, when it is the game's.
		if (m_ViewportShowsGameView && IsUsingGameCamera())
			return Renderer::ReadbackTexture(m_ViewportRenderer->GetFinalImage());

		// Otherwise (free camera in play, or edit mode) the game camera is drawn on its own, without overlays.
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene || !m_ViewportRenderer)
			return std::nullopt;
		const uint32_t width = std::max(m_ViewportRenderer->GetViewportWidth(), 1u);
		const uint32_t height = std::max(m_ViewportRenderer->GetViewportHeight(), 1u);
		RenderCamera camera;
		if (!scene->GetPrimaryCamera(static_cast<float>(width) / static_cast<float>(height), camera))
			return std::nullopt;
		if (!m_ScreenshotRenderer)
			m_ScreenshotRenderer = CreateScope<SceneRenderer>();
		m_ScreenshotRenderer->SetViewportSize(width, height);
		m_ScreenshotRenderer->GetSettings().ShowGrid = false;
		m_ScreenshotRenderer->GetSettings().ShowSelectionOutline = false;
		scene->OnRender(*m_ScreenshotRenderer, camera);
		return Renderer::ReadbackTexture(m_ScreenshotRenderer->GetFinalImage());
	}

	void EditorLayer::PickEntity(const glm::vec2& viewportPosition)
	{
		const Ref<Scene>& scene = GetActiveScene();
		if (!scene)
			return;
		const uint32_t width = m_ViewportRenderer->GetViewportWidth();
		const uint32_t height = m_ViewportRenderer->GetViewportHeight();
		const glm::vec2 scaled = (viewportPosition + m_ViewportPosition - m_ViewportImageOrigin) / glm::max(m_ViewportImageScale, glm::vec2(1e-4f));
		if (scaled.x < 0.0f || scaled.y < 0.0f || scaled.x >= static_cast<float>(width) || scaled.y >= static_cast<float>(height))
			return;

		// The UI view picks the UI rect itself (it is what gets moved), in front of the 3D scene.
		if (IsInUIView())
		{
			if (Entity rect = scene->FindUIRectAt(scaled))
			{
				Select(rect, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
				return;
			}
		}

		const uint32_t id = m_ViewportRenderer->ReadEntityID(static_cast<uint32_t>(scaled.x), static_cast<uint32_t>(scaled.y));
		Entity entity = scene->GetEntityByRenderID(id);

		// Clicking a part of a selected prefab/hierarchy selects the root first, then the part.
		if (entity && !ImGui::GetIO().KeyAlt)
		{
			Entity root = entity;
			while (root.GetParent())
				root = root.GetParent();
			if (root != entity && !IsSelected(root) && !IsSelected(entity))
				entity = root;
		}
		Select(entity, ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
	}

	// -------------------------------------------------------------------------
	// Automation
	// -------------------------------------------------------------------------

	void EditorLayer::RunAtFrameBoundary(std::function<void()> function)
	{
		Application::Get().SubmitToMainThread([this, function = std::move(function)]
		{
			const bool previous = m_AtFrameBoundary;
			m_AtFrameBoundary = true;
			try
			{
				function();
			}
			catch (...)
			{
				m_AtFrameBoundary = previous;
				throw;
			}
			m_AtFrameBoundary = previous;
		});
	}

	void EditorLayer::RunFrames(uint32_t frames)
	{
		if (m_RunningFrames)
			throw std::runtime_error("run_frames cannot be nested");
		if (!m_AtFrameBoundary)
			throw std::runtime_error("run_frames can only be called from automation (agent requests, the console or --run-script)");

		// Frames run from here are ordinary frames; code inside them is not at a boundary.
		m_RunningFrames = true;
		m_AtFrameBoundary = false;
		Application& application = Application::Get();
		try
		{
			for (uint32_t i = 0; i < frames && application.IsRunning(); i++)
				application.RunFrame();
		}
		catch (...)
		{
			m_RunningFrames = false;
			m_AtFrameBoundary = true;
			throw;
		}
		m_RunningFrames = false;
		m_AtFrameBoundary = true;
	}

	void EditorLayer::RequestExit(int exitCode)
	{
		Application::Get().Close(exitCode);
	}

	bool EditorLayer::ExecutePython(const std::string& source, std::string& outOutput)
	{
		const bool success = ScriptEngine::ExecuteSource(source, outOutput, "<agent>");
		CommitChange("Script");
		return success;
	}

	void EditorLayer::RunStartupScript()
	{
		std::string output;
		BW_INFO("Running automation script '{}'", m_Options.StartupScript.string());
		const bool success = ScriptEngine::ExecuteFile(m_Options.StartupScript, output);
		CommitChange("Script");
		if (!output.empty())
		{
			std::istringstream stream(output);
			std::string line;
			while (std::getline(stream, line))
			{
				if (success)
					BW_INFO("{}", line);
				else
					BW_ERROR("{}", line);
			}
		}
		if (m_Options.ExitAfterScript)
			RequestExit(success && ScriptEngine::GetErrorCount() == 0 ? 0 : 1);
	}

	void EditorLayer::StartAgentServer()
	{
		m_AgentServer.Start(m_Settings.AgentServerPort, [this](const std::string& method, const Json& params) { return HandleAgentRequest(method, params); });
	}

	Json EditorLayer::HandleAgentRequest(const std::string& method, const Json& params)
	{
		if (method == "ping")
		{
			Json result;
			result["engine"] = "MageCap Bellows";
			result["version"] = BW_VERSION;
			result["commit"] = BuildInfo::GetCommit();
			result["project"] = m_Project ? m_Project->GetConfig().Name : "";
			result["scene"] = m_ScenePath;
			result["state"] = m_SceneState == SceneState::Edit ? "edit" : (m_SceneState == SceneState::Play ? "play" : "pause");
			return result;
		}
		if (method == "exec")
		{
			const std::string code = params.value("code", std::string());
			std::string output;
			const bool success = ExecutePython(code, output);
			Json result;
			result["success"] = success;
			result["output"] = output;
			return result;
		}
		if (method == "logs")
		{
			const uint64_t since = params.value("since", uint64_t(0));
			Json messages = Json::array();
			for (const LogMessage& message : Log::GetMessagesSince(since))
			{
				static const char* levels[] = { "trace", "info", "warning", "error", "critical" };
				Json entry;
				entry["sequence"] = message.Sequence;
				entry["level"] = levels[static_cast<int>(message.Level)];
				entry["source"] = message.Source == LogSource::Core ? "engine" : "game";
				entry["text"] = message.Text;
				messages.push_back(entry);
			}
			return messages;
		}
		throw std::runtime_error("Unknown method '" + method + "'. Available: ping, exec, logs, wait_frames");
	}

	// -------------------------------------------------------------------------
	// Events and shortcuts
	// -------------------------------------------------------------------------

	void EditorLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowFileDropEvent>([this](WindowFileDropEvent& e)
		{
			if (m_ContentBrowserPanel && m_Project)
				m_ContentBrowserPanel->ImportFiles(e.GetPaths());
			return true;
		});
	}

	void EditorLayer::TogglePlay()
	{
		if (m_SceneState != SceneState::Edit)
			Stop();
		else if (m_GameWindow.IsRunning())
			StopGameWindow();
		else
			PlayInPreferredMode();
	}

	void EditorLayer::HandlePlayKeys(bool ctrl)
	{
		// Godot's keys: F5 plays the project, F6 the open scene, F8 stops. While playing in the viewport
		// F5 belongs to the debugger (Continue), above.
		if (ctrl)
			return;
		if (m_SceneState == SceneState::Edit && ImGui::IsKeyPressed(ImGuiKey_F5, false))
			PlayInGameWindow(true);
		if (m_SceneState == SceneState::Edit && ImGui::IsKeyPressed(ImGuiKey_F6, false))
			PlayInGameWindow(false);
		if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
		{
			if (m_SceneState != SceneState::Edit)
				Stop();
			StopGameWindow();
		}
	}

	void EditorLayer::HandleShortcuts()
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (!m_Project)
			return;

		const bool ctrl = io.KeyCtrl;
		const bool shift = io.KeyShift;

		// Debugger keys type nothing, so they work wherever the focus is, the Script Editor included.
		if (m_SceneState != SceneState::Edit && !ctrl)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
				ResumeScripts(DebugAction::Continue);
			if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
				ResumeScripts(DebugAction::StepOver);
			if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
				ResumeScripts(shift ? DebugAction::StepOut : DebugAction::StepIn);
		}
		HandlePlayKeys(ctrl);
		if (io.WantTextInput)
			return;

		// The Script Editor handles its own editing keys (save, undo, ...); only play mode passes through.
		if (m_ScriptEditorPanel->IsFocused())
		{
			if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
				TogglePlay();
			return;
		}

		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
		{
			if (shift)
			{
				m_DialogPath = m_ScenePath.empty() ? "Scenes/NewScene.bscene" : m_ScenePath;
				m_ShowSaveSceneAsDialog = true;
			}
			else
			{
				SaveScene();
			}
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
			NewScene();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			shift ? Redo() : Undo();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			Redo();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
			TogglePlay();

		// Viewport and hierarchy commands need one of those panels to be in focus.
		const bool sceneFocus = m_ViewportHovered || m_ViewportFocused || m_HierarchyPanel->IsFocused();
		if (!sceneFocus || IsViewportInputActive())
			return;

		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
			DuplicateSelection();
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			DeleteSelection();
		if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ctrl)
			FocusSelection();

		if (!m_Camera.IsFlying() && !ctrl && !ImGuizmo::IsUsing())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_GizmoMode = GizmoMode::None;
			if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoMode = GizmoMode::Translate;
			if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoMode = GizmoMode::Rotate;
			if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoMode = GizmoMode::Scale;
		}
	}

	// -------------------------------------------------------------------------
	// UI
	// -------------------------------------------------------------------------

	void EditorLayer::OnImGuiRender()
	{
		if (!m_Project)
		{
			m_ProjectManager->OnImGuiRender();
			DrawDialogs();
			return;
		}

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float toolbarHeight = ImGui::GetFrameHeight() + 10.0f * Theme::GetScale();
		const float statusHeight = ImGui::GetFrameHeight() + 2.0f * Theme::GetScale();

		// Root window: menu bar, toolbar, dock space and status bar.
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::ToVec4(Theme::Background));
		const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		ImGui::Begin("##EditorRoot", nullptr, rootFlags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);

		DrawMenuBar();
		DrawToolbar();

		const ImGuiID dockspaceID = ImGui::GetID("EditorDockspace");
		if (!m_LayoutInitialized || !ImGui::DockBuilderGetNode(dockspaceID))
		{
			BuildDefaultLayout(dockspaceID);
			m_LayoutInitialized = true;
		}
		const ImVec2 dockSize(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - statusHeight);
		ImGui::SetCursorPosX(0.0f);
		ImGui::DockSpace(dockspaceID, dockSize, ImGuiDockNodeFlags_None);

		DrawStatusBar();
		ImGui::End();
		(void)toolbarHeight;

		EditorPanelVisibility& panels = m_Settings.Panels;
		if (panels.Hierarchy)
			m_HierarchyPanel->OnImGuiRender(&panels.Hierarchy);
		if (panels.Inspector)
			m_InspectorPanel->OnImGuiRender(&panels.Inspector);
		if (panels.Content)
			m_ContentBrowserPanel->OnImGuiRender(&panels.Content);
		if (panels.Console)
			m_ConsolePanel->OnImGuiRender(&panels.Console);
		if (panels.Debugger)
			m_DebuggerPanel->OnImGuiRender(&panels.Debugger);
		if (panels.Settings)
			m_SettingsPanel->OnImGuiRender(&panels.Settings);
		if (m_ShowStatistics)
			m_StatisticsPanel->OnImGuiRender(&m_ShowStatistics);
		DrawViewport();
		m_ScriptEditorPanel->OnImGuiRender();
		DrawDialogs();
		if (m_ShowDemoWindow)
			ImGui::ShowDemoWindow(&m_ShowDemoWindow);

		HandleShortcuts();

		// Any finished widget interaction (drag, text edit, checkbox, gizmo) becomes one undo step.
		const bool editing = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();
		if (m_WasEditingWidget && !editing)
			CommitChange("Edit");
		m_WasEditingWidget = editing;

		UpdateWindowTitle();
	}

	void EditorLayer::SetWorkspace(Workspace workspace)
	{
		if (workspace == Workspace::Script)
			m_ScriptEditorPanel->Focus();
		else
			m_FocusViewport = true;
	}

	Workspace EditorLayer::GetWorkspace() const
	{
		return m_ScriptEditorPanel->IsVisible() && !m_ViewportVisible ? Workspace::Script : Workspace::Scene;
	}

	void EditorLayer::BuildDefaultLayout(unsigned int dockspaceID)
	{
		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		const ImVec2 size = ImGui::GetMainViewport()->WorkSize;
		ImGui::DockBuilderSetNodeSize(dockspaceID, size);

		ImGuiID center = dockspaceID;
		ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
		ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
		ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

		ImGui::DockBuilderDockWindow("Hierarchy", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Settings", right);
		ImGui::DockBuilderDockWindow("Content", bottom);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderDockWindow(DebuggerPanel::WindowName, bottom);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderDockWindow(ScriptEditorPanel::WindowName, center);
		ImGui::DockBuilderFinish(dockspaceID);

		SetWorkspace(Workspace::Scene);
		ImGui::SetWindowFocus("Inspector");
		ImGui::SetWindowFocus("Content");
	}

	void EditorLayer::DrawMenuBar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
		if (!ImGui::BeginMenuBar())
		{
			ImGui::PopStyleVar();
			return;
		}

		// Brand mark, centered in the bar
		const ImGuiWindow* barWindow = ImGui::GetCurrentWindow();
		const float barHeight = barWindow->MenuBarHeight;
		const float logoSize = std::round(barHeight * 0.86f);
		ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, barWindow->Pos.y + barWindow->TitleBarHeight + (barHeight - logoSize) * 0.5f));
		DrawLogo(logoSize);
		ImGui::PushFont(Theme::GetFonts().Bold, 0.0f);
		ImGui::TextUnformatted("Bellows");
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(8.0f, 0.0f));

		const bool editing = m_SceneState == SceneState::Edit;
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem(ICON_FOLDER_PLUS "  New Project..."))
				m_ProjectManager->ShowNewProjectDialog();
			if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Open Project..."))
				m_ShowOpenProjectDialog = true;
			const std::vector<std::string> recentProjects = m_Settings.GetRecentProjects(12);
			if (ImGui::BeginMenu(ICON_REFRESH_CW "  Recent Projects", !recentProjects.empty()))
			{
				for (const std::string& recent : recentProjects)
				{
					if (ImGui::MenuItem(recent.c_str()))
					{
						const std::string path = recent;
						Application::Get().SubmitToMainThread([this, path] { OpenProject(path); });
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem(ICON_LAYOUT_GRID "  Project Manager..."))
			{
				// Like Godot's "Quit to Project List": asks about unsaved scene changes first.
				if (IsSceneDirty() && m_SceneState == SceneState::Edit)
					m_ShowCloseProjectDialog = true;
				else
					Application::Get().SubmitToMainThread([this] { CloseProject(); });
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_FILE_PLUS "  New Scene", "Ctrl+N", false, editing))
				NewScene();
			if (ImGui::MenuItem(ICON_SAVE "  Save Scene", "Ctrl+S", false, editing))
				SaveScene();
			if (ImGui::MenuItem(ICON_SAVE "  Save Scene As...", "Ctrl+Shift+S", false, editing))
			{
				m_DialogPath = m_ScenePath.empty() ? "Scenes/NewScene.bscene" : m_ScenePath;
				m_ShowSaveSceneAsDialog = true;
			}
			if (ImGui::MenuItem(ICON_FILE_CODE "  New Script..."))
				m_ScriptEditorPanel->ShowCreateDialog();
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_ROCKET "  Export Game..."))
			{
				if (m_DialogPath.empty() || !m_ShowExportDialog)
					m_DialogPath = (m_Project->GetProjectDirectory() / "Build").string();
				m_LastExport = {};
				m_ShowExportDialog = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_X "  Exit"))
				RequestExit(0);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			const std::string undoLabel = std::string(ICON_UNDO_2 "  Undo ") + m_UndoHistory.GetUndoDescription();
			const std::string redoLabel = std::string(ICON_REDO_2 "  Redo ") + m_UndoHistory.GetRedoDescription();
			if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, editing && m_UndoHistory.CanUndo()))
				Undo();
			if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, editing && m_UndoHistory.CanRedo()))
				Redo();
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_COPY "  Duplicate", "Ctrl+D", false, !m_Selection.empty()))
				DuplicateSelection();
			if (ImGui::MenuItem(ICON_TRASH_2 "  Delete", "Del", false, !m_Selection.empty()))
				DeleteSelection();
			if (ImGui::MenuItem(ICON_FOCUS "  Focus Selection", "F", false, !m_Selection.empty()))
				FocusSelection();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Entity"))
		{
			m_HierarchyPanel->DrawCreateMenu({});
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::BeginMenu(ICON_PANEL_LEFT "  Panels"))
			{
				EditorPanelVisibility& panels = m_Settings.Panels;
				ImGui::MenuItem(ICON_LIST_TREE "  Hierarchy", nullptr, &panels.Hierarchy);
				ImGui::MenuItem(ICON_SLIDERS_HORIZONTAL "  Inspector", nullptr, &panels.Inspector);
				ImGui::MenuItem(ICON_FOLDER "  Content", nullptr, &panels.Content);
				ImGui::MenuItem(ICON_TERMINAL "  Console", nullptr, &panels.Console);
				ImGui::MenuItem(ICON_BUG "  Debugger", nullptr, &panels.Debugger);
				ImGui::MenuItem(ICON_SETTINGS "  Settings", nullptr, &panels.Settings);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			ImGui::MenuItem(ICON_GRID_3X3 "  Grid", nullptr, &m_Settings.ShowGrid);
			ImGui::MenuItem(ICON_MONITOR "  Game UI", nullptr, &m_Settings.ShowUI);
			ImGui::MenuItem(ICON_LAYOUT_TEMPLATE "  UI View", nullptr, &m_Settings.UIView);
			if (ImGui::BeginMenu(ICON_SCAN "  UI View Size"))
			{
				DrawUIViewSizeMenu();
				ImGui::EndMenu();
			}
			ImGui::MenuItem(ICON_BOX "  Collider Outlines", nullptr, &m_ShowColliders);
			ImGui::MenuItem(ICON_ACTIVITY "  Statistics", nullptr, &m_ShowStatistics);
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_LAYOUT_GRID "  Reset Layout"))
			{
				// Back to the default arrangement, with every panel shown.
				m_Settings.Panels = EditorPanelVisibility();
				m_LayoutInitialized = false;
			}
#ifdef BW_DEBUG
			ImGui::Separator();
			ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_ShowDemoWindow);
#endif
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Game"))
		{
			const bool gameWindowRunning = m_GameWindow.IsRunning();
			if (ImGui::MenuItem(ICON_PLAY "  Play", "Ctrl+P", false, editing && !gameWindowRunning))
				PlayInPreferredMode();
			if (ImGui::MenuItem(ICON_MONITOR "  Play Project in Game Window", "F5", false, editing))
				PlayInGameWindow(true);
			if (ImGui::MenuItem(ICON_MONITOR "  Play Scene in Game Window", "F6", false, editing))
				PlayInGameWindow(false);
			if (ImGui::MenuItem(ICON_PLAY "  Play Scene in Editor Viewport", nullptr, false, editing && !gameWindowRunning))
				Play();
			if (ImGui::MenuItem(ICON_PAUSE "  Pause", nullptr, m_SceneState == SceneState::Pause, !editing))
				SetPaused(m_SceneState != SceneState::Pause);
			if (ImGui::MenuItem(ICON_SQUARE "  Stop", "F8", false, !editing || gameWindowRunning))
			{
				if (!editing)
					Stop();
				StopGameWindow();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Play Opens a Game Window", nullptr, m_Settings.PlayInGameWindow))
			{
				m_Settings.PlayInGameWindow = !m_Settings.PlayInGameWindow;
				m_Settings.Save();
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_ROCKET "  Export Game..."))
			{
				m_DialogPath = (m_Project->GetProjectDirectory() / "Build").string();
				m_LastExport = {};
				m_ShowExportDialog = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			m_ScriptEditorPanel->DrawHelpMenuItems();
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_INFO "  About MageCap Bellows"))
				m_ShowAboutDialog = true;
			ImGui::EndMenu();
		}

		// Project name, right aligned.
		const std::string projectLabel = std::string(ICON_PACKAGE "  ") + m_Project->GetConfig().Name;
		const float width = ImGui::CalcTextSize(projectLabel.c_str()).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - width - 20.0f * Theme::GetScale());
		UI::TextMuted(projectLabel.c_str());

		ImGui::EndMenuBar();
		ImGui::PopStyleVar();
	}

	void EditorLayer::DrawToolbar()
	{
		const float scale = Theme::GetScale();
		const float height = ImGui::GetFrameHeight() + 10.0f * scale;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(Theme::Background));
		ImGui::BeginChild("##Toolbar", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();

		const float buttonSize = ImGui::GetFrameHeight();
		const float spacing = 4.0f * scale;
		const float modeButtonWidth = buttonSize * 0.6f;
		const float totalWidth = buttonSize * 3.0f + modeButtonWidth + spacing * 3.0f;

		// Workspace switcher on the left.
		const Workspace workspace = GetWorkspace();
		ImGui::SetCursorPos(ImVec2(10.0f * scale, 5.0f * scale));
		struct WorkspaceButton { const char* Label; const char* Tooltip; Workspace Target; };
		const WorkspaceButton workspaceButtons[] = {
			{ ICON_BOX "  Scene", "The 3D scene and viewport", Workspace::Scene },
			{ ICON_CODE "  Script", "The Script Editor for ForgeScript and Python", Workspace::Script },
		};
		for (const WorkspaceButton& button : workspaceButtons)
		{
			const ImVec2 size(ImGui::CalcTextSize(button.Label).x + 22.0f * scale, buttonSize);
			if (UI::IconButton(button.Label, button.Tooltip, workspace == button.Target, size))
				SetWorkspace(button.Target);
			ImGui::SameLine(0.0f, spacing);
		}
		ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - totalWidth) * 0.5f, 5.0f * scale));

		const bool editing = m_SceneState == SceneState::Edit;
		const bool gameWindowRunning = m_GameWindow.IsRunning();
		if (editing && !gameWindowRunning)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Success));
			const char* tooltip = m_Settings.PlayInGameWindow ? "Play in a game window (Ctrl+P; F5 plays the project, F6 the scene)"
				: "Play in the editor viewport (Ctrl+P)";
			if (UI::IconButton(ICON_PLAY, tooltip, false, ImVec2(buttonSize, buttonSize)))
				PlayInPreferredMode();
			ImGui::PopStyleColor();
		}
		else
		{
			if (UI::IconButton(ICON_SQUARE, "Stop (Ctrl+P or F8)", true, ImVec2(buttonSize, buttonSize)))
				TogglePlay();
		}
		ImGui::SameLine(0.0f, 1.0f * scale);
		// Where Play runs the game: its own window (like an exported game) or the editor viewport.
		// A narrow button: no side padding, or the chevron would be clipped.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
		const bool openModeMenu = UI::IconButton(ICON_CHEVRON_DOWN "##PlayMode", "Where Play runs the game", false, ImVec2(modeButtonWidth, buttonSize));
		ImGui::PopStyleVar();
		if (openModeMenu)
			ImGui::OpenPopup("##PlayModeMenu");
		if (ImGui::BeginPopup("##PlayModeMenu"))
		{
			if (ImGui::MenuItem(ICON_MONITOR "  Game window", nullptr, m_Settings.PlayInGameWindow))
			{
				m_Settings.PlayInGameWindow = true;
				m_Settings.Save();
			}
			if (ImGui::MenuItem(ICON_PLAY "  Editor viewport (debugger, live Inspector)", nullptr, !m_Settings.PlayInGameWindow))
			{
				m_Settings.PlayInGameWindow = false;
				m_Settings.Save();
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine(0.0f, spacing);
		ImGui::BeginDisabled(editing);
		if (UI::IconButton(ICON_PAUSE, "Pause", m_SceneState == SceneState::Pause, ImVec2(buttonSize, buttonSize)))
			SetPaused(m_SceneState != SceneState::Pause);
		ImGui::SameLine(0.0f, spacing);
		ImGui::BeginDisabled(m_SceneState != SceneState::Pause || m_ScriptDebugger.IsStopped());
		if (UI::IconButton(ICON_STEP_FORWARD, "Advance one frame", false, ImVec2(buttonSize, buttonSize)))
			Step(1);
		ImGui::EndDisabled();
		ImGui::EndDisabled();

		// A thin accent line across the toolbar while playing makes play mode impossible to miss.
		if (!editing || gameWindowRunning)
		{
			const ImVec2 min = ImGui::GetWindowPos();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, min.y + height - 2.0f * scale), ImVec2(min.x + ImGui::GetWindowWidth(), min.y + height),
				m_SceneState == SceneState::Pause ? Theme::Warning : Theme::Accent);
		}

		ImGui::EndChild();
	}

	void EditorLayer::DrawStatusBar()
	{
		const float scale = Theme::GetScale();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(Theme::Background));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 3.0f * scale));
		ImGui::BeginChild("##StatusBar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
		const bool gameWindowRunning = m_GameWindow.IsRunning();
		const char* stateText = m_SceneState == SceneState::Edit ? (gameWindowRunning ? "Running in a game window" : "Editing")
			: (m_SceneState == SceneState::Play ? "Playing" : (m_ScriptDebugger.IsStopped() ? "Stopped in a script" : "Paused"));
		const ImU32 stateColor = m_SceneState == SceneState::Edit ? (gameWindowRunning ? Theme::Accent : Theme::TextMuted)
			: (m_SceneState == SceneState::Play ? Theme::Accent : Theme::Warning);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(stateColor));
		ImGui::Text(ICON_CIRCLE " %s", stateText);
		ImGui::PopStyleColor();

		ImGui::SameLine(0.0f, 18.0f * scale);
		const std::string scene = (m_ScenePath.empty() ? std::string("Untitled scene") : m_ScenePath) + (IsSceneDirty() ? "  (unsaved)" : "");
		UI::TextMuted(scene.c_str());

		const SceneRendererStatistics& stats = m_ViewportRenderer->GetStatistics();
		const float frameTime = Application::Get().GetLastFrameTime() * 1000.0f;
		std::string right = fmt::format("{} entities    {} draws    {:.1f}k tris    {:.1f} ms",
			GetActiveScene() ? GetActiveScene()->GetEntityCount() : 0, stats.DrawCalls, static_cast<double>(stats.Triangles) / 1000.0, frameTime);
		if (m_AgentServer.IsRunning())
			right += fmt::format("    " ICON_BOT " Agent :{}", m_AgentServer.GetPort());
		const float width = ImGui::CalcTextSize(right.c_str()).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - width - 12.0f * scale);
		UI::TextMuted(right.c_str());
		ImGui::PopFont();

		ImGui::EndChild();
	}

	void EditorLayer::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (m_FocusViewport)
		{
			ImGui::SetNextWindowFocus();
			m_FocusViewport = false;
		}
		const bool visible = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();
		m_ViewportVisible = visible;
		if (!visible)
		{
			m_ViewportFocused = false;
			m_ViewportHovered = false;
			ImGui::End();
			return;
		}

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !ImGui::IsAnyItemHovered();
		if (m_ViewportHovered)
			m_ViewportMouseWheel += ImGui::GetIO().MouseWheel;

		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		m_ViewportSize = glm::vec2(std::max(available.x, 1.0f), std::max(available.y, 1.0f));
		m_ViewportPosition = glm::vec2(cursor.x, cursor.y);

		// The image fills the viewport, except in the UI view, where the game's screen is fitted and letterboxed.
		ImVec2 imageSize = available;
		m_ViewportImageOrigin = m_ViewportPosition;
		const glm::vec2 rendered(static_cast<float>(std::max(m_ViewportRenderer->GetViewportWidth(), 1u)),
			static_cast<float>(std::max(m_ViewportRenderer->GetViewportHeight(), 1u)));
		if (IsInUIView())
		{
			const float fit = std::min(m_ViewportSize.x / rendered.x, m_ViewportSize.y / rendered.y);
			imageSize = ImVec2(std::max(std::floor(rendered.x * fit), 1.0f), std::max(std::floor(rendered.y * fit), 1.0f));
			m_ViewportImageOrigin = glm::floor(m_ViewportPosition + (m_ViewportSize - glm::vec2(imageSize.x, imageSize.y)) * 0.5f);
			ImGui::GetWindowDrawList()->AddRectFilled(cursor, ImVec2(cursor.x + available.x, cursor.y + available.y), Theme::Background);
			ImGui::SetCursorScreenPos(ImVec2(m_ViewportImageOrigin.x, m_ViewportImageOrigin.y));
		}
		m_ViewportImageScale = glm::vec2(imageSize.x, imageSize.y) / rendered;
		if (nvrhi::ITexture* image = m_ViewportRenderer->GetFinalImage())
			ImGui::Image(ImTextureRef(reinterpret_cast<ImTextureID>(image)), imageSize);
		else
			ImGui::Dummy(imageSize);

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(UI::AssetPayload))
				HandleViewportDrop(std::string(static_cast<const char*>(payload->Data)));
			ImGui::EndDragDropTarget();
		}

		DrawGizmo();

		bool rectToolHasMouse = false;
		if (IsInUIView() && GetActiveScene())
		{
			const UIRectTool::ImageMapping mapping { m_ViewportImageOrigin, m_ViewportImageScale };
			rectToolHasMouse = m_UIRectTool.OnImGui(*GetActiveScene(), GetPrimarySelection(), mapping, m_ViewportHovered, m_SceneState == SceneState::Edit);
			const std::string finished = m_UIRectTool.TakeFinishedDrag();
			if (!finished.empty())
				CommitChange(finished);
			if (m_UIRectTool.TakeClick())
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				PickEntity(glm::vec2(mouse.x - cursor.x, mouse.y - cursor.y));
			}
		}

		const bool canPick = m_SceneState == SceneState::Edit || !IsUsingGameCamera();
		if (canPick && !rectToolHasMouse && m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt
			&& !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !m_CameraControlActive)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			PickEntity(glm::vec2(mouse.x - cursor.x, mouse.y - cursor.y));
		}

		// Right-click capture: keep the viewport focused while flying.
		if (m_ViewportHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
			ImGui::SetWindowFocus();

		DrawViewportToolbar();

		if (!GetActiveScene())
			UI::CenteredText("No scene is open");

		ImGui::End();
	}

	void EditorLayer::DrawViewportToolbar()
	{
		const float scale = Theme::GetScale();
		const ImVec2 origin = ImVec2(m_ViewportPosition.x + 10.0f * scale, m_ViewportPosition.y + 10.0f * scale);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float buttonSize = ImGui::GetFrameHeight();
		const float padding = 4.0f * scale;

		auto beginOverlay = [&](const char* id, ImVec2 position, int buttons)
		{
			const ImVec2 size(buttonSize * static_cast<float>(buttons) + padding * static_cast<float>(buttons + 1), buttonSize + padding * 2.0f);
			drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(13, 15, 19, 210), 6.0f * scale);
			drawList->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), Theme::Border, 6.0f * scale);
			ImGui::SetCursorScreenPos(ImVec2(position.x + padding, position.y + padding));
			ImGui::PushID(id);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(padding, padding));
			return size;
		};
		auto endOverlay = []()
		{
			ImGui::PopStyleVar();
			ImGui::PopID();
		};

		// Gizmo tools
		const ImVec2 toolsSize = beginOverlay("Tools", origin, 5);
		if (UI::IconButton(ICON_MOUSE_POINTER_2, "Select (Q)", m_GizmoMode == GizmoMode::None)) m_GizmoMode = GizmoMode::None;
		ImGui::SameLine();
		if (UI::IconButton(ICON_MOVE, "Move (W)", m_GizmoMode == GizmoMode::Translate)) m_GizmoMode = GizmoMode::Translate;
		ImGui::SameLine();
		if (UI::IconButton(ICON_ROTATE_3D, "Rotate (E)", m_GizmoMode == GizmoMode::Rotate)) m_GizmoMode = GizmoMode::Rotate;
		ImGui::SameLine();
		if (UI::IconButton(ICON_SCALE_3D, "Scale (R)", m_GizmoMode == GizmoMode::Scale)) m_GizmoMode = GizmoMode::Scale;
		ImGui::SameLine();
		if (UI::IconButton(m_GizmoLocal ? ICON_BOX : ICON_GLOBE, m_GizmoLocal ? "Local space" : "World space", false))
			m_GizmoLocal = !m_GizmoLocal;
		endOverlay();

		// View options, right aligned
		const int viewButtons = 5;
		const float viewWidth = buttonSize * static_cast<float>(viewButtons) + padding * static_cast<float>(viewButtons + 1);
		const ImVec2 viewOrigin(m_ViewportPosition.x + m_ViewportSize.x - viewWidth - 10.0f * scale, origin.y);
		beginOverlay("View", viewOrigin, viewButtons);
		if (UI::IconButton(ICON_GRID_3X3, "Grid", m_Settings.ShowGrid))
			m_Settings.ShowGrid = !m_Settings.ShowGrid;
		ImGui::SameLine();
		if (UI::IconButton(ICON_MONITOR, "Game UI (always shown through the game camera)", m_Settings.ShowUI))
			m_Settings.ShowUI = !m_Settings.ShowUI;
		ImGui::SameLine();
		if (UI::IconButton(ICON_BOX, "Collider outlines", m_ShowColliders))
			m_ShowColliders = !m_ShowColliders;
		ImGui::SameLine();
		if (UI::IconButton(ICON_ACTIVITY, "Statistics", m_ShowStatistics))
			m_ShowStatistics = !m_ShowStatistics;
		ImGui::SameLine();
		if (m_SceneState == SceneState::Edit)
		{
			const std::string tooltip = fmt::format("UI view: the game's screen at {} x {}, for laying out UI (View > UI View Size)",
				m_Settings.UIViewWidth, m_Settings.UIViewHeight);
			if (UI::IconButton(ICON_LAYOUT_TEMPLATE, tooltip.c_str(), m_Settings.UIView))
				m_Settings.UIView = !m_Settings.UIView;
		}
		else
		{
			if (UI::IconButton(ICON_CAMERA, m_FreeCameraInPlay ? "Free camera (click for game camera)" : "Game camera (click for free camera)", m_FreeCameraInPlay))
				m_FreeCameraInPlay = !m_FreeCameraInPlay;
		}
		endOverlay();
		(void)toolsSize;

		// The UI view's size, under the image, opens the size menu.
		if (IsInUIView())
		{
			const std::string label = fmt::format(ICON_LAYOUT_TEMPLATE "  {} x {}", m_Settings.UIViewWidth, m_Settings.UIViewHeight);
			const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			ImGui::SetCursorScreenPos(ImVec2(m_ViewportPosition.x + m_ViewportSize.x - textSize.x - 22.0f * scale,
				m_ViewportPosition.y + m_ViewportSize.y - textSize.y - 18.0f * scale));
			if (ImGui::SmallButton(label.c_str()))
				ImGui::OpenPopup("UIViewSize");
			UI::Tooltip("The size of the game's screen in the UI view");
			if (ImGui::BeginPopup("UIViewSize"))
			{
				DrawUIViewSizeMenu();
				ImGui::EndPopup();
			}
		}

		// Camera speed hint while flying
		if (m_Camera.IsFlying())
		{
			const std::string text = fmt::format(ICON_GAUGE "  {:.1f} m/s", m_Camera.GetFlySpeed());
			const ImVec2 position(m_ViewportPosition.x + 12.0f * scale, m_ViewportPosition.y + m_ViewportSize.y - 30.0f * scale);
			drawList->AddText(position, Theme::TextMuted, text.c_str());
		}
	}

	void EditorLayer::DrawUIViewSizeMenu()
	{
		struct Preset { const char* Name; uint32_t Width; uint32_t Height; };
		static constexpr Preset presets[] = {
			{ "1280 x 720 (HD)", 1280, 720 },
			{ "1920 x 1080 (Full HD)", 1920, 1080 },
			{ "2560 x 1440 (QHD)", 2560, 1440 },
			{ "3840 x 2160 (4K)", 3840, 2160 },
			{ "2560 x 1080 (Ultrawide)", 2560, 1080 },
			{ "1280 x 800 (16:10)", 1280, 800 },
			{ "1024 x 768 (4:3)", 1024, 768 },
			{ "1080 x 1920 (Portrait)", 1080, 1920 },
		};
		for (const Preset& preset : presets)
		{
			const bool current = m_Settings.UIViewWidth == preset.Width && m_Settings.UIViewHeight == preset.Height;
			if (ImGui::MenuItem(preset.Name, nullptr, current))
			{
				m_Settings.UIViewWidth = preset.Width;
				m_Settings.UIViewHeight = preset.Height;
				m_Settings.UIView = true;
			}
		}
		ImGui::Separator();
		int size[2] = { static_cast<int>(m_Settings.UIViewWidth), static_cast<int>(m_Settings.UIViewHeight) };
		ImGui::SetNextItemWidth(140.0f * Theme::GetScale());
		if (ImGui::InputInt2("Custom", size, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			m_Settings.UIViewWidth = static_cast<uint32_t>(std::clamp(size[0], 64, 8192));
			m_Settings.UIViewHeight = static_cast<uint32_t>(std::clamp(size[1], 64, 8192));
			m_Settings.UIView = true;
		}
	}

	void EditorLayer::DrawGizmo()
	{
		// UI entities are placed by their UI rect, not their transform, so the 3D gizmo would mislead.
		Entity entity = GetPrimarySelection();
		// The UI view looks through the game camera, and is for laying out UI.
		if (!entity || m_GizmoMode == GizmoMode::None || IsUsingGameCamera() || IsInUIView() || GetActiveScene()->IsInsideUICanvas(entity))
		{
			if (m_GizmoWasUsing && !ImGuizmo::IsUsing())
				m_GizmoWasUsing = false;
			return;
		}

		const Ref<Scene>& scene = GetActiveScene();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(m_ViewportPosition.x, m_ViewportPosition.y, m_ViewportSize.x, m_ViewportSize.y);
		ImGuizmo::Style& style = ImGuizmo::GetStyle();
		style.TranslationLineThickness = 3.0f * Theme::GetScale();
		style.RotationLineThickness = 3.0f * Theme::GetScale();

		const glm::mat4 view = m_Camera.GetViewMatrix();
		const glm::mat4 projection = m_Camera.GetProjectionMatrix();
		glm::mat4 world = scene->GetWorldTransform(entity);

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		float snapValue = m_TranslateSnap;
		if (m_GizmoMode == GizmoMode::Rotate)
		{
			operation = ImGuizmo::ROTATE;
			snapValue = m_RotateSnap;
		}
		else if (m_GizmoMode == GizmoMode::Scale)
		{
			operation = ImGuizmo::SCALE;
			snapValue = m_ScaleSnap;
		}
		const bool snap = ImGui::GetIO().KeyCtrl;
		const float snapValues[3] = { snapValue, snapValue, snapValue };

		ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation,
			m_GizmoLocal || m_GizmoMode == GizmoMode::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
			glm::value_ptr(world), nullptr, snap ? snapValues : nullptr);

		if (ImGuizmo::IsUsing())
		{
			scene->SetWorldTransform(entity, world);
			if (scene->GetPhysicsWorld() && scene->GetPhysicsWorld()->HasBody(entity))
				scene->GetPhysicsWorld()->TeleportToEntity(entity);
			m_GizmoWasUsing = true;
		}
		else if (m_GizmoWasUsing)
		{
			m_GizmoWasUsing = false;
			CommitChange("Transform");
		}
	}

	void EditorLayer::SetSkyEnvironment(const std::string& assetPath)
	{
		Entity sky;
		for (Entity entity : GetActiveScene()->GetAllEntities())
		{
			if (entity.HasComponent<SkyLightComponent>())
			{
				sky = entity;
				break;
			}
		}
		if (!sky)
			sky = GetActiveScene()->CreateEntity("Sky");
		sky.AddOrReplaceComponent<SkyLightComponent>().EnvironmentPath = assetPath;
		CommitChange("Set environment");
	}

	void EditorLayer::HandleViewportDrop(const std::string& assetPath)
	{
		const AssetType type = AssetManager::GetAssetType(assetPath);

		// Drop point: where the mouse ray hits the ground plane (or in front of the camera).
		const ImVec2 mouse = ImGui::GetMousePos();
		glm::vec3 origin(0.0f), direction(0.0f);
		const RenderCamera camera = m_Camera.GetRenderCamera();
		Math::ScreenPointToRay(glm::vec2(mouse.x, mouse.y) - m_ViewportPosition, m_ViewportSize,
			glm::inverse(camera.Projection * camera.View), origin, direction);
		glm::vec3 position = origin + direction * 8.0f;
		if (direction.y < -1e-3f)
		{
			const float t = -origin.y / direction.y;
			if (t > 0.0f && t < 200.0f)
				position = origin + direction * t;
		}

		switch (type)
		{
			case AssetType::Mesh:
				CreateMeshEntity(assetPath, position);
				break;
			case AssetType::Prefab:
				InstantiatePrefab(assetPath, position);
				break;
			case AssetType::Scene:
				Application::Get().SubmitToMainThread([this, assetPath] { OpenScene(assetPath); });
				break;
			case AssetType::Environment:
				SetSkyEnvironment(assetPath);
				break;
			case AssetType::Texture:
				// An OpenEXR image dropped on the scene is a sky (HDRIs); as a material map it goes on a material.
				if (AssetManager::CanUseAs(assetPath, AssetType::Environment))
					SetSkyEnvironment(assetPath);
				break;
			case AssetType::Material:
			{
				// Materials apply to whatever is under the cursor.
				PickEntity(glm::vec2(mouse.x, mouse.y) - m_ViewportPosition);
				if (Entity entity = GetPrimarySelection(); entity && entity.HasComponent<MeshRendererComponent>())
				{
					entity.GetComponent<MeshRendererComponent>().MaterialPath = assetPath;
					CommitChange("Assign material");
				}
				break;
			}
			default:
				break;
		}
	}

	ExportResult EditorLayer::ExportGame(const std::filesystem::path& outputDirectory)
	{
		if (!m_Project)
		{
			ExportResult result;
			result.Messages.push_back("Error: no project is open");
			return result;
		}
		if (IsSceneDirty() && !m_ScenePath.empty())
			SaveScene();
		ExportOptions options;
		options.OutputDirectory = outputDirectory;
		return ProjectExporter::Export(*m_Project, options);
	}

	// -------------------------------------------------------------------------
	// Project hub and dialogs
	// -------------------------------------------------------------------------

	void EditorLayer::DrawDialogs()
	{
		const float scale = Theme::GetScale();
		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();

		auto beginModal = [&](const char* title, bool& open, float width)
		{
			if (open && !ImGui::IsPopupOpen(title))
				ImGui::OpenPopup(title);
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(width * scale, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 16.0f * scale));
			const bool visible = ImGui::BeginPopupModal(title, &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
			ImGui::PopStyleVar();
			// Escape cancels a dialog unless a text field is using the key.
			if (visible && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive())
			{
				open = false;
				ImGui::CloseCurrentPopup();
			}
			return visible;
		};

		auto primaryButton = [&](const char* label, bool enabled = true)
		{
			ImGui::BeginDisabled(!enabled);
			ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::Accent));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::AccentHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::ToVec4(Theme::Accent));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.04f, 0.03f, 1.0f));
			const bool pressed = ImGui::Button(label, ImVec2(120.0f * scale, 0.0f));
			ImGui::PopStyleColor(4);
			ImGui::EndDisabled();
			return pressed;
		};

		auto pathField = [&](const char* id, std::string& value, bool folder, const std::vector<std::string>& filters)
		{
			ImGui::PushID(id);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			ImGui::InputText("##path", &value);
			ImGui::SameLine();
			if (ImGui::Button(ICON_FOLDER_OPEN, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
			{
				const std::string chosen = folder ? UI::SelectFolderDialog("Choose a folder", value) : UI::OpenFileDialog("Open", filters, value);
				if (!chosen.empty())
					value = chosen;
			}
			UI::Tooltip("Browse...");
			ImGui::PopID();
		};

		if (m_Project)
			m_ProjectManager->DrawDialogs();

		if (beginModal("Close Project", m_ShowCloseProjectDialog, 480.0f))
		{
			const bool untitled = m_ScenePath.empty();
			ImGui::PushTextWrapPos(0.0f);
			if (untitled)
				ImGui::TextUnformatted("The scene has never been saved. Close the project anyway? Its changes will be lost.");
			else
				ImGui::Text("Save changes to '%s' before closing the project?", m_ScenePath.c_str());
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			bool close = false;
			if (!untitled)
			{
				if (primaryButton(ICON_SAVE "  Save & Close"))
					close = SaveScene();
				ImGui::SameLine();
			}
			if (ImGui::Button("Close Without Saving", ImVec2(170.0f * scale, 0.0f)))
				close = true;
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f * scale, 0.0f)))
			{
				m_ShowCloseProjectDialog = false;
				ImGui::CloseCurrentPopup();
			}
			if (close)
			{
				m_ShowCloseProjectDialog = false;
				ImGui::CloseCurrentPopup();
				Application::Get().SubmitToMainThread([this] { CloseProject(); });
			}
			ImGui::EndPopup();
		}

		if (beginModal("Open Project", m_ShowOpenProjectDialog, 560.0f))
		{
			UI::TextMuted("Project file (.bproject) or project folder");
			pathField("project", m_DialogPath, false, { "MageCap Bellows project", "*.bproject" });
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (primaryButton("Open", !m_DialogPath.empty()))
			{
				if (OpenProject(m_DialogPath))
					m_ShowOpenProjectDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f * scale, 0.0f)))
			{
				m_ShowOpenProjectDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (beginModal("Save Scene As", m_ShowSaveSceneAsDialog, 520.0f))
		{
			UI::TextMuted("Scene path inside the project's assets");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			const bool enter = ImGui::InputText("##scenepath", &m_DialogPath, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (primaryButton("Save", !m_DialogPath.empty()) || (enter && !m_DialogPath.empty()))
			{
				SaveSceneAs(m_DialogPath);
				m_ShowSaveSceneAsDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f * scale, 0.0f)))
			{
				m_ShowSaveSceneAsDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (beginModal("Export Game", m_ShowExportDialog, 600.0f))
		{
			UI::TextMuted("Output folder");
			pathField("output", m_DialogPath, true, {});
			ImGui::Spacing();
			ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
			ImGui::TextDisabled("Builds a standalone game for this platform: the runtime, your assets and a bundled Python runtime.");
			ImGui::PopFont();
			if (!m_LastExport.Messages.empty())
			{
				ImGui::Spacing();
				ImGui::BeginChild("##ExportLog", ImVec2(0.0f, 110.0f * scale), ImGuiChildFlags_Borders);
				for (const std::string& message : m_LastExport.Messages)
				{
					const bool isError = message.rfind("Error", 0) == 0;
					ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(isError ? Theme::Error : Theme::TextMuted));
					ImGui::TextWrapped("%s", message.c_str());
					ImGui::PopStyleColor();
				}
				ImGui::EndChild();
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (primaryButton("Export", !m_DialogPath.empty()))
				m_LastExport = ExportGame(m_DialogPath);
			ImGui::SameLine();
			ImGui::BeginDisabled(!m_LastExport.Success);
			if (ImGui::Button(ICON_EXTERNAL_LINK "  Open Folder", ImVec2(140.0f * scale, 0.0f)))
				Platform::OpenWithDefaultApplication(m_DialogPath);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Close", ImVec2(120.0f * scale, 0.0f)))
			{
				m_ShowExportDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (beginModal("About MageCap Bellows", m_ShowAboutDialog, 480.0f))
		{
			DrawLogo(56.0f * scale);
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::PushFont(Theme::GetFonts().Bold, Theme::BaseFontSize * scale * 1.4f);
			ImGui::TextUnformatted("MageCap Bellows");
			ImGui::PopFont();
			UI::TextMuted(("Version " + BuildInfo::GetVersionText() + "  -  (c) MageCap Games").c_str());
			ImGui::EndGroup();
			ImGui::Spacing();
			ImGui::TextWrapped("Renderer: %s (Vulkan via NVRHI)", Application::Get().GetRenderDevice().GetAdapterName().c_str());
			ImGui::Spacing();
			ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
			ImGui::TextWrapped("Built with GLFW, NVRHI, Dear ImGui, ImGuizmo, EnTT, Jolt Physics, miniaudio, pybind11, glm, spdlog, cgltf, "
				"stb, nlohmann/json, portable-file-dialogs, Inter, JetBrains Mono and Lucide icons. See ThirdPartyNotices.md.");
			ImGui::PopFont();
			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(120.0f * scale, 0.0f)))
			{
				m_ShowAboutDialog = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		bool showMessage = !m_DialogMessage.empty();
		if (beginModal("Notice", showMessage, 460.0f))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
			ImGui::TextUnformatted(ICON_TRIANGLE_ALERT);
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::TextWrapped("%s", m_DialogMessage.c_str());
			ImGui::Spacing();
			if (primaryButton("OK"))
			{
				m_DialogMessage.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (!showMessage)
			m_DialogMessage.clear();
	}

}
