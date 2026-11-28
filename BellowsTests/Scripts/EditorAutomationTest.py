"""End-to-end test of the editor automation API (bellows.editor).

Run by ctest as:  BellowsEditor --headless --no-agent-server --run-script <this file> --exit
Any failed assertion raises, which makes the editor exit with code 1.
"""

import os
import pathlib
import runpy
import shutil
import tempfile
import time

import bellows
from bellows import editor

root = pathlib.Path(tempfile.gettempdir()) / "BellowsEditorAutomationTest"
shutil.rmtree(root, ignore_errors=True)
output = root / "Output"
output.mkdir(parents=True)


def raises(action, error):
    """True when action() raises the given exception type."""
    try:
        action()
    except error:
        return True
    return False


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

# Hierarchy groups ---------------------------------------------------------------------
heroes = bellows.create_group("Heroes", "users")
heroes.note = "The player and friends"
heroes.add_entity(player)
editor.commit("Group the player")
editor.run_frames(2)
rows = editor.hierarchy_rows()
kinds = [(r["kind"], r["name"]) for r in rows[:3]]
check(kinds == [("group", "Heroes"), ("note", "The player and friends"), ("entity", "Player")], f"the Hierarchy shows the group, its note and its entity first ({kinds})")
check(rows[2]["depth"] == 1 and rows[2]["group"] == "Heroes", "a group's entities are indented under it")
check(any(r["kind"] == "ungrouped" for r in rows) and rows[-1]["group"] is None, "entities in no group come after the divider")
editor.undo()
check(bellows.group("Heroes") is None and bellows.find_entity("Player").group is None, "undo takes the group away")
editor.redo()
player = bellows.find_entity("Player")
check(bellows.group("Heroes") is not None and player.group == bellows.group("Heroes"), "redo brings the group back with its entity")
editor.run_frames(1)
check(editor.hierarchy_rows()[0]["name"] == "Heroes", "the Hierarchy follows undo and redo")

# The Hierarchy's views, search, pins, stacks and Organize
crates = [bellows.create_entity(f"Crate {i}") for i in range(1, 5)]
lamp = bellows.create_entity("Lamp")
lamp.add_component(bellows.PointLight)
lamp.add_tag("light source")
editor.set_pinned(player)
editor.commit("Crates, a lamp and a pin")
editor.run_frames(1)
rows = editor.hierarchy_rows()
check([(r["kind"], r["name"]) for r in rows[:3]] == [("section", "PINNED"), ("entity", "Player"), ("section", "SCENE")],
      "a pinned entity is listed at the top")
stacks = [r for r in rows if r["kind"] == "stack"]
check([(r["name"], r["count"]) for r in stacks] == [("Crate", 4)], f"numbered siblings fold into one stack row ({stacks})")
editor.set_hierarchy_search("has:light")
editor.run_frames(1)
names = [r["name"] for r in editor.hierarchy_rows() if r["kind"] == "entity"]
check("Lamp" in names and "Player" not in names, f"the search's has: finds lights ({names})")
editor.set_hierarchy_search("")
editor.set_hierarchy_view("tag")
editor.run_frames(1)
check(editor.hierarchy_view() == "tag", "set_hierarchy_view")
headings = [(r["name"], r["count"]) for r in editor.hierarchy_rows() if r["kind"] == "heading"]
check(headings[0] == ("light source", 1) and headings[-1][0] == "No tags", f"the By tag view lists each tag, then those with none ({headings})")
editor.set_hierarchy_view("type")
editor.run_frames(1)
lights = [r for r in editor.hierarchy_rows() if r["kind"] == "heading" and r["name"] == "Lights"]
check(lights and lights[0]["count"] >= 1, "the By type view has a Lights heading")
editor.set_hierarchy_view("groups")
moves = editor.organize_scene()
check(any(m["entity"] == bellows.find_entity("Lamp") and m["group"] == "Lighting" and m["new_group"] for m in moves),
      f"Organize suggests a Lighting group for the lamp ({moves})")
editor.organize_scene(apply=True)
check(bellows.find_entity("Lamp").is_in_group("Lighting"), "Organize applies its moves")
editor.undo()
check(bellows.group("Lighting") is None, "Organize is one undo step")
for name in ("Lamp", "Crate 1", "Crate 2", "Crate 3", "Crate 4"):
    bellows.find_entity(name).destroy()
