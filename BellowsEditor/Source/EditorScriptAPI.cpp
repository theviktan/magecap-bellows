#include "EditorScriptAPI.h"

#include "EditorLayer.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Scripting/ScriptEngine.h"
#include "Bellows/Scripting/ScriptGlue.h"

#include <pybind11/stl.h>

namespace py = pybind11;

namespace Bellows {

	namespace {

		using Scripting::ScriptEntity;

		EditorLayer& Editor()
		{
			if (!EditorLayer::HasInstance())
				throw std::runtime_error("The editor is not running");
			return EditorLayer::Get();
		}

		const char* StateName(SceneState state)
		{
			switch (state)
			{
				case SceneState::Edit: return "edit";
				case SceneState::Play: return "play";
				case SceneState::Pause: return "pause";
			}
			return "edit";
		}

		glm::vec3 ToVec3(const py::handle& value)
		{
			if (py::isinstance<glm::vec3>(value))
				return value.cast<glm::vec3>();
			const auto sequence = value.cast<std::vector<float>>();
			if (sequence.size() != 3)
				throw py::value_error("expected a Vec3 or a sequence of three numbers");
			return { sequence[0], sequence[1], sequence[2] };
		}

		Entity ResolveInActiveScene(const ScriptEntity& handle)
		{
			Entity entity = handle.Resolve();
			if (entity.GetScene() != Editor().GetActiveScene().get())
				throw std::runtime_error("The entity belongs to a scene the editor is not showing");
			return entity;
		}

