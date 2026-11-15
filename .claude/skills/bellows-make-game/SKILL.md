---
name: bellows-make-game
description: Build a game (e.g. "make me a Tetris clone") in a running MageCap Bellows editor through its Python automation API and agent server, then play-test and verify it with screenshots. Use whenever the user asks for a game, level or prototype made with Bellows.
---

# Make a game with the MageCap Bellows editor

The editor exposes everything to Python (`bellows` game API + `bellows.editor`). Read
`docs/ScriptingAPI.md` and `docs/EditorAutomation.md` once; they are the source of truth.

## 1. Connect

* If the user has the editor open: `python Tools/bellows_cli.py ping`. It reads the session token
  from `AgentSession.json` in the editor config folder (`%APPDATA%\MageCapBellows` or
  `~/.config/MageCapBellows`).
* Otherwise start one yourself:
  `<bin>/BellowsEditor --headless --project <dir> --run-script build.py --exit`
  (`--headless` needs no window; drop it if the user wants to watch).

## 2. Plan the game in components

Map the design onto entities + components + scripts before writing code:
* Visuals: `MeshRenderer` with `Builtin/Cube|Sphere|Plane|Capsule|Cylinder|Cone|Quad` or imported
  glTF; `Material`s for color. Lights: one `DirectionalLight`, a `SkyLight`, optional point/spot lights.
* Camera: one `Camera` with `primary = True`. For 2D-style games (Tetris, Breakout) use
  `projection = "Orthographic"` and set `orthographic_size` to the board height plus margin.
* Logic: one Python script per responsibility in `Assets/Scripts/` (game manager, player, piece).
  Grid games keep their state in Python data structures and only *show* it with entities.
* Physics only where the game needs simulation (rigid bodies + colliders); grid logic needs none.
* Input: `bellows.is_key_pressed(Key.Left)` etc. in `on_update`.

## 3. Build it reproducibly

Write a build script (keep it in the project, e.g. `Tools/build_level.py` or `Assets/Editor/`)
that: creates/opens the project, writes the scripts (plain file I/O into
`editor.asset_directory()`), creates materials, entities and prefabs, then
`editor.save_scene("Scenes/Main.bscene")`. Run it with `exec-file` or `--run-script`. Re-running
it must give the same scene. `Tools/BuildFeatureTest.py` is a complete example.

## 4. Verify - never skip this

1. `editor.screenshot(path, 1280, 720)` and look at the image: is everything visible, lit and framed?
2. Play-test: `editor.play()`, `editor.run_frames(120)`, then inspect state
   (`bellows.find_entity(...)`, script instances via `entity.script`), `editor.logs(since)` and
   `editor.script_error_count()`, take a play-mode screenshot, `editor.stop()`.
   Input cannot be simulated through the API, so expose game actions as script methods
   (`entity.script.move_left()`) and call those to test the rules.
3. Fix and repeat until there are no script errors and the screenshots match the intent.
4. `editor.save_scene()`; optionally `editor.export_game(<dir>)` and run the exported game headless:
   `<Game> --headless --frames 300 --screenshot shot.png`.

## 5. Report

Tell the user what was built, how to play (controls), where the files are, and include the
final screenshot.
