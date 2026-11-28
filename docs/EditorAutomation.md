# Editor automation

Everything you can do in the MageCap Bellows editor can also be done from Python. This is how AI
agents (and build scripts) create projects, build scenes, play-test and export games.

There are three ways in:

| Way | Use it for |
|-----|-----------|
| **Console** panel (`>>>` prompt) | Trying things interactively. |
| `BellowsEditor --run-script file.py [--exit]` | Repeatable automation, e.g. `Tools/BuildFeatureTest.py`. Add `--headless` to run without a window. |
| **Agent server** + `Tools/bellows_cli.py` | Controlling an editor that is already open (this is what AI agents use). |

All three run the code on the editor's main thread between frames, with `bellows` (the game
API, see [ScriptingAPI.md](ScriptingAPI.md)) and `bellows.editor` available. In the Console and
through the agent server, a trailing expression prints its value, like the Python prompt.
Console and agent-server code that runs 10 seconds without returning is stopped with
`KeyboardInterrupt` (see "Time limit" in [ScriptingAPI.md](ScriptingAPI.md)); each `run_frames` frame
restarts that clock, and `--run-script` has no limit. A loop that catches everything (`except:`) is
stopped again on every line it runs.

On Windows the editor (like a game) is a GUI program, so it never opens a console window of its own.
Its output still goes where it's sent: a pipe or a file (`> log.txt`), or the command prompt it was
started from. `--console` opens a console window for the log when there is nowhere else to show it.

## For other tools: checking scripts and finding the editor