editor.set_pinned(bellows.find_entity("Player"), False)
check(not editor.pinned(bellows.find_entity("Player")), "set_pinned(entity, False) unpins")
editor.commit("Clean up the Hierarchy checks")

# Saving and reopening -----------------------------------------------------------------
check(editor.save_scene(), "save scene")
check(not editor.is_dirty(), "saved scene is clean")
check(editor.save_scene("Scenes/Copy.bscene"), "save scene as")
check(editor.scene_path() == "Scenes/Copy.bscene", "save as switches the open scene")
check(editor.open_scene("Scenes/Main.bscene"), "reopen scene")
check(bellows.find_entity("Player") is not None, "saved entity loads back")

# The title bar's search (Ctrl+K) --------------------------------------------------------
listed = editor.search()
check(listed and all(r["kind"] == "command" for r in listed) and listed[0]["name"] == "Save Scene",
      f"an empty search lists the commands ({[r['name'] for r in listed]})")
found = editor.search("player", 5)
check(found and found[0]["kind"] == "entity" and found[0]["name"] == "Player", f"search finds an entity by name ({found})")
check(any(r["kind"] == "scene" and r["name"] == "Main.bscene" and r["detail"] == "Scenes" for r in editor.search("main")),
      "search finds a scene in its folder")
check(any(r["name"] == "Save Scene" for r in editor.search("sv sc")), "search matches scattered letters")
check(not next(r for r in editor.search("stop") if r["name"] == "Stop")["enabled"], "a command that can't run now is listed disabled")
bellows.create_entity("Searchable")
editor.commit("Searchable")
editor.run_command("Undo")
check(bellows.find_entity("Searchable") is None, "run_command runs a command (Undo)")
check(raises(lambda: editor.run_command("Stop"), ValueError), "run_command refuses a command that can't run now")
check(raises(lambda: editor.run_command("Fly to the Moon"), ValueError), "run_command refuses an unknown command")
editor.open_search("sav")
editor.run_frames(2)
check(editor.is_search_open(), "open_search")
editor.close_search()
editor.run_frames(1)
check(not editor.is_search_open(), "close_search")
check(bellows.find_entity("Player").is_in_group("Heroes") and bellows.group("Heroes").note == "The player and friends",
      "groups are saved with the scene")

# Navigation ---------------------------------------------------------------------------
# The new project's ground is a static collider, so there's something to walk on.
baked = editor.bake_navigation()
check(baked["tiles"] >= 1 and baked["polygons"] > 0, f"bake_navigation ({baked})")
check((pathlib.Path(editor.asset_directory()) / "Scenes" / "Main.bnav").is_file(), "the navmesh is saved beside the scene")

# Prefabs ------------------------------------------------------------------------------
player = bellows.find_entity("Player")
child = bellows.create_entity("Hat")
child.parent = player
check(editor.save_prefab(player, "Prefabs/Player.bprefab"), "save prefab")
copy = bellows.instantiate("Prefabs/Player.bprefab", bellows.Vec3(5.0, 0.0, 0.0))
check(copy is not None and copy.find_child("Hat") is not None, "prefab instantiates with children")
copy.name = "Player Copy"
editor.commit("Instantiate")

# Model front: a +Z-front model is turned at import, by a setting kept beside it ------------
import struct
models = pathlib.Path(editor.asset_directory()) / "Models"
models.mkdir(parents=True, exist_ok=True)
(models / "Front.bin").write_bytes(struct.pack("<9f", -0.5, 0, 1, 0.5, 0, 1, 0, 1, 2))
(models / "Front.gltf").write_text(
    '{"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],'
    ' "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],'
    ' "buffers": [{"uri": "Front.bin", "byteLength": 36}], "bufferViews": [{"buffer": 0, "byteLength": 36}],'
    ' "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",'
    ' "min": [-0.5, 0, 1], "max": [0.5, 1, 2]}]}',
    encoding="utf-8")
