"""End-to-end test of the editor automation API (bellows.editor).

Run by ctest as:  BellowsEditor --headless --no-agent-server --run-script <this file> --exit
Any failed assertion raises, which makes the editor exit with code 1.
"""

import os
import pathlib
import runpy
import shutil
import tempfile

import bellows
from bellows import editor

root = pathlib.Path(tempfile.gettempdir()) / "BellowsEditorAutomationTest"
shutil.rmtree(root, ignore_errors=True)
output = root / "Output"
output.mkdir(parents=True)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


# Project ------------------------------------------------------------------------------
check(editor.new_project(str(root / "Game"), "Automation Game"), "new_project failed")
check(editor.project_name() == "Automation Game", "project name")
check(pathlib.Path(editor.asset_directory()).is_dir(), "asset directory exists")
check(editor.scene_path() == "Scenes/Main.bscene", "the default scene is opened")
check(editor.state() == "edit", "starts in edit mode")
check(not editor.is_dirty(), "a freshly opened scene is clean")

# Editing and undo ---------------------------------------------------------------------
base_count = len(bellows.get_entities())
player = bellows.create_entity("Player")
player.add_component(bellows.MeshRenderer).mesh = "Builtin/Capsule"
player.transform.position = bellows.Vec3(1.0, 1.0, 0.0)
editor.commit("Create player")
check(editor.is_dirty(), "scene is dirty after an edit")
check(len(bellows.get_entities()) == base_count + 1, "entity created")
check(editor.can_undo(), "the edit is undoable")

editor.undo()
check(bellows.find_entity("Player") is None, "undo removes the player")
editor.redo()
player = bellows.find_entity("Player")
check(player is not None, "redo restores the player")
check(abs(player.transform.position.x - 1.0) < 1e-5, "redo restores component data")

# Selection ----------------------------------------------------------------------------
editor.select(player)
check([e.name for e in editor.selection()] == ["Player"], "selection")
editor.clear_selection()
check(editor.selection() == [], "clear selection")

# Saving and reopening -----------------------------------------------------------------
check(editor.save_scene(), "save scene")
check(not editor.is_dirty(), "saved scene is clean")
check(editor.save_scene("Scenes/Copy.bscene"), "save scene as")
check(editor.scene_path() == "Scenes/Copy.bscene", "save as switches the open scene")
check(editor.open_scene("Scenes/Main.bscene"), "reopen scene")
check(bellows.find_entity("Player") is not None, "saved entity loads back")

# Prefabs ------------------------------------------------------------------------------
player = bellows.find_entity("Player")
child = bellows.create_entity("Hat")
child.parent = player
check(editor.save_prefab(player, "Prefabs/Player.bprefab"), "save prefab")
copy = bellows.instantiate("Prefabs/Player.bprefab", bellows.Vec3(5.0, 0.0, 0.0))
check(copy is not None and copy.find_child("Hat") is not None, "prefab instantiates with children")
copy.name = "Player Copy"
editor.commit("Instantiate")

# Play mode ----------------------------------------------------------------------------
scripts = pathlib.Path(editor.asset_directory()) / "Scripts"
(scripts / "Mover.py").write_text(
    "import bellows\n\n"
    "class Mover(bellows.Script):\n"
    "    speed: float = 2.0\n\n"
    "    def on_update(self, dt):\n"
    "        self.entity.translate(bellows.Vec3(self.speed * dt, 0.0, 0.0))\n",
    encoding="utf-8")
script = player.add_component(bellows.ScriptComponent)
script.path = "Scripts/Mover.py"
editor.commit("Add script")
edit_count = len(bellows.get_entities())

editor.play()
check(editor.state() == "play", "play mode")
editor.run_frames(30)
moved = bellows.find_entity("Player")
check(moved.transform.position.x > 1.5, f"script moved the player during play ({moved.transform.position.x})")
bellows.create_entity("Spawned during play")
editor.pause(True)
check(editor.state() == "pause", "pause")
paused_x = bellows.find_entity("Player").transform.position.x
editor.run_frames(5)
check(bellows.find_entity("Player").transform.position.x == paused_x, "paused games do not update")
editor.step(1)
editor.run_frames(1)
check(bellows.find_entity("Player").transform.position.x > paused_x, "step advances a paused game")
editor.stop()
check(editor.state() == "edit", "stop")
check(abs(bellows.find_entity("Player").transform.position.x - 1.0) < 1e-5, "stopping restores the edited scene")
check(len(bellows.get_entities()) == edit_count, "entities spawned in play mode are discarded")
check(editor.script_error_count() == 0, "no script errors")

# Screenshots --------------------------------------------------------------------------
editor.set_camera((4.0, 3.0, 6.0), (0.0, 0.5, 0.0))
shot = output / "viewport.png"
editor.screenshot(str(shot), 640, 360)
check(shot.is_file() and shot.stat().st_size > 1000, "viewport screenshot")
window = output / "window.png"
editor.capture_window(str(window))
check(window.is_file() and window.stat().st_size > 1000, "window screenshot")

# Console-style evaluation and errors ---------------------------------------------------
check(editor.error_count() == 0, f"no errors were logged ({editor.error_count()})")

# The Python type stub matches the running API ------------------------------------------
repository = pathlib.Path(__file__).resolve().parents[2]
stubs = runpy.run_path(str(repository / "Tools" / "GeneratePythonStubs.py"))
committed = (repository / "Python" / "_bellows.pyi").read_text(encoding="utf-8")
check(stubs["generate"]() == committed, "Python/_bellows.pyi is out of date: run Tools/GeneratePythonStubs.py (see its docstring)")

shutil.rmtree(root, ignore_errors=True)
print("Editor automation test passed")