**Checking a script** without opening the editor, exactly as the Script Editor does (ForgeScript
with the engine's functions and phrases known, Python with the bundled interpreter):

```
BellowsEditor --check-script Scripts/Player.forge          # on stderr: file:line:column: message (or: warning: message)
BellowsEditor --check-script Scripts/Player.forge --json   # {"file", "diagnostics": [{line, column, endColumn, severity, message, fix?}]}
BellowsEditor --check-script Scripts/Player.forge --stdin  # reads the (unsaved) text from standard input
```

A ForgeScript mistake or warning whose message names exactly what to write has a `"fix"`, the same
as `forgescript --check --json`'s: `{"title", "line", "column", "endLine", "endColumn", "newText"}`,
which replaces `line:column` up to `endLine:endColumn` (1-based bytes, the end not included; equal
ends insert) with `newText`. Python's diagnostics have none.

`use "Scripts/Helpers"` lines are read from the project's `Assets` folder, as in the game: the
project is the nearest folder above the script that contains a `.bproject` file (outside a project,
the script's own folder is used).

A ForgeScript script with no mistakes gets its **warnings** instead (things allowed but probably
not meant, like a value named `max` in a script that calls `max(...)`): `warning: ` before the
message, `"severity": "warning"` in JSON. They never fail the check.

Exit codes: `0` no mistakes (warnings or none), `65` mistakes found, `66` the file could not be
read, `64` bad usage, `70` Python could not start. The text format, warnings and exit codes match
`forgescript --check` with `forgescript-api.json` beside it.

**Finding the editor.** Every time the editor starts it writes `Install.json` to its
configuration folder (Windows: `%APPDATA%\MageCapBellows`, Linux: `~/.config/MageCapBellows`):
`Engine`, `EngineVersion`, `ForgeScriptVersion`, `EditorPath`, `RuntimePath`, `ForgeScriptPath`
(empty if the `forgescript` tool is missing) and `DocsDirectory` (the folder with
`ScriptingAPI.md` and `ForgeScript/`).

## The agent server

When the editor starts it listens on `127.0.0.1:7420` (Settings → Editor → Agent automation, or
`--agent-port N`; `--no-agent-server` turns it off). It only accepts local connections and every
request must carry the session token the editor writes to `AgentSession.json` in its config folder:

* Windows: `%APPDATA%\MageCapBellows\AgentSession.json`
* Linux: `~/.config/MageCapBellows/AgentSession.json`

(`BELLOWS_CONFIG_DIR` overrides the config folder, which the tests use to stay isolated.)

The token is 64 hex digits from the system's secure random source, new every time the editor
starts; on Linux the session file is readable only by you (mode 0600). A connection whose first line
is an HTTP request line (`POST / HTTP/1.1`, ...) is closed without an answer, so a web page in a
browser can't send the editor commands. Up to 8 clients can be connected at once, each served on
its own; a request line has 30 seconds to arrive whole.

### Command-line client

```bash
python Tools/bellows_cli.py ping                              # editor state
python Tools/bellows_cli.py exec "bellows.create_entity('Player')"
python Tools/bellows_cli.py exec-file Tools/BuildFeatureTest.py
python Tools/bellows_cli.py screenshot shot.png --width 1280 --height 720
python Tools/bellows_cli.py window-screenshot editor.png      # the whole editor UI
python Tools/bellows_cli.py logs --since 0
python Tools/bellows_cli.py wait 60                           # wait for 60 frames
```

`exec` exits with code 1 if the Python code raised; the traceback is printed.

### Protocol

One JSON object per line over TCP, one response line per request:

```json
{"id": 1, "token": "<token>", "method": "exec", "params": {"code": "bellows.editor.state()"}}
{"id": 1, "ok": true, "result": {"success": true, "output": "'edit'\n"}}
```

| Method | Params | Result |
|--------|--------|--------|
| `ping` | – | `{engine, version, commit, project, scene, state}` |
| `exec` | `code` | `{success, output}` (stdout, stderr and the value of a trailing expression) |
| `logs` | `since` (sequence number) | `[{sequence, level, source, text}]` |
| `wait_frames` | `frames` | `true` after that many frames |

Errors in the request itself (bad token, unknown method) return `{"ok": false, "error": "..."}`.

## `bellows.editor` reference

### Projects and scenes
| Function | Description |
|----------|-------------|
| `new_project(directory, name)` | Creates a project folder (Assets/Scenes, Scripts, Materials, …, a default scene) and opens it. |
| `open_project(path)` | Opens a `.bproject` file or a folder containing one. |
| `project_name()`, `project_directory()`, `asset_directory()` | Information about the open project. |
| `new_scene(name="Untitled")` | Replaces the open scene with a new one (camera, sun, sky). |
| `open_scene(path)` | Opens a scene by asset path. |
| `save_scene(path=None)` | Saves (to `path` if given, e.g. `"Scenes/Level2.bscene"`). Returns success. |
| `bake_navigation()` | Bakes the edited scene's navmesh from its static colliders (Scene Settings → Navigation) and saves it beside the scene as `<scene>.bnav`. Returns `{"tiles": n, "polygons": n}`; raises `RuntimeError` with the reason when the scene isn't saved yet or has nothing to walk on. |
| `scene_path()`, `is_dirty()` | Path of the open scene; unsaved changes? |

### Play mode
| Function | Description |
|----------|-------------|
| `play()` / `stop()` | Enter play mode in the editor viewport with a copy of the scene / return to the unchanged edited scene. |
| `play_in_window(scene="open", headless=False, frames=0)` | Play in a game window: runs the game as `BellowsRuntime` in its own window and process, like an exported game. `scene` is `"open"` (the open scene, unsaved changes included) or `"project"` (the project's start scene). `headless` runs without a window at a fixed time step; `frames` ends the game after that many frames. Returns whether it started. The game's log lines arrive in the Console (`logs`) marked `[Game]`; the editor stays in edit mode meanwhile. |
| `is_game_running()`, `stop_game()`, `game_exit_code()` | Whether the game window still runs; end it; its exit code once it has ended (`None` before). |
| `pause(paused=True)`, `step(frames=1)` | Pause the game; advance a paused game. |
| `state()` | `"edit"`, `"play"` or `"pause"`. |
| `run_frames(frames=1)` | Runs whole editor frames (rendering, game updates, UI). This is how automation lets time pass. |
| `wait_seconds(seconds)` | Runs frames until that much game time has passed (or ~60 frames per second in edit mode). |

While playing, `bellows.*` functions act on the running copy of the scene.

### ForgeScript debugger
| Function | Description |
|----------|-------------|
| `set_breakpoints(script, lines)`, `breakpoints(script)`, `clear_breakpoints()` | Breakpoint lines (1-based) of a ForgeScript asset such as `"Scripts/Player.forge"`; `[]` removes them. They last for the editor session. |
| `set_breakpoint(script, line, condition="", log_message="")` | Adds a breakpoint, or changes the one at that line. With a `condition` it stops only when that is true there (`"hp < 10"`); with a `log_message` it prints that to the Console instead of stopping (`"hp is {hp}"`, `{...}` worked out as in ForgeScript text); with both it prints when the condition is true. Empty ones make it a plain breakpoint. `ValueError` for a line below 1. `set_breakpoints` keeps the details of lines that stay. |
| `breakpoint_details(script)` | `[{"line", "condition", "log_message"}]`, sorted by line (`""` when not set). |
| `debug_state()` | `{"stopped", "file", "line", "reason", "message", "stack", "variables", "script_variables"}`: where a stopped script is (`reason` is `"breakpoint"`, `"step"` or `"pause"`), its calls innermost first (`function`, `file`, `line`), and the variables (`name`, `type`, `value`) of the innermost call and of its script. `message` says why a breakpoint's condition or log message couldn't be worked out; such a breakpoint stops, so a typo is never skipped silently (`""` otherwise). |
| `debug_evaluate(expression, frame=0)` | Works out one line of ForgeScript in a call of the stopped script (`0` is the innermost): `{"type", "value"}`. It reads variables and engine values (`my position`) but never calls a function, so it can't change the game. `ValueError`, with the reason, when nothing is stopped or the line can't be worked out. |
| `debug_continue()`, `debug_step_over()`, `debug_step_in()`, `debug_step_out()` | Let the stopped script go on; the game runs again unless the script stops again. `False` when nothing is stopped. |
| `debug_pause()` | Stops at the next ForgeScript line that runs (play mode). |

A stop pauses the game (`state()` is `"pause"`) until the script goes on; `pause(False)` continues it,
and `stop()` ends play even while a script is held. Run frames after `play()` for a script to reach a
breakpoint:

```python
editor.set_breakpoints("Scripts/Player.forge", [12])
editor.play()
editor.run_frames(2)
state = editor.debug_state()
if state["stopped"]:
    print(state["line"], state["variables"])
    print(editor.debug_evaluate("health * 2"))
    editor.debug_step_over()

# Stop only when health is low, and log every hit without stopping:
editor.set_breakpoint("Scripts/Player.forge", 12, condition="health < 10")
editor.set_breakpoint("Scripts/Player.forge", 20, log_message="hit for {amount}")
```

### Editing helpers
| Function | Description |
|----------|-------------|
| `select(entity, additive=False)`, `selection()`, `clear_selection()`, `focus(entity=None)` | Selection and framing in the viewport. |
| `commit(description="Script")` | Records an undo step. Each Console/agent/script command already records one automatically. |
| `undo()`, `redo()`, `can_undo()`, `can_redo()` | Undo history (edit mode). |
| `save_prefab(entity, path)` | Saves an entity and its children as a `.bprefab`. |
| `import_asset(source, folder="")` | Copies an external file into the project; returns its asset path. |
| `reload_asset(path)` | Reloads an asset you changed on disk. |
| `model_front(path)`, `set_model_front(path, front)` | Which way a model's front faces in its file: `"-Z"` (as made, Bellows' forward) or `"+Z"` (Blender's and glTF's convention; the model is turned half a turn about its up axis, around its own origin, at import). Saved in `<model>.bimport` beside the model, and placed copies reload at once. Models without the file use the project's **Model Front Faces** (Project Settings): `+Z` in new projects, `-Z` in projects made before 0.5.0. Raises `ValueError` for a path that isn't a model, or a front that isn't `"-Z"` or `"+Z"`. |
| `download_polyhaven_hdri(asset_id, resolution="2k", folder="Environments")` | Downloads a CC0 HDRI from [Poly Haven](https://polyhaven.com/hdris) and returns its asset path (assign it to `SkyLight.environment`). |

### Viewing and verifying
| Function | Description |
|----------|-------------|
| `set_camera(position, target)`, `camera_position()`, `camera_target()` | The editor (scene view) camera. |
| `screenshot(path, width=0, height=0)` | Renders the viewport to a PNG: the editor camera in edit mode, the game camera in play mode. |
| `capture_window(path)` | PNG of the whole editor window including the UI (runs one frame). |
| `create_ui(kind, parent=None)` | Makes what **Create → UI** makes, selects it and returns its top entity. Widgets (`canvas`, `panel`, `text`, `button`, `toggle`, `slider`, `dropdown`, `text_input`, `vertical_layout`, `scroll_list`) go into the canvas `parent` is in, or a new canvas; screens (`main_menu`: title, Play/Settings/Quit buttons in a layout, version line; `hud`: health bar whose `Fill` anchor max x is the amount left, score, message line) get their own canvas. Edit mode only. |
| `ui_templates()` | The names `create_ui` takes. |
| `set_ui_anchors(entity, preset, move=False)` | The Inspector's anchor presets (`top_left` ... `bottom_right`, `top_wide`, `middle_wide`, `bottom_wide`, `left_tall`, `center_tall`, `right_tall`, `full_rect`): the rect keeps its place on screen (as of the last frame), or with `move=True` goes to the preset. |
| `set_ui_view(enabled=True, width=0, height=0)` | The UI view: in edit mode the viewport, and `screenshot()` without a size, show the game's screen at `width` x `height` (64 to 8192; 0 keeps the size) through the game camera, for laying out UI. Saved with the editor settings. |
| `ui_view()` | `{"enabled", "width", "height"}`. |
| `hierarchy_rows()` | The Hierarchy panel's rows as it last drew them, top to bottom: `[{"kind", "name", "depth", "group"}]`. `kind` is `"group"` (a group's header; `name` is the group's), `"note"` (its note, under an open group; `name` is the note), `"entity"`, `"ungrouped"` (the line above the top-level entities in no group, shown when the scene has groups), `"section"` (`"PINNED"` and `"SCENE"`, when something is pinned), `"heading"` (a kind or a tag in the By type and By tag views, with `count`) or `"stack"` (three or more siblings in a row with one numbered name, like `Crate 1` to `Crate 4`, folded into one row; `name` is the shared name, with `count`). `group` is the group a row is under, or `None`; a group's entities are one level deeper than its header. Closed groups, headings, stacks and nodes hide their rows, and a search shows only matches. Run a frame after a change. |
| `set_hierarchy_view(view)`, `hierarchy_view()` | The Hierarchy's view: `"groups"` (groups and the hierarchy), `"type"` (every entity under its kind: Scripted, Lights, Cameras, UI, Audio, Physics, Meshes, Other) or `"tag"` (under each of its tags, then No tags). |
| `set_hierarchy_search(text)` | Types into the Hierarchy's search: words match names; `tag:enemy`, `group:docking` and `has:` (`script`, `light`, `camera`, `physics`, `audio`, `mesh`, `ui`, `animation`, `nav`, `particles`, `decal`, or `tag` for any tag) narrow it. |
| `organize_scene(apply=False)` | The moves the Hierarchy's **Organize** suggests for top-level entities in no group: lights and skies to Lighting, cameras to Cameras, UI canvases to UI, sound sources that draw nothing to Audio (scripted entities stay put): `[{"entity", "group", "new_group"}]`. With `apply=True` it makes them, and the groups they need, as one undo step. |
| `set_pinned(entity, pinned=True)`, `pinned(entity)` | Pins an entity to the top of the Hierarchy (saved with the scene). |
| `search(query="", limit=20)` | What the title bar's search (Ctrl+K) lists for `query`, best first: `[{"kind", "name", "detail", "enabled"}]`. `kind` is `"command"`, `"entity"`, `"scene"` or `"script"`; `detail` is a shortcut, a parent or group, or a folder. An empty query lists the commands. Letters may be scattered (`"sv sc"` finds Save Scene). |
| `run_command(name)` | Runs a search command by its exact name (`"Save Scene"`, `"Undo"`, `"Show Grid"`), as picking it would; `ValueError` for an unknown name or one that can't run now. |
| `open_search(query="")`, `close_search()`, `is_search_open()` | Opens the title bar's search with `query` typed in it (for screenshots), closes it, or says whether it's open. |
| `logs(since=0)`, `latest_log_sequence()`, `error_count()`, `script_error_count()` | Console messages and error counters. |
| `export_game(output_directory)` | Builds a standalone game; returns `{success, executable, messages, bytes}`. |
| `frame()`, `quit(exit_code=0)` | Frame counter; close the editor. |

### Profiler
The CPU profiler behind **View → Profiler**. It keeps the last 300 frames the editor ran (play in
the viewport included; a game window is a separate program and isn't timed). Times are
milliseconds; a scope's `start_ms` counts from the start of its frame, and `depth` is 0 for a scope
directly inside the frame.

| Function | Description |
|----------|-------------|
| `profiler_frames(count=1)` | The newest frames, at most `count`, newest last: `[{"index", "cpu_ms", "scopes": [{"name", "depth", "start_ms", "ms"}], "gpu_timed", "gpu_ms", "gpu_scopes": [{"name", "depth", "ms"}]}]`, scopes in the order they began. GPU times arrive a few frames after their frame (`gpu_timed` turns true); `gpu_ms` adds up the depth-0 GPU scopes. |
| `profiler_summary(frames=120, gpu=False)` | Each scope name over the newest `frames` frames, averaged per frame: `[{"name", "depth", "total_ms", "self_ms", "max_ms", "calls"}]`. `total_ms` includes the scopes inside it, `self_ms` leaves them out, and `max_ms` is the slowest single frame. With `gpu=True`, the GPU scopes, over the frames whose GPU times have arrived. |
| `gpu_timing_supported()` | Whether the graphics driver can time GPU work; without it no frame gets GPU times. |
| `set_profiler_paused(paused)`, `profiler_paused()` | While paused, frames still run but aren't added to the history. |
| `clear_profiler()` | Forgets every recorded frame. |

Scope names include `Events`, the layer names (`EditorLayer`), `Scene Update` with `Game UI`,
`Animation`, `Tweens`, `Scripts` (and each Python script's path inside it, such as
`Scripts/Mover.py`, then `ForgeScript`), `Navigation`, `Physics` (`Fixed Update Scripts`,
`Physics Step`, `Contact Scripts`) and `Audio`, then `Scene Render`, `Render Passes`, `Editor UI`,
`Acquire Frame` and `Present`. A scope runs only when its part does: `Animation` is missing while
the game is paused, and `Physics` can run several times in one frame or not at all.

GPU scope names: `Viewport` (the editor viewport's renderer; `Scene` in a game) with its passes
`Uploads`, `Shadows`, `Depth Prepass`, `Ambient Occlusion`, `Opaque`, `Sky`, `Transparent`,
`Bloom`, `Tone Mapping`, `Text`, `Overlays`, `Game UI` and `Selection Outline` inside it, then
`Editor UI`.

## Recipe: an agent builds and verifies a game

```python
import bellows
from bellows import Vec3, editor

editor.new_project("C:/Games/Blocks", "Blocks")

ground = bellows.create_entity("Ground")
ground.add_component(bellows.MeshRenderer).mesh = "Builtin/Plane"
ground.scale = Vec3(20.0, 1.0, 20.0)
ground.add_component(bellows.RigidBody).type = "Static"
ground.add_component(bellows.BoxCollider).half_extents = Vec3(0.5, 0.01, 0.5)

block = bellows.create_entity("Block")
block.add_component(bellows.MeshRenderer).mesh = "Builtin/Cube"
block.position = Vec3(0.0, 5.0, 0.0)
block.add_component(bellows.RigidBody)        # Dynamic by default
block.add_component(bellows.BoxCollider)

editor.save_scene("Scenes/Main.bscene")

editor.play()
editor.run_frames(120)                         # two seconds of game time at 60 FPS
assert bellows.find_entity("Block").position.y < 1.0, "the block should have landed"
editor.screenshot("C:/Games/Blocks/check.png") # look at the result
editor.stop()
assert editor.script_error_count() == 0
```

Game logic goes into `.py` files in the project's `Scripts` folder (write them with normal file
I/O, then attach them with `entity.add_component(bellows.ScriptComponent).path = "Scripts/X.py"`).
The script reloads the next time play starts.

Tips for agents:
* Verify visually: take a `screenshot` after building and look at it.
* Verify behavior: `play()`, `run_frames(n)`, then inspect entity state, `logs()` and
  `script_error_count()`, then `stop()`.
* Keep scenes reproducible by building them from a script you keep in the project (see
  `Tools/BuildFeatureTest.py`).
