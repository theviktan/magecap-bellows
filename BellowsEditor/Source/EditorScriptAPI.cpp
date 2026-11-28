#include "EditorScriptAPI.h"

#include "EditorLayer.h"
#include "HierarchyLogic.h"
#include "Panels/SceneHierarchyPanel.h"
#include "UITemplates.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Core/Profiler.h"
#include "Bellows/Renderer/GpuTimer.h"
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
			m.def("bake_navigation", []()
			{
				EditorLayer& editor = Editor();
				std::string error;
				if (!editor.BakeNavigation(error))
					throw std::runtime_error(error);
				const NavMesh& navMesh = *editor.GetEditorScene()->GetNavMesh();
				py::dict result;
				result["tiles"] = navMesh.GetTileCount();
				result["polygons"] = navMesh.GetPolygonCount();
				return result;
			}, "Bakes the edited scene's navmesh from its static colliders and saves it beside the scene; returns "
				"{'tiles': n, 'polygons': n}. Raises RuntimeError with the reason when nothing can be baked or the scene isn't saved");
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
			m.def("play_in_window", [](const std::string& scene, bool headless, uint64_t frames)
			{
				if (scene != "open" && scene != "project")
					throw py::value_error("scene must be 'open' (the open scene) or 'project' (the project's start scene)");
				return Editor().PlayInGameWindow(scene == "project", headless, frames);
			}, py::arg("scene") = "open", py::arg("headless") = false, py::arg("frames") = 0,
				"Runs the game in its own window and process (Play in a game window); returns whether it started");
			m.def("is_game_running", [] { return Editor().IsGameWindowRunning(); }, "True while a game window started by the editor runs");
			m.def("stop_game", [] { Editor().StopGameWindow(); }, "Ends the game window");
			m.def("game_exit_code", []() -> py::object
			{
				const std::optional<int> code = Editor().GetGameWindowExitCode();
				return code ? py::object(py::int_(*code)) : py::object(py::none());
			}, "The last game window's exit code once it has ended, else None");
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
			m.def("set_breakpoint", [](const std::string& script, int line, const std::string& condition, const std::string& logMessage)
			{
				if (!Editor().GetScriptDebugger().SetBreakpoint(script, line, condition, logMessage))
					throw py::value_error("A breakpoint needs a ForgeScript asset path and a line of 1 or more");
			}, py::arg("script"), py::arg("line"), py::arg("condition") = "", py::arg("log_message") = "",
				"Adds a breakpoint at a line (1-based), or changes the one there. With a condition it stops only when that is "
				"true ('hp < 10'); with a log message it prints that instead of stopping ('hp is {hp}'); with both it prints when the condition is true");
			m.def("breakpoint_details", [](const std::string& script)
			{
				py::list result;
				for (const ForgeScript::Breakpoint& breakpoint : Editor().GetScriptDebugger().GetDetailedBreakpoints(script))
				{
					py::dict entry;
					entry["line"] = breakpoint.Line;
					entry["condition"] = breakpoint.Condition;
					entry["log_message"] = breakpoint.LogMessage;
					result.append(entry);
				}
				return result;
			}, py::arg("script"), "A ForgeScript asset's breakpoints, sorted by line: [{'line', 'condition', 'log_message'}] ('' when not set)");
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
					state["message"] = stop.Message;
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
			}, "Where a stopped ForgeScript is: {'stopped', 'file', 'line', 'reason', 'message', 'stack', 'variables', 'script_variables'}. "
				"'message' says why a breakpoint's condition or log message couldn't be worked out ('' otherwise)");
			m.def("debug_evaluate", [](const std::string& expression, size_t frame)
			{
				ForgeScript::DebugVariable value;
				std::string error;
				if (!Editor().GetScriptDebugger().Evaluate(frame, expression, value, error))
					throw py::value_error(error);
				py::dict result;
				result["type"] = value.Type;
				result["value"] = value.Text;
				return result;
			}, py::arg("expression"), py::arg("frame") = 0,
				"Works out one line of ForgeScript in a call of the stopped script (0 = innermost): {'type', 'value'}. It reads variables and "
				"engine values but never calls a function, so it can't change the game. ValueError when nothing is stopped or it can't be worked out");
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
			m.def("model_front", [](const std::string& path)
			{
				if (AssetManager::GetAssetType(path) != AssetType::Mesh || AssetManager::IsBuiltinPath(path))
					throw py::value_error("'" + path + "' is not a model file (.glb or .gltf)");
				return std::string(ModelFrontToString(AssetManager::GetMeshImportSettings(path).Front));
			}, py::arg("path"), "Which way the model's front faces in its file: \"-Z\" (as made) or \"+Z\" (turned to face forward)");
			m.def("set_model_front", [](const std::string& path, const std::string& front)
			{
				const std::optional<ModelFront> parsed = ModelFrontFromString(front);
				if (!parsed)
					throw py::value_error("front must be \"-Z\" or \"+Z\", not \"" + front + "\"");
				if (!Editor().SetModelFront(path, *parsed))
					throw py::value_error("'" + path + "' is not a model file in this project, or its settings could not be saved");
			}, py::arg("path"), py::arg("front"),
				"Saves the model's front (\"-Z\" or \"+Z\") in '<model>.bimport' and reloads it where it is placed");

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
			m.def("create_ui", [](const std::string& kind, const std::optional<ScriptEntity>& parent)
			{
				const std::optional<UITemplate> found = UITemplateFromName(kind);
				if (!found)
				{
					std::string names;
					for (int i = 0; i < UITemplateCount; i++)
						names += (i ? ", " : "") + UITemplateScriptName(static_cast<UITemplate>(i));
					throw py::value_error("Unknown UI template '" + kind + "'. Use one of: " + names);
				}
				EditorLayer& editor = Editor();
				if (editor.GetSceneState() != SceneState::Edit)
					throw std::runtime_error("UI templates are made in edit mode; stop playing first");
				Entity parentEntity = parent ? ResolveInActiveScene(*parent) : Entity();
				Entity created = CreateUITemplate(*editor.GetActiveScene(), parentEntity, *found);
				editor.Select(created);
				return ScriptEntity(created);
			}, py::arg("kind"), py::arg("parent") = py::none(),
				"Makes what Create > UI makes and selects it: a widget ('button', 'text_input', ...) inside the "
				"canvas 'parent' is in (or a new canvas), or a whole screen ('main_menu', 'hud') with its own "
				"canvas. Returns its top entity");
			m.def("ui_templates", []
			{
				std::vector<std::string> names;
				for (int i = 0; i < UITemplateCount; i++)
					names.push_back(UITemplateScriptName(static_cast<UITemplate>(i)));
				return names;
			}, "The names create_ui() takes");
			m.def("set_ui_anchors", [](const ScriptEntity& handle, const std::string& preset, bool move)
			{
				const std::optional<UIAnchorPreset> found = UIAnchorPresetFromName(preset);
				if (!found)
				{
					std::string names;
					for (int i = 0; i < UIAnchorPresetCount; i++)
						names += (i ? ", " : "") + UIAnchorPresetScriptName(static_cast<UIAnchorPreset>(i));
					throw py::value_error("Unknown anchor preset '" + preset + "'. Use one of: " + names);
				}
				if (!ApplyUIAnchorPreset(ResolveInActiveScene(handle), *found, move))
					throw py::value_error("The entity has no UI Rect");
			}, py::arg("entity"), py::arg("preset"), py::arg("move") = false,
				"The Inspector's anchor presets ('top_left', 'center', 'bottom_wide', 'full_rect', ...): the rect "
				"keeps its place on screen, or with move=True goes to the preset");
			m.def("set_ui_view", [](bool enabled, uint32_t width, uint32_t height)
			{
				if ((width == 0) != (height == 0))
					throw py::value_error("Give both the width and the height of the UI view, or neither");
				if (width != 0 && (width < 64 || width > 8192 || height < 64 || height > 8192))
					throw py::value_error("The UI view's width and height go from 64 to 8192 pixels");
				EditorSettings& settings = Editor().GetSettings();
				settings.UIView = enabled;
				if (width != 0)
				{
					settings.UIViewWidth = width;
					settings.UIViewHeight = height;
				}
			}, py::arg("enabled") = true, py::arg("width") = 0, py::arg("height") = 0,
				"Turns the UI view on or off: in edit mode the viewport (and screenshot()) shows the game's screen at "
				"width x height through the game camera, for laying out UI. A size of 0 keeps the current one");
			m.def("ui_view", []
			{
				const EditorSettings& settings = Editor().GetSettings();
				py::dict result;
				result["enabled"] = settings.UIView;
				result["width"] = settings.UIViewWidth;
				result["height"] = settings.UIViewHeight;
				return result;
			}, "The UI view's state: {'enabled': bool, 'width': int, 'height': int}");
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

			// Profiler ----------------------------------------------------------------------------
			m.def("hierarchy_rows", []
			{
				py::list result;
				Scene& scene = *Editor().GetActiveScene();
				for (const SceneHierarchyPanel::Row& row : Editor().GetHierarchyPanel().GetRows())
				{
					py::dict entry;
					const SceneGroup* group = scene.FindGroup(row.Group);
					entry["group"] = group ? py::cast(group->Name) : py::none();
					entry["depth"] = row.Depth;
					switch (row.Kind)
					{
						case SceneHierarchyPanel::RowKind::Entity:
							if (!row.Target)
								continue;
							entry["kind"] = "entity";
							entry["name"] = row.Target.GetName();
							break;
						case SceneHierarchyPanel::RowKind::Group:
							entry["kind"] = "group";
							entry["name"] = group ? group->Name : std::string();
							break;
						case SceneHierarchyPanel::RowKind::Note:
							entry["kind"] = "note";
							entry["name"] = group ? group->Note : std::string();
							break;
						case SceneHierarchyPanel::RowKind::Ungrouped:
							entry["kind"] = "ungrouped";
							entry["name"] = "";
							break;
						case SceneHierarchyPanel::RowKind::Section:
							entry["kind"] = "section";
							entry["name"] = row.Label;
							break;
						case SceneHierarchyPanel::RowKind::Heading:
							entry["kind"] = "heading";
							entry["name"] = row.Label.substr(row.Label.find(':') + 1);
							entry["count"] = row.Count;
							break;
						case SceneHierarchyPanel::RowKind::Stack:
							entry["kind"] = "stack";
							entry["name"] = row.Label;
							entry["count"] = row.Count;
							break;
					}
					result.append(entry);
				}
				return result;
			}, "The Hierarchy panel's rows as it last drew them, top to bottom: {kind, name, depth, group}");
			m.def("search", [](const std::string& query, int limit)
			{
				if (limit < 1)
					throw py::value_error("search's limit is 1 or more");
				const std::vector<EditorLayer::SearchEntry> entries = Editor().CollectSearchEntries();
				std::vector<CommandSearch::Item> items;
				for (const EditorLayer::SearchEntry& entry : entries)
					items.push_back(entry.Item);
				py::list result;
				for (size_t index : CommandSearch::Rank(items, query, static_cast<size_t>(limit)))
				{
					py::dict row;
					row["kind"] = CommandSearch::KindName(items[index].Kind);
					row["name"] = items[index].Name;
					row["detail"] = items[index].Detail;
					row["enabled"] = items[index].Enabled;
					result.append(row);
				}
				return result;
			}, py::arg("query") = "", py::arg("limit") = 20,
				"What the title bar's search (Ctrl+K) lists for 'query', best first: {kind, name, detail, enabled}. "
				"kind is 'command', 'entity', 'scene' or 'script'; an empty query lists the commands");
			m.def("run_command", [](const std::string& name)
			{
				for (const EditorLayer::SearchEntry& entry : Editor().CollectSearchEntries())
				{
					if (entry.Item.Kind != CommandSearch::ResultKind::Command || entry.Item.Name != name)
						continue;
					if (!entry.Item.Enabled)
						throw py::value_error("'" + name + "' can't run right now (the search lists it dimmed)");
					entry.Run();
					return;
				}
				throw py::value_error("No command is called '" + name + "'. editor.search() lists them");
			}, py::arg("name"), "Runs a command from the title bar's search by its exact name, as picking it there would");
			m.def("open_search", [](const std::string& query) { Editor().OpenSearch(query); }, py::arg("query") = "",
				"Opens the title bar's search (as Ctrl+K does), with 'query' typed in it");
			m.def("close_search", [] { Editor().CloseSearch(); }, "Closes the title bar's search");
			m.def("is_search_open", [] { return Editor().IsSearchOpen(); }, "Whether the title bar's search is open");
			m.def("set_hierarchy_view", [](const std::string& view)
			{
				using View = SceneHierarchyPanel::View;
				if (view == "groups") Editor().GetHierarchyPanel().SetView(View::Groups);
				else if (view == "type") Editor().GetHierarchyPanel().SetView(View::Type);
				else if (view == "tag") Editor().GetHierarchyPanel().SetView(View::Tag);
				else throw py::value_error("Unknown Hierarchy view '" + view + "'. Views: groups, type, tag");
			}, py::arg("view"), "Shows the Hierarchy by groups and hierarchy (\"groups\"), by kind (\"type\") or by tag (\"tag\")");
			m.def("hierarchy_view", []
			{
				switch (Editor().GetHierarchyPanel().GetView())
				{
					case SceneHierarchyPanel::View::Type:	return std::string("type");
					case SceneHierarchyPanel::View::Tag:	return std::string("tag");
					case SceneHierarchyPanel::View::Groups:	break;
				}
				return std::string("groups");
			});
			m.def("set_hierarchy_search", [](const std::string& text) { Editor().GetHierarchyPanel().SetSearch(text); }, py::arg("text"),
				"Types into the Hierarchy's search box (tag:, has: and group: work as there)");
			m.def("organize_scene", [](bool apply)
			{
				Scene& scene = *Editor().GetActiveScene();
				const std::vector<HierarchyLogic::OrganizeMove> moves = HierarchyLogic::SuggestOrganize(scene);
				py::list result;
				for (const HierarchyLogic::OrganizeMove& move : moves)
				{
					py::dict entry;
					entry["entity"] = ScriptEntity(scene.TryGetEntityByUUID(move.Entity));
					entry["group"] = move.Group;
					entry["new_group"] = move.NewGroup;
					result.append(entry);
				}
				if (apply)
				{
					if (Editor().GetSceneState() != SceneState::Edit)
						throw std::runtime_error("Stop the game to organize the scene");
					if (HierarchyLogic::ApplyOrganize(scene, moves) > 0)
						Editor().CommitChange("Organize scene");
				}
				return result;
			}, py::arg("apply") = false, "The moves the Hierarchy's Organize suggests: [{entity, group, new_group}]; with apply=True, makes them (one undo step)");
			m.def("set_pinned", [](const ScriptEntity& entity, bool pinned) { entity.Resolve().GetComponent<TagComponent>().Pinned = pinned; },
				py::arg("entity"), py::arg("pinned") = true, "Pins an entity to the top of the Hierarchy, or unpins it (saved with the scene)");
			m.def("pinned", [](const ScriptEntity& entity) { return entity.Resolve().GetComponent<TagComponent>().Pinned; }, py::arg("entity"));
			m.def("profiler_frames", [](size_t count)
			{
				py::list result;
				for (const ProfileFrame& frame : Profiler::GetRecentFrames(count))
				{
					py::list scopes;
					for (const ProfileSample& sample : frame.Samples)
					{
						py::dict scope;
						scope["name"] = sample.Name;
						scope["depth"] = sample.Depth;
						scope["start_ms"] = sample.StartMs;
						scope["ms"] = sample.DurationMs;
						scopes.append(scope);
					}
					py::list gpuScopes;
					for (const ProfileSample& sample : frame.GpuSamples)
					{
						py::dict scope;
						scope["name"] = sample.Name;
						scope["depth"] = sample.Depth;
						scope["ms"] = sample.DurationMs;
						gpuScopes.append(scope);
					}
					py::dict entry;
					entry["index"] = frame.Index;
					entry["cpu_ms"] = frame.CpuMs;
					entry["scopes"] = scopes;
					entry["gpu_timed"] = frame.GpuTimed;
					entry["gpu_ms"] = frame.GpuMs;
					entry["gpu_scopes"] = gpuScopes;
					result.append(entry);
				}
				return result;
			}, py::arg("count") = 1,
				"The newest recorded frames (at most 'count', newest last): {index, cpu_ms, scopes: [{name, depth, start_ms, ms}], "
				"gpu_timed, gpu_ms, gpu_scopes: [{name, depth, ms}]}");
			m.def("profiler_summary", [](size_t frames, bool gpu)
			{
				py::list result;
				const std::vector<ProfileFrame> recent = Profiler::GetRecentFrames(frames);
				for (const ProfileSummary& summary : gpu ? Profiler::SummarizeGpu(recent) : Profiler::Summarize(recent))
				{
					py::dict entry;
					entry["name"] = summary.Name;
					entry["depth"] = summary.Depth;
					entry["total_ms"] = summary.AverageMs;
					entry["self_ms"] = summary.AverageSelfMs;
					entry["max_ms"] = summary.MaxMs;
					entry["calls"] = summary.CallsPerFrame;
					result.append(entry);
				}
				return result;
			}, py::arg("frames") = 120, py::arg("gpu") = false,
				"Per-scope averages over the newest frames, of the CPU's scopes or (gpu=True) the GPU's: [{name, depth, total_ms, self_ms, max_ms, calls}]");
			m.def("gpu_timing_supported", [] { return GpuTimer::IsSupported(); }, "Whether the graphics driver can time GPU work");
			m.def("set_profiler_paused", [](bool paused) { Profiler::SetPaused(paused); }, py::arg("paused"),
				"While paused, frames are not added to the profiler's history");
			m.def("profiler_paused", [] { return Profiler::IsPaused(); });
			m.def("clear_profiler", [] { Profiler::Clear(); }, "Forgets every recorded frame");
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