		void Define(py::module_& m)
		{
			m.doc() = "Editor automation API (only available inside MageCap Bellows Editor)";

			// Project ---------------------------------------------------------------------------
			m.def("project_name", [] { return Editor().GetProject() ? Editor().GetProject()->GetConfig().Name : std::string(); },
				"Name of the open project ('' when none)");
			m.def("project_directory", [] { return Editor().GetProject() ? Editor().GetProject()->GetProjectDirectory().string() : std::string(); },
				"Absolute path of the open project's folder");
			m.def("asset_directory", [] { return AssetManager::GetAssetDirectory().string(); },
				"Absolute path of the open project's asset folder");
			m.def("new_project", [](const std::string& directory, const std::string& name) { return Editor().NewProject(directory, name); },
				py::arg("directory"), py::arg("name"), "Creates a project in 'directory' and opens it");
			m.def("open_project", [](const std::string& path) { return Editor().OpenProject(path); },
				py::arg("path"), "Opens a .bproject file (or a folder containing one)");

			// Scenes ----------------------------------------------------------------------------
			m.def("new_scene", [](const std::string& name) { Editor().NewScene(name); },
				py::arg("name") = "Untitled", "Replaces the open scene with a new one (camera, sun and sky)");
			m.def("open_scene", [](const std::string& path) { return Editor().OpenScene(path); },
				py::arg("path"), "Opens a scene by asset path, e.g. 'Scenes/Main.bscene'");
			m.def("save_scene", [](const std::optional<std::string>& path)
			{
				EditorLayer& editor = Editor();
				if (path)
					return editor.SaveSceneAs(*path);
				if (editor.GetScenePath().empty())
					return editor.SaveSceneAs("Scenes/" + editor.GetEditorScene()->GetName() + ".bscene");
				return editor.SaveScene();
			}, py::arg("path") = py::none(), "Saves the edited scene (to 'path' when given); returns success");
			m.def("scene_path", [] { return Editor().GetScenePath(); }, "Asset path of the open scene ('' if never saved)");
			m.def("is_dirty", [] { return Editor().IsSceneDirty(); }, "True when the scene has unsaved changes");

			// Play mode -------------------------------------------------------------------------
			m.def("play", [] { Editor().Play(); }, "Enters play mode with a copy of the edited scene");
			m.def("stop", [] { Editor().Stop(); }, "Leaves play mode; the edited scene is unchanged");
			m.def("pause", [](bool paused) { Editor().SetPaused(paused); }, py::arg("paused") = true);
			m.def("step", [](uint32_t frames) { Editor().Step(frames); }, py::arg("frames") = 1, "Advances a paused game");
			m.def("state", [] { return std::string(StateName(Editor().GetSceneState())); }, "'edit', 'play' or 'pause'");
			m.def("run_frames", [](uint32_t frames)
			{
				Editor().RunFrames(frames);
			}, py::arg("frames") = 1,
				"Runs the editor for whole frames (rendering, play-mode updates, UI). Use it to let time pass in automation.");

			// ForgeScript debugger --------------------------------------------------------------
			m.def("set_breakpoints", [](const std::string& script, const std::vector<int>& lines)
			{
				Editor().GetScriptDebugger().SetBreakpoints(script, lines);
			}, py::arg("script"), py::arg("lines"),
				"Sets the breakpoint lines (1-based) of a ForgeScript asset, e.g. 'Scripts/Player.forge'; [] removes them");
			m.def("breakpoints", [](const std::string& script) { return Editor().GetScriptDebugger().GetBreakpoints(script); },
				py::arg("script"), "The breakpoint lines of a ForgeScript asset, sorted");
			m.def("clear_breakpoints", [] { Editor().GetScriptDebugger().ClearBreakpoints(); }, "Removes every breakpoint");
			m.def("debug_state", []
			{
				const ScriptDebugger& debugger = Editor().GetScriptDebugger();
				const auto describe = [](const std::vector<ForgeScript::DebugVariable>& variables)
				{
					py::list result;
					for (const ForgeScript::DebugVariable& variable : variables)
					{
						py::dict entry;
						entry["name"] = variable.Name;
						entry["type"] = variable.Type;
						entry["value"] = variable.Text;
						result.append(entry);
					}
					return result;
				};
				py::dict state;
				const bool stopped = debugger.IsStopped();
				state["stopped"] = stopped;
				py::list stack;
				if (stopped)
				{
					const ForgeScript::DebugStop& stop = debugger.GetStop();
					state["file"] = stop.File;
					state["line"] = stop.Line;
					state["reason"] = stop.Reason == ForgeScript::DebugStopReason::Breakpoint ? "breakpoint"
						: (stop.Reason == ForgeScript::DebugStopReason::Step ? "step" : "pause");
					for (const ForgeScript::DebugFrame& frame : debugger.GetCallStack())
					{
						py::dict entry;
						entry["function"] = frame.Function;
						entry["file"] = frame.File;
						entry["line"] = frame.Line;
						stack.append(entry);
					}
				}
				state["stack"] = stack;
				state["variables"] = describe(debugger.GetVariables(0));
				state["script_variables"] = describe(debugger.GetScriptVariables(0));
				return state;
			}, "Where a stopped ForgeScript is: {'stopped', 'file', 'line', 'reason', 'stack', 'variables', 'script_variables'}");
			m.def("debug_continue", [] { return Editor().ResumeScripts(EditorLayer::DebugAction::Continue); },
				"Lets the stopped script go on (the game runs again); False when nothing is stopped");
			m.def("debug_step_over", [] { return Editor().ResumeScripts(EditorLayer::DebugAction::StepOver); },
				"Runs the stopped script's line, including the functions it calls");
			m.def("debug_step_in", [] { return Editor().ResumeScripts(EditorLayer::DebugAction::StepIn); },
				"Runs the stopped script to its next line, inside a function the line calls");
			m.def("debug_step_out", [] { return Editor().ResumeScripts(EditorLayer::DebugAction::StepOut); },
				"Runs the stopped script to the end of its current function");
			m.def("debug_pause", [] { return Editor().PauseScripts(); }, "Stops at the next ForgeScript line that runs (play mode)");

			// Selection -------------------------------------------------------------------------
			m.def("select", [](const std::optional<ScriptEntity>& entity, bool additive)
			{
				if (!entity)
					Editor().ClearSelection();
				else
					Editor().Select(ResolveInActiveScene(*entity), additive);
			}, py::arg("entity"), py::arg("additive") = false, "Selects an entity (None clears the selection)");
			m.def("selection", []
			{
				std::vector<ScriptEntity> result;
				for (UUID uuid : Editor().GetSelection())
				{
					if (Entity entity = Editor().GetActiveScene()->TryGetEntityByUUID(uuid))
						result.emplace_back(entity);
				}
				return result;
			}, "Selected entities, most recently selected last");
			m.def("clear_selection", [] { Editor().ClearSelection(); });
			m.def("focus", [](const std::optional<ScriptEntity>& entity)
			{
				if (entity)
					Editor().Select(ResolveInActiveScene(*entity));
				Editor().FocusSelection();
			}, py::arg("entity") = py::none(), "Points the editor camera at an entity (default: the selection)");

			// Undo ------------------------------------------------------------------------------
			m.def("commit", [](const std::string& description) { Editor().CommitChange(description); },
				py::arg("description") = "Script", "Records the current scene as one undo step (done automatically after each command)");
			m.def("undo", [] { Editor().Undo(); });
			m.def("redo", [] { Editor().Redo(); });
			m.def("can_undo", [] { return Editor().GetUndoHistory().CanUndo(); });
			m.def("can_redo", [] { return Editor().GetUndoHistory().CanRedo(); });

			// Prefabs and assets ----------------------------------------------------------------
			m.def("save_prefab", [](const ScriptEntity& entity, const std::string& path) { return Editor().SavePrefab(entity.Resolve(), path); },
				py::arg("entity"), py::arg("path"), "Saves an entity and its children as a .bprefab asset");
			m.def("import_asset", [](const std::string& source, const std::string& folder)
			{
				const std::filesystem::path sourcePath(source);
				const std::filesystem::path directory = AssetManager::GetAssetDirectory() / folder;
				FileSystem::CreateDirectories(directory);
				const std::filesystem::path target = directory / sourcePath.filename();
				std::error_code error;
				std::filesystem::copy_file(sourcePath, target, std::filesystem::copy_options::overwrite_existing, error);
				if (error)
					throw std::runtime_error("Could not import '" + source + "': " + error.message());
				const std::string assetPath = AssetManager::GetAssetPath(target);
				AssetManager::Invalidate(assetPath);
				return assetPath;
			}, py::arg("source"), py::arg("folder") = "", "Copies a file into the project's assets; returns its asset path");
			m.def("reload_asset", [](const std::string& path) { AssetManager::Invalidate(path); }, py::arg("path"),
				"Drops a cached asset so it is loaded again from disk");

			// Viewport --------------------------------------------------------------------------
			m.def("set_camera", [](const py::object& position, const py::object& target)
			{
				Editor().GetCamera().SetPose(ToVec3(position), ToVec3(target));
			}, py::arg("position"), py::arg("target"), "Places the editor camera at 'position' looking at 'target'");
			m.def("camera_position", [] { return Editor().GetCamera().GetPosition(); });
			m.def("camera_target", [] { return Editor().GetCamera().GetFocusPoint(); });
			m.def("screenshot", [](const std::string& path, uint32_t width, uint32_t height)
			{
				EditorLayer& editor = Editor();
				if (width > 0 && height > 0)
					editor.SetViewportSizeOverride(width, height);
				const Image image = editor.CaptureViewport();
				editor.SetViewportSizeOverride(0, 0);
				if (!image.IsValid() || !image.SavePNG(path))
					throw std::runtime_error("Could not save the screenshot to '" + path + "'");
				return path;
			}, py::arg("path"), py::arg("width") = 0, py::arg("height") = 0,
				"Renders the viewport (editor camera in edit mode, game camera in play mode) to a PNG file");
			m.def("capture_window", [](const std::string& path)
			{
				EditorLayer& editor = Editor();
				if (!editor.CanRunFrames())
					throw std::runtime_error("capture_window needs to run a frame; call it from automation");
				auto saved = std::make_shared<bool>(false);
				Application::Get().RequestBackBufferCapture([path, saved](const Image& image) { *saved = image.IsValid() && image.SavePNG(path); });
				editor.RunFrames(1);
				if (!*saved)
					throw std::runtime_error("Could not capture the editor window to '" + path + "'");
				return path;
			}, py::arg("path"), "Saves a PNG of the whole editor window (UI included) after rendering one frame");

			// Build -----------------------------------------------------------------------------
			m.def("export_game", [](const std::string& outputDirectory)
			{
				const ExportResult result = Editor().ExportGame(outputDirectory);
				py::dict dict;
				dict["success"] = result.Success;
				dict["executable"] = result.ExecutablePath.string();
				dict["messages"] = result.Messages;
				dict["bytes"] = result.TotalBytes;
				return dict;
			}, py::arg("output_directory"), "Exports a standalone game; returns {success, executable, messages, bytes}");

			// Diagnostics -----------------------------------------------------------------------
			m.def("logs", [](uint64_t since)
			{
				static const char* levels[] = { "trace", "info", "warning", "error", "critical" };
				py::list result;
				for (const LogMessage& message : Log::GetMessagesSince(since))
				{
					py::dict entry;
					entry["sequence"] = message.Sequence;
					entry["level"] = levels[static_cast<int>(message.Level)];
					entry["source"] = message.Source == LogSource::Core ? "engine" : "game";
					entry["text"] = message.Text;
					result.append(entry);
				}
				return result;
			}, py::arg("since") = 0, "Log messages newer than sequence number 'since'");
			m.def("latest_log_sequence", [] { return Log::GetLatestSequence(); });
			m.def("error_count", [] { return Log::GetErrorCount(); }, "Errors logged since startup (engine and game)");
			m.def("script_error_count", [] { return ScriptEngine::GetErrorCount(); }, "Python errors raised by game scripts");
			m.def("frame", [] { return Application::Get().GetFrameCount(); }, "Frames rendered since the editor started");
			m.def("quit", [](int exitCode) { Editor().RequestExit(exitCode); }, py::arg("exit_code") = 0, "Closes the editor");
		}

	}

	void RegisterEditorScriptAPI()
	{
		ScriptEngine::RegisterModuleExtension([](void* modulePointer)
		{
			py::module_ module = py::reinterpret_borrow<py::module_>(static_cast<PyObject*>(modulePointer));
			py::module_ editor = module.def_submodule("editor");
			Define(editor);
			// Make 'import _bellows.editor' work as well as attribute access.
			py::module_::import("sys").attr("modules")["_bellows.editor"] = editor;
		});
	}

}