check(editor.model_front("Models/Front.gltf") == "+Z", "a new project turns +Z-front models by default")
editor.set_model_front("Models/Front.gltf", "-Z")
check(editor.model_front("Models/Front.gltf") == "-Z", "set_model_front")
check((models / "Front.gltf.bimport").is_file(), "the setting is saved beside the model")
for bad_call in (lambda: editor.set_model_front("Models/Front.gltf", "up"),
                 lambda: editor.set_model_front("Scenes/Main.bscene", "+Z"),
                 lambda: editor.model_front("Builtin/Cube")):
    try:
        bad_call()
        check(False, "bad model front calls raise ValueError")
    except ValueError:
        check(True, "bad model front calls raise ValueError")
editor.set_model_front("Models/Front.gltf", "+Z")

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

# Hot reload: saving a script during play swaps in its new code (polled twice a second).
editor.pause(False)
mover = scripts / "Mover.py"
mover.write_text(
    "import bellows\n\n"
    "class Mover(bellows.Script):\n"
    "    speed: float = 2.0\n\n"
    "    def on_update(self, dt):\n"
    "        self.entity.name = 'Reloaded Player'\n",
    encoding="utf-8")
# The new file must look newer even when written within the same clock tick.
stat = mover.stat()
os.utime(mover, ns=(stat.st_atime_ns, stat.st_mtime_ns + 2_000_000_000))
editor.run_frames(45)
check(bellows.find_entity("Reloaded Player") is not None, "a script saved during play is reloaded")

# load_scene during play loads into the play copy: a kept entity stays, the rest is replaced.
(pathlib.Path(editor.asset_directory()) / "Scenes" / "Next.bscene").write_text(
    '{"Version": 1, "Scene": "Next", "Entities": [{"ID": 424242, "Name": "Next Level Thing", "Parent": 0}]}',
    encoding="utf-8")
bellows.find_entity("Reloaded Player").keep_across_scenes = True
check(bellows.load_scene("Scenes/Next.bscene"), "load_scene during play")
editor.run_frames(2)
check(bellows.find_entity("Next Level Thing") is not None, "the next scene is loaded during play")
check(bellows.find_entity("Reloaded Player") is not None, "a kept entity stays through load_scene")
check(bellows.find_entity("Spawned during play") is None, "entities that aren't kept go with the old scene")
editor.stop()
check(bellows.find_entity("Next Level Thing") is None, "load_scene in play never changes the edited scene")
check(not bellows.find_entity("Player").keep_across_scenes, "keep_across_scenes set in play stays in play")
check(editor.state() == "edit", "stop")
check(abs(bellows.find_entity("Player").transform.position.x - 1.0) < 1e-5, "stopping restores the edited scene")
check(len(bellows.get_entities()) == edit_count, "entities spawned in play mode are discarded")
check(editor.script_error_count() == 0, "no script errors")

# Profiler: every frame is timed, part by part --------------------------------------------
editor.clear_profiler()
check(editor.profiler_frames(5) == [], "clear_profiler forgets the frames")
editor.play()
editor.run_frames(10)
frames = editor.profiler_frames(5)
check(len(frames) == 5, f"profiler_frames returns the newest frames ({len(frames)})")
check(all(b["index"] == a["index"] + 1 for a, b in zip(frames, frames[1:])), "profiler frames are consecutive, newest last")
newest = frames[-1]
names = [scope["name"] for scope in newest["scopes"]]
for part in ("Events", "EditorLayer", "Scene Update", "Scripts", "Scripts/Mover.py", "Scene Render", "Render Passes", "Editor UI"):
    check(part in names, f"the profiler times '{part}' ({names})")
check(all(0.0 <= scope["ms"] and scope["start_ms"] + scope["ms"] <= newest["cpu_ms"] + 1e-6 for scope in newest["scopes"]),
      "every scope lies inside its frame")
by_name = {scope["name"]: scope for scope in newest["scopes"]}
check(by_name["Scripts/Mover.py"]["depth"] == by_name["Scripts"]["depth"] + 1, "a script's time is inside Scripts")
summary = {row["name"]: row for row in editor.profiler_summary(5)}
check(summary["Scene Update"]["calls"] == 1.0, "Scene Update runs once a frame")
check(summary["Scene Update"]["total_ms"] >= summary["Scene Update"]["self_ms"] >= 0.0, "self time is part of total time")
# GPU times arrive a few frames after their frame: the newest frames don't have them yet.
check(editor.gpu_timing_supported(), "the GPU can be timed here")
recent = editor.profiler_frames(10)
timed = [frame for frame in recent if frame["gpu_timed"]]
check(len(timed) >= 4 and not recent[-1]["gpu_timed"], f"older frames have GPU times, the newest not yet ({len(timed)} of 10)")
gpu_names = [scope["name"] for scope in timed[-1]["gpu_scopes"]]
for part in ("Viewport", "Shadows", "Opaque", "Tone Mapping", "Editor UI"):
    check(part in gpu_names, f"the profiler times '{part}' on the GPU ({gpu_names})")
