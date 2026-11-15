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

## The agent server

When the editor starts it listens on `127.0.0.1:7420` (Settings → Editor → Agent automation, or
`--agent-port N`; `--no-agent-server` turns it off). It only accepts local connections and every
request must carry the session token the editor writes to `AgentSession.json` in its config folder:

* Windows: `%APPDATA%\MageCapBellows\AgentSession.json`
* Linux: `~/.config/MageCapBellows/AgentSession.json`

(`BELLOWS_CONFIG_DIR` overrides the config folder, which the tests use to stay isolated.)

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
| `ping` | – | `{engine, version, project, scene, state}` |
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
| `scene_path()`, `is_dirty()` | Path of the open scene; unsaved changes? |

### Play mode
| Function | Description |
|----------|-------------|
| `play()` / `stop()` | Enter play mode with a copy of the scene / return to the unchanged edited scene. |
| `pause(paused=True)`, `step(frames=1)` | Pause the game; advance a paused game. |
| `state()` | `"edit"`, `"play"` or `"pause"`. |
| `run_frames(frames=1)` | Runs whole editor frames (rendering, game updates, UI). This is how automation lets time pass. |
| `wait_seconds(seconds)` | Runs frames until that much game time has passed (or ~60 frames per second in edit mode). |

While playing, `bellows.*` functions act on the running copy of the scene.

### Editing helpers
| Function | Description |
|----------|-------------|
| `select(entity, additive=False)`, `selection()`, `clear_selection()`, `focus(entity=None)` | Selection and framing in the viewport. |
| `commit(description="Script")` | Records an undo step. Each Console/agent/script command already records one automatically. |
| `undo()`, `redo()`, `can_undo()`, `can_redo()` | Undo history (edit mode). |
| `save_prefab(entity, path)` | Saves an entity and its children as a `.bprefab`. |
| `import_asset(source, folder="")` | Copies an external file into the project; returns its asset path. |
| `reload_asset(path)` | Reloads an asset you changed on disk. |
| `download_polyhaven_hdri(asset_id, resolution="2k", folder="Environments")` | Downloads a CC0 HDRI from [Poly Haven](https://polyhaven.com/hdris) and returns its asset path (assign it to `SkyLight.environment`). |

### Viewing and verifying
| Function | Description |
|----------|-------------|
| `set_camera(position, target)`, `camera_position()`, `camera_target()` | The editor (scene view) camera. |
| `screenshot(path, width=0, height=0)` | Renders the viewport to a PNG: the editor camera in edit mode, the game camera in play mode. |
| `capture_window(path)` | PNG of the whole editor window including the UI (runs one frame). |
| `logs(since=0)`, `latest_log_sequence()`, `error_count()`, `script_error_count()` | Console messages and error counters. |
| `export_game(output_directory)` | Builds a standalone game; returns `{success, executable, messages, bytes}`. |
| `frame()`, `quit(exit_code=0)` | Frame counter; close the editor. |

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
