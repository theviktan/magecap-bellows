#pragma once

#include "AgentServer.h"
#include "EditorCamera.h"
#include "EditorSettings.h"
#include "GameWindowSession.h"
#include "ScriptDebugger.h"
#include "UIRectTool.h"
#include "UndoHistory.h"

#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/FileWatcher.h"
#include "Bellows/Core/Layer.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Project/ProjectExporter.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	class SceneHierarchyPanel;
	class InspectorPanel;
	class ContentBrowserPanel;
	class ConsolePanel;
	class SettingsPanel;
	class StatisticsPanel;
	class ScriptEditorPanel;
	class DebuggerPanel;
	class ProjectManager;

	enum class SceneState : uint8_t
	{
		Edit = 0,
		Play,
		Pause,
	};

	// The main areas of the editor, switched from the toolbar like Godot's 2D/3D/Script buttons.
	enum class Workspace : uint8_t
	{
		Scene = 0,	// The 3D viewport
		Script		// The built-in Script Editor
	};

	enum class GizmoMode : uint8_t
	{
		None = 0,
		Translate,
		Rotate,
		Scale,
	};

	struct EditorOptions
	{
		std::filesystem::path ProjectPath;		// Open this project at startup
		std::filesystem::path StartupScript;	// Python automation script to run once the editor is ready
		bool ExitAfterScript = false;
		bool Headless = false;
		bool DisableAgentServer = false;
		uint16_t AgentServerPort = 0;			// 0 = use the saved preference
	};

	// The editor: project and scene management, play mode, selection, undo, the viewport,
	// all panels and the automation surface used by AI agents.
	class EditorLayer : public Layer
	{
	public:
		explicit EditorLayer(EditorOptions options);
		~EditorLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnImGuiRender() override;
		void OnEvent(Event& event) override;

		static EditorLayer& Get() { return *s_Instance; }
		static bool HasInstance() { return s_Instance != nullptr; }

		// Project ------------------------------------------------------------
		// Creates a project in 'directory' and opens it. 'emptyScene' starts with an empty scene
		// instead of the starter scene (camera, light, ground and a cube).
		bool NewProject(const std::filesystem::path& directory, const std::string& name, bool emptyScene = false);
		bool OpenProject(const std::filesystem::path& path);
		Ref<Project> GetProject() const { return m_Project; }
		// Saves open scripts, closes the project and returns to the Project Manager.
		void CloseProject();

		// Scenes (paths are asset paths or absolute paths) -------------------
		void NewScene(const std::string& name = "Untitled");
		bool OpenScene(const std::string& path);
		bool SaveScene();
		bool SaveSceneAs(const std::string& assetPath);
		const std::string& GetScenePath() const { return m_ScenePath; }
		bool IsSceneDirty() const;
		const Ref<Scene>& GetEditorScene() const { return m_EditorScene; }
		// The scene being shown: the play-mode copy while playing, otherwise the edited scene.
		const Ref<Scene>& GetActiveScene() const { return m_RuntimeScene ? m_RuntimeScene : m_EditorScene; }

		// Play mode ----------------------------------------------------------
		// Play in the editor viewport, with a copy of the edited scene (the debugger and live Inspector work here).
		void Play();
		void Stop();
		// Play the way the settings say: in a game window or in the viewport. The toolbar's Play and Ctrl+P.
		void PlayInPreferredMode();
		// Play in a game window: BellowsRuntime in its own window and process. startScene plays the project's
		// start scene, otherwise the open scene; unsaved edits to the open scene play too.
		bool PlayInGameWindow(bool startScene, bool headless = false, uint64_t frames = 0);
		void StopGameWindow();
		bool IsGameWindowRunning() { return m_GameWindow.IsRunning(); }
		std::optional<int> GetGameWindowExitCode() const { return m_GameWindow.GetLastExitCode(); }
		void SetPaused(bool paused);
		void Step(uint32_t frames = 1);
		SceneState GetSceneState() const { return m_SceneState; }

		// Selection ----------------------------------------------------------
		void Select(Entity entity, bool additive = false);
		void SetSelection(const std::vector<UUID>& selection);
		void ClearSelection();
		Entity GetPrimarySelection() const;
		const std::vector<UUID>& GetSelection() const { return m_Selection; }
		bool IsSelected(Entity entity) const;

		// Commands (undoable) -------------------------------------------------
		Entity CreateEntity(const std::string& name, Entity parent = {});
		Entity CreatePrimitive(const std::string& meshPath, const std::string& name);
		Entity CreateMeshEntity(const std::string& meshPath, const glm::vec3& position);
		// Writes a model's '.bimport' with this front and reloads it everywhere it is placed. False when
		// the path isn't a model file or the settings can't be written.
		bool SetModelFront(const std::string& meshPath, ModelFront front);
		// Placed models load their mesh again (all of them when meshPath is empty), after import settings change.
		void ReloadPlacedMeshes(const std::string& meshPath = {});
		Entity InstantiatePrefab(const std::string& prefabPath, const glm::vec3& position);
		void DeleteSelection();
		void DuplicateSelection();
		void FocusSelection();
		bool SavePrefab(Entity entity, const std::string& assetPath);
		// Gives the entity a Script component running 'scriptPath' (.forge or .py), replacing any script it had.
		bool AttachScript(Entity entity, const std::string& scriptPath);
		void DetachScript(Entity entity);

		// Undo: call CommitChange after any change to the edited scene.
		void CommitChange(const std::string& description);
		void Undo();
		void Redo();
		const UndoHistory& GetUndoHistory() const { return m_UndoHistory; }

		// Viewport -----------------------------------------------------------
		EditorCamera& GetCamera() { return m_Camera; }
		SceneRenderer& GetViewportRenderer() { return *m_ViewportRenderer; }
		// Renders the active scene now and returns the image (used by screenshots and agents).
		Image CaptureViewport();
		// What bellows.save_screenshot() saves: the game camera's image without editor overlays.
		std::optional<Image> CaptureGameView();
		void SetViewportSizeOverride(uint32_t width, uint32_t height);
		GizmoMode GetGizmoMode() const { return m_GizmoMode; }
		bool IsUsingGameCamera() const;
		// The UI view (edit mode only): the viewport shows the game's screen at the UI view size, fitted,
		// with handles on the selected UI rect.
		bool IsInUIView() const;
		// Hot reload: hands scripts saved since the last poll to the script engine.
		void ReloadChangedScripts();

		// Workspaces and the Script Editor ----------------------------------
		void SetWorkspace(Workspace workspace);
		Workspace GetWorkspace() const;
		ScriptEditorPanel& GetScriptEditor() { return *m_ScriptEditorPanel; }
		// The app icon (Resources/Icons/Bellows.png) for logos; null when it could not be loaded.
		const Ref<Texture2D>& GetLogo() const { return m_Logo; }
		// ForgeScript's icon (Resources/Icons/ForgeScript.png) for .forge files; null when it could not be loaded.
		const Ref<Texture2D>& GetForgeScriptIcon() const { return m_ForgeScriptIcon; }
		// Draws the logo as a square item of the given size (the flame glyph if the icon is missing).
		void DrawLogo(float size) const;

		// ForgeScript debugger -----------------------------------------------
		// Breakpoints and the current stop. A stop pauses the game until the script goes on.
		ScriptDebugger& GetScriptDebugger() { return m_ScriptDebugger; }
		enum class DebugAction : uint8_t { Continue, StepOver, StepIn, StepOut };
		// Lets the stopped script go on; the game runs again unless a script stopped again.
		// Returns false when no script is stopped.
		bool ResumeScripts(DebugAction action);
		// Stops at the next ForgeScript line that runs (the game must be running).
		bool PauseScripts();

		// Build and settings -------------------------------------------------
		ExportResult ExportGame(const std::filesystem::path& outputDirectory);
		EditorSettings& GetSettings() { return m_Settings; }
		AgentServer& GetAgentServer() { return m_AgentServer; }

		// Advances the editor by whole frames (automation scripts use this to let time pass).
		// Only valid inside RunAtFrameBoundary, where no frame is in progress.
		void RunFrames(uint32_t frames);
		bool CanRunFrames() const { return m_AtFrameBoundary && !m_RunningFrames; }
		// Runs 'function' at the start of the next frame, before any frame work begins. Automation
		// (agent requests, the console prompt, startup scripts) runs there so it may call RunFrames.
		void RunAtFrameBoundary(std::function<void()> function);
		void RequestExit(int exitCode = 0);

		// Executes Python in the editor context and records one undo step if the scene changed.
		bool ExecutePython(const std::string& source, std::string& outOutput);
	private:
		void DrawMenuBar();
		void DrawToolbar();
		void DrawStatusBar();
		void DrawViewport();
		void DrawViewportToolbar();
		void DrawUIViewSizeMenu();
		void DrawGizmo();
		void DrawDialogs();
		void BuildDefaultLayout(unsigned int dockspaceID);

		void RenderViewport();
		void SubmitEditorOverlays(SceneRenderer& renderer);
		void HandleShortcuts();
		// Ctrl+P: stop whatever plays, otherwise play in the preferred mode.
		void TogglePlay();
		void HandlePlayKeys(bool ctrl);
		void HandleViewportDrop(const std::string& assetPath);
		// Makes the image the scene's sky (adding a Sky entity when there is none).
		void SetSkyEnvironment(const std::string& assetPath);
		void PickEntity(const glm::vec2& viewportPosition);
		void ResetEditorStateForScene();
		std::string SerializeEditorScene() const;
		void LoadSnapshot(const UndoHistory::Snapshot& snapshot);
		void UpdateWindowTitle();
		void StartAgentServer();
		Json HandleAgentRequest(const std::string& method, const Json& params);
		void RunStartupScript();
		void SaveLayout();
		bool IsViewportInputActive() const;
		// After a script stopped: pause the game and show the stop in the Script Editor.
		void HandleDebugStop();
		// After a script went on: let the game run again if the debugger paused it.
		void ResumeAfterDebugStop();
	private:
		static EditorLayer* s_Instance;

		EditorOptions m_Options;
		EditorSettings m_Settings;
		GameWindowSession m_GameWindow;
		Ref<Project> m_Project;

		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;
		std::string m_ScenePath;			// Asset path of the open scene; empty for unsaved scenes
		SceneState m_SceneState = SceneState::Edit;
		std::optional<std::string> m_PendingSceneLoad;

		std::vector<UUID> m_Selection;
		UndoHistory m_UndoHistory;
		std::string m_CurrentSnapshot;		// Serialized edited scene as of the last commit
		std::string m_SavedSnapshot;		// Serialized edited scene as of the last save
		bool m_WasEditingWidget = false;

		EditorCamera m_Camera;
		Scope<SceneRenderer> m_ViewportRenderer;
		// Renders the game camera for screenshots while the viewport shows the free camera; made on first use.
		Scope<SceneRenderer> m_ScreenshotRenderer;
		// True when the viewport's last image came from the game camera (no editor overlays).
		bool m_ViewportShowsGameView = false;
		glm::vec2 m_ViewportSize { 1280.0f, 720.0f };
		glm::vec2 m_ViewportPosition { 0.0f };
		glm::uvec2 m_ViewportSizeOverride { 0, 0 };
		// Where the rendered image is drawn in the viewport window, and screen pixels per rendered pixel.
		glm::vec2 m_ViewportImageOrigin { 0.0f };
		glm::vec2 m_ViewportImageScale { 1.0f };
		UIRectTool m_UIRectTool;
		bool m_ViewportHovered = false;
		bool m_ViewportFocused = false;
		float m_ViewportMouseWheel = 0.0f;		// Collected during the UI frame for the next camera update
		bool m_CameraControlActive = false;
		// Hot reload: script files saved during play are polled a few times a second.
		static constexpr float c_ScriptWatchInterval = 0.5f;
		FileWatcher m_ScriptWatcher;
		float m_ScriptWatchElapsed = 0.0f;
		// Declared before the panels, which use it, so it outlives them.
		ScriptDebugger m_ScriptDebugger;
		bool m_DebugStopPending = false;	// A script stopped; the game pauses at the end of the update
		bool m_PausedByDebugger = false;	// The game was running when the script stopped
		bool m_FreeCameraInPlay = false;
		bool m_ShowColliders = true;
		GizmoMode m_GizmoMode = GizmoMode::Translate;
		bool m_GizmoLocal = false;
		bool m_GizmoWasUsing = false;
		float m_TranslateSnap = 0.5f;
		float m_RotateSnap = 15.0f;
		float m_ScaleSnap = 0.1f;

		Scope<SceneHierarchyPanel> m_HierarchyPanel;
		Scope<InspectorPanel> m_InspectorPanel;
		Scope<ContentBrowserPanel> m_ContentBrowserPanel;
		Scope<ConsolePanel> m_ConsolePanel;
		Scope<SettingsPanel> m_SettingsPanel;
		Scope<StatisticsPanel> m_StatisticsPanel;
		Scope<ScriptEditorPanel> m_ScriptEditorPanel;
		Scope<DebuggerPanel> m_DebuggerPanel;
		Ref<Texture2D> m_Logo;
		Ref<Texture2D> m_ForgeScriptIcon;
		Scope<ProjectManager> m_ProjectManager;
		bool m_ViewportVisible = false;
		bool m_FocusViewport = false;

		// Dialog state
		bool m_ShowCloseProjectDialog = false;
		bool m_ShowOpenProjectDialog = false;
		bool m_ShowExportDialog = false;
		bool m_ShowAboutDialog = false;
		bool m_ShowSaveSceneAsDialog = false;
		bool m_ShowStatistics = false;
		bool m_ShowDemoWindow = false;
		std::string m_DialogPath;
		std::string m_DialogMessage;
		ExportResult m_LastExport;

		AgentServer m_AgentServer;
		bool m_StartupScriptPending = false;
		bool m_LayoutInitialized = false;
		bool m_RunningFrames = false;
		bool m_AtFrameBoundary = false;
		bool m_AgentRequestsQueued = false;
		bool m_ProcessingAgentRequests = false;
		float m_LastFrameTime = 0.0f;
		std::filesystem::path m_LayoutPath;

		friend class SceneHierarchyPanel;
		friend class InspectorPanel;
		friend class ContentBrowserPanel;
	};

}