check(timed[-1]["gpu_ms"] > 0.0, "a frame's GPU time adds up its renderers")
gpu_summary = {row["name"]: row for row in editor.profiler_summary(10, gpu=True)}
check(gpu_summary["Viewport"]["depth"] == 0 and gpu_summary["Shadows"]["depth"] == 1, "passes are inside their renderer")
editor.set_profiler_paused(True)
check(editor.profiler_paused(), "set_profiler_paused")
last = editor.profiler_frames(1)[0]["index"]
editor.run_frames(3)
check(editor.profiler_frames(1)[0]["index"] == last, "a paused profiler keeps the frames it has")
editor.set_profiler_paused(False)
editor.run_frames(2)
check(editor.profiler_frames(1)[0]["index"] > last, "recording resumes")
editor.stop()

# ForgeScript debugger: a breakpoint pauses the game, then step, look and go on ----------
(scripts / "Counter.forge").write_text(
    "set ticks to 0\n"
    "when update with delta\n"
    "\tadd 1 to ticks\n"
    "\tset my name to \"Counter {ticks}\"\n"
    "end\n",
    encoding="utf-8")
counter = bellows.create_entity("Counter")
counter.add_component(bellows.ScriptComponent).path = "Scripts/Counter.forge"
editor.commit("Add counter")
editor.set_breakpoints("Scripts/Counter.forge", [3, 0, 3])
check(editor.breakpoints("Scripts/Counter.forge") == [3], "breakpoint lines are cleaned up")
editor.play()
editor.run_frames(2)
debug = editor.debug_state()
check(debug["stopped"] and debug["file"] == "Scripts/Counter.forge" and debug["line"] == 3, f"stops at the breakpoint ({debug})")
check(debug["reason"] == "breakpoint", "the stop says why")
check(editor.state() == "pause", "a stop pauses the game")
check(debug["stack"][0]["function"] == "when update", "the call stack names the handler")
check(any(v["name"] == "delta" for v in debug["variables"]), "the handler's variables are shown")
check(any(v["name"] == "ticks" and v["value"] == "0" for v in debug["script_variables"]), "the script's variables are shown")
editor.run_frames(3)
check(editor.debug_state()["line"] == 3, "the game stays still while a script is stopped")
check(editor.debug_step_over(), "step over")
check(editor.debug_state()["line"] == 4, "step over goes to the next line")
editor.set_breakpoints("Scripts/Counter.forge", [])
check(editor.debug_continue(), "continue")
check(editor.state() == "play" and not editor.debug_state()["stopped"], "continuing lets the game run again")
editor.run_frames(3)
check(bellows.find_entity("Counter") is None, "the script runs freely after the breakpoint is removed")
check(editor.debug_pause(), "pause at the next line")
editor.run_frames(1)
debug = editor.debug_state()
check(debug["stopped"] and debug["reason"] == "pause", f"pause stops the next line ({debug})")
editor.stop()
check(editor.state() == "edit" and not editor.debug_state()["stopped"], "play stops even while a script is held")
check(not editor.debug_continue(), "nothing to continue after play stops")

# Conditions, log points and Evaluate
editor.set_breakpoint("Scripts/Counter.forge", 4, condition="ticks >= 3")
editor.set_breakpoint("Scripts/Counter.forge", 3, log_message="tick {ticks}")
details = editor.breakpoint_details("Scripts/Counter.forge")
check(details == [{"line": 3, "condition": "", "log_message": "tick {ticks}"},
                  {"line": 4, "condition": "ticks >= 3", "log_message": ""}], f"breakpoint details ({details})")
editor.set_breakpoints("Scripts/Counter.forge", [3, 4])
check(editor.breakpoint_details("Scripts/Counter.forge") == details, "setting the same lines keeps their details")
try:
    editor.set_breakpoint("Scripts/Counter.forge", 0)
    check(False, "a breakpoint at line 0 is refused")
except ValueError:
    pass
try:
    editor.debug_evaluate("ticks")
    check(False, "nothing to evaluate while nothing is stopped")
except ValueError:
    pass
editor.play()
editor.run_frames(6)
debug = editor.debug_state()
check(debug["stopped"] and debug["line"] == 4 and debug["message"] == "", f"a condition stops only when it is true ({debug})")
check(editor.debug_evaluate("ticks") == {"type": "number", "value": "3"}, "Evaluate reads a script variable")
check(editor.debug_evaluate("ticks * 2 + delta * 0")["value"] == "6", "Evaluate reads the handler's variables")
try:
    editor.debug_evaluate("ticks +")
    check(False, "a line with a mistake can't be worked out")
except ValueError as error:
    check(str(error) != "", "Evaluate says why it can't work a line out")
check(editor.debug_evaluate("ticks")["value"] == "3", "working things out never changes the game")
editor.set_breakpoint("Scripts/Counter.forge", 4, condition="nothing.size > 1")
check(editor.debug_continue(), "continue past the condition")
editor.run_frames(1)
debug = editor.debug_state()
check(debug["stopped"] and debug["line"] == 4 and "nothing.size > 1" in debug["message"],
      f"a condition that can't be worked out stops, with the reason ({debug})")
editor.stop()
editor.clear_breakpoints()
check(editor.script_error_count() == 0, "no script errors while debugging")

# Play in a game window: the runtime plays the open scene in its own process ------------
def wait_for_game(seconds):
    deadline = time.monotonic() + seconds
    while editor.is_game_running() and time.monotonic() < deadline:
        editor.run_frames(5)
    editor.run_frames(1)


try:
    editor.play_in_window("somewhere")
    check(False, "an unknown scene choice is refused")
except ValueError:
    pass
first_log = editor.latest_log_sequence()
check(editor.play_in_window(headless=True, frames=30), "the game window starts")
check(editor.state() == "edit", "the editor keeps editing while the game window runs")
wait_for_game(180)
check(not editor.is_game_running(), "the game window ends after its frames")
check(editor.game_exit_code() == 0, f"the game window ends cleanly ({editor.game_exit_code()})")
game_lines = [entry["text"] for entry in editor.logs(first_log)]
check(any("[Game]" in line and "PlayScene.bscene" in line for line in game_lines),
      "the game's log reaches the Console, and it played the open scene")
check(any("Game window closed" in line for line in game_lines), "the Console says the game window closed")

check(editor.play_in_window(headless=True), "a game window without a frame limit starts")
editor.run_frames(2)
check(editor.is_game_running(), "it keeps running")
editor.stop_game()
check(not editor.is_game_running() and editor.game_exit_code() != 0, "stop_game ends it")
check(any("Game window stopped" in entry["text"] for entry in editor.logs(first_log)), "the Console says it was stopped")

# Screenshots --------------------------------------------------------------------------
editor.set_camera((4.0, 3.0, 6.0), (0.0, 0.5, 0.0))
shot = output / "viewport.png"
editor.screenshot(str(shot), 640, 360)
check(shot.is_file() and shot.stat().st_size > 1000, "viewport screenshot")
window = output / "window.png"
editor.capture_window(str(window))
check(window.is_file() and window.stat().st_size > 1000, "window screenshot")

# The UI view: the game's screen at a set size, with handles on the selected UI rect ----
def png_size(path):
    header = pathlib.Path(path).read_bytes()[16:24]
    return int.from_bytes(header[:4], "big"), int.from_bytes(header[4:], "big")


saved_view = editor.ui_view()
check(saved_view["width"] >= 64 and saved_view["height"] >= 64, "the UI view has a size")
menu = bellows.create_entity("Menu")
menu.add_component(bellows.UICanvas)
button = bellows.create_entity("Button", parent=menu)
button_rect = button.add_component(bellows.UIRect)
button_rect.offset_min = bellows.Vec3(40.0, 30.0, 0.0)
button_rect.offset_max = bellows.Vec3(240.0, 90.0, 0.0)
editor.select(button)
editor.set_ui_view(True, 320, 180)
check(editor.ui_view() == {"enabled": True, "width": 320, "height": 180}, "set_ui_view sets the state and size")
ui_shot = output / "ui_view.png"
editor.screenshot(str(ui_shot))
check(png_size(ui_shot) == (320, 180), f"a UI view screenshot is the UI view's size ({png_size(ui_shot)})")
editor.capture_window(str(output / "ui_view_window.png"))	# Draws the rect tool over the fitted image
editor.set_ui_view(False)
check(editor.ui_view() == {"enabled": False, "width": 320, "height": 180}, "turning the UI view off keeps its size")
for bad in ((100, 0), (32, 32), (9000, 100)):
    try:
        editor.set_ui_view(True, *bad)
        check(False, f"set_ui_view refuses {bad}")
    except ValueError:
        pass
check(not editor.ui_view()["enabled"], "a refused size changes nothing")
editor.set_ui_view(saved_view["enabled"], saved_view["width"], saved_view["height"])
editor.clear_selection()
menu.destroy()

# UI templates and anchor presets ----
templates = editor.ui_templates()
check("main_menu" in templates and "hud" in templates and "text_input" in templates, f"ui_templates lists the templates ({templates})")
main_menu = editor.create_ui("Main Menu")
check(main_menu.has_component(bellows.UICanvas) and main_menu.name == "Main Menu", "create_ui makes a Main Menu with its own canvas")
check(editor.selection() == [main_menu], "create_ui selects what it made")
buttons = bellows.find_entity("Buttons")
check(buttons is not None and [child.name for child in buttons.children] == ["Play", "Settings", "Quit"], "the Main Menu's three buttons")
slider = editor.create_ui("slider", parent=buttons)
check(slider.parent == buttons and slider.has_component(bellows.UISlider), "a widget goes inside the canvas its parent is in")
hud = editor.create_ui("hud")
fill = bellows.find_entity("Fill")
check(fill is not None and abs(fill.get_component(bellows.UIRect).anchor_max.x - 0.75) < 1e-6, "the HUD's health bar fill")
editor.run_frames(1)
panel = editor.create_ui("panel")
editor.run_frames(1)
rect = panel.get_component(bellows.UIRect)
size_before, offset_before = rect.size, rect.offset_min
editor.set_ui_anchors(panel, "bottom_right")
check(abs(rect.anchor_min.x - 1.0) < 1e-6 and abs(rect.anchor_max.y - 1.0) < 1e-6, "set_ui_anchors sets a preset's anchors")
# The rect stays put (the C++ tests check its place on screen): same size, offsets now from the corner.
check(abs(rect.size.x - size_before.x) < 0.01 and abs(rect.size.y - size_before.y) < 0.01, "set_ui_anchors keeps the size")
check(rect.offset_min.x < offset_before.x and rect.offset_min.y < offset_before.y, "set_ui_anchors keeps the place")
editor.set_ui_anchors(panel, "full_rect", move=True)
check(abs(rect.offset_min.x) < 0.01 and abs(rect.offset_max.y) < 0.01, "move=True fills the parent for Full Rect")
plain = bellows.create_entity("Plain")
for bad_call in (lambda: editor.create_ui("pause_menu"), lambda: editor.set_ui_anchors(panel, "middle"),
                 lambda: editor.set_ui_anchors(plain, "center")):
    try:
        bad_call()
        check(False, "a bad template, preset or entity raises ValueError")
    except ValueError:
        pass
editor.clear_selection()
for entity in (main_menu, hud, panel.parent, plain):
    entity.destroy()

# Console-style evaluation and errors ---------------------------------------------------
check(editor.error_count() == 0, f"no errors were logged ({editor.error_count()})")

# The Python type stub matches the running API ------------------------------------------
repository = pathlib.Path(__file__).resolve().parents[2]
stubs = runpy.run_path(str(repository / "Tools" / "GeneratePythonStubs.py"))
committed = (repository / "Python" / "_bellows.pyi").read_text(encoding="utf-8")
check(stubs["generate"]() == committed, "Python/_bellows.pyi is out of date: run Tools/GeneratePythonStubs.py (see its docstring)")

shutil.rmtree(root, ignore_errors=True)
print("Editor automation test passed")
