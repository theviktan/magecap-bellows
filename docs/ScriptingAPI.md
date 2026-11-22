# Scripting API reference

Game logic in MageCap Bellows is written in **ForgeScript** (Bellows' own English-like language,
`.forge` files) or **Python 3** (CPython 3.13 embedded in the engine, `.py` files). Both use the
same engine API with the same names, and a game can mix them freely. This page describes the API
in Python terms; [Using the API from ForgeScript](#using-the-api-from-forgescript) shows how the
same calls look in ForgeScript. In Python everything lives in the `bellows` package; exported
games bundle their own Python runtime, so players do not need Python installed.

```python
import bellows
from bellows import Vec3, Key


class Player(bellows.Script):
    speed: float = 5.0                       # Shown and editable in the Inspector

    def on_update(self, dt):
        direction = Vec3(0.0)
        if bellows.is_key_down(Key.W): direction += self.entity.forward
        if bellows.is_key_down(Key.S): direction -= self.entity.forward
        self.entity.translate(direction * (self.speed * dt))
```

Attach a script to an entity with a **Script** component (Inspector → Add Component → Script, or
drag a `.py` or `.forge` file onto the component's Script field). The project's `Assets` folder is on
`sys.path`, so scripts can import each other (`from Scripts.Helpers import clamp01`). It comes after
Python's standard library and the engine's modules, so a game file named like one of them (`json.py`,
`bellows.py`) is never imported in its place; give your modules other names. Scripts saved during
play are reloaded right away (see Hot reload below).

The API is described once (`Bellows/Source/Bellows/Scripting/ScriptApiDefinition.h`), so both
languages always offer the same functions.

For code editors and type checkers, `Python/_bellows.pyi` (next to the `bellows` package, and in
every exported game's `Python` folder) describes every function and class with its types and
documentation. It is generated from the running engine by `Tools/GeneratePythonStubs.py`.
`forgescript-api.json` (next to the programs) describes the same API for ForgeScript tools such as
`forgescript --lsp`; `BellowsEditor --write-forgescript-api <file>` writes it.

The complete API is exercised by `Projects/FeatureTest/Assets/Scripts/FeatureTest.py`, and from
ForgeScript by `FeatureTest.forge` next to it; when in doubt, those files show working usage of
every function below.

## Conventions

| Topic | Convention |
|-------|------------|
| Units | Meters, seconds, kilograms. |
| Axes | Right-handed, **Y up**. An entity's *forward* is **-Z**. |
| Angles | `Entity.rotation`, `Transform.rotation_degrees`, light angles, camera FOV and `SkyLight.rotation` are **degrees**. `Transform.rotation` and `Quat.from_euler` use **radians**. |
| Colors | Linear RGB(A) floats, usually 0-1 (values above 1 are allowed for HDR emission). |
| Vectors | Anywhere a `Vec2/Vec3/Vec4` is expected you may pass a tuple or list, e.g. `entity.position = (0, 1, 0)`. A vector or color holding NaN or infinity raises `ValueError` naming what it was for (in ForgeScript, a mistake), so it never reaches physics or a saved scene. |
| Asset paths | Relative to the project's `Assets` folder with forward slashes, e.g. `"Models/Ship.glb"`. Built-in meshes: `Builtin/Cube`, `Builtin/Sphere`, `Builtin/Plane`, `Builtin/Capsule`, `Builtin/Cylinder`, `Builtin/Cone`, `Builtin/Quad`. |
| Errors | Invalid arguments raise `ValueError`/`TypeError`; using a destroyed entity raises `RuntimeError`. A script that raises 10 times is disabled; errors are shown in the Console. |

## Script lifecycle

Derive from `bellows.Script`. The engine sets `self.entity` before any callback runs.

| Callback | When |
|----------|------|
| `on_create(self)` | Once, after every script in the scene has been created (safe to look up other entities' scripts). |
| `on_update(self, dt)` | Every frame. `dt` is in seconds. |
| `on_fixed_update(self, dt)` | Every physics step (fixed timestep, default 60 Hz). Use it for forces. |
| `on_destroy(self)` | When the entity is destroyed or play stops. |
| `on_collision_enter(self, other)` / `on_collision_exit(self, other)` | Started/stopped touching `other` (an `Entity`). Both bodies need colliders; at least one must be dynamic. |
| `on_trigger_enter(self, other)` / `on_trigger_exit(self, other)` | `other` entered/left this trigger, or this entity entered/left a trigger. |
| `on_event(self, name, *values)` | An event sent by any script with `bellows.emit` (Python) or `trigger` (ForgeScript), this one included. |
| `on_reload(self)` | After the script's file was saved during play and its new code took over (hot reload, below). |

**Engine events.** Besides the events scripts send each other, the engine sends two to every script,
in both languages (Python's `on_event`, ForgeScript's `when <name> with ...`):

| Event | Values | When |
|-------|--------|------|
| `script error` | `message`, `file`, `line`, `stack`, `language`, `name` | A script raised an error (it is also logged as before). `file` is the script's asset path with forward slashes (`Scripts/player.py`), or just a file name for code outside the assets; `line` is 0 when unknown. `stack` is a list of the calls, innermost first, each `"file:line in function"` or `"file:line"` for code at the top of a file. `language` is `"python"` or `"forgescript"`; `name` is the Python exception's class (`"AttributeError"`), or `""`. An error raised while a `script error` handler runs is only logged, never sent again. |
| `scene loaded` | `name`, `path` | After `load_scene` has loaded a scene, to every script, kept ones included: the new scene's name and its asset path. New scripts have had `on_create` by then, so a kept player can find the new level's things. |
| `game quitting` | none | Once, just before a normal quit: the window closing, `quit()`, the end of a `--frames` run, and the editor's Stop. Never when `load_scene` changes the scene (`on_destroy` runs then too for what isn't kept, so it can't tell the two apart; `scene loaded` follows the change). Everything waits for the handlers to finish, so they can still save or use the network. A crash, or stopping a game window from the editor (which ends its program at once), sends nothing. |

```python
class CrashReporter(bellows.Script):
    def on_event(self, name, *values):
        if name == "script error":
            message, file, line, stack, language, error_name = values
            bellows.log(f"{error_name or 'Error'} at {file}:{line}: {message}")
```

**Exposed fields.** Class attributes with a type annotation of `bool`, `int`, `float`, `str`,
`Vec2`, `Vec3` or `Vec4` appear in the Inspector. Values edited there are saved with the scene and
assigned to the instance before `on_create`.

`Script` also offers the shortcuts `self.transform` and `self.get_component(component)`.

**Time limit.** In the editor, a callback, a script's top-level code, or code typed in the Console that
runs for more than 10 seconds without returning is stopped with `KeyboardInterrupt` (which
`except Exception` doesn't catch), and the Console says why. This keeps a loop with no end from
freezing the editor, like ForgeScript's "ran too long" guard. Code that catches everything (a bare
`except:`) and keeps going is stopped again on every line it runs, a moment later. A script whose
callback was stopped still gets its next callbacks (a script whose top-level code was stopped doesn't
start). A single long call into C, such as `sum(range(10**10))`, is only stopped once it returns.
Automation that lets time pass with `bellows.editor.run_frames` is never stopped, and scripts run with
`--run-script` have no limit.

A shipped game has no limit, so a long load such as generating a world is never cut off; a game
played from the editor has the editor's. A game can set its own for the rest of its play:

| Function | Description |
|----------|-------------|
| `set_python_time_limit(seconds)` | Seconds Python may run without returning; `0` or `None` turns the limit off. The editor puts its 10 seconds back when play stops. |
| `python_time_limit()` | The current limit in seconds; `0.0` when there is none. |

**Hot reload.** Saving a `.py` or `.forge` script while the editor is playing swaps in its new code
within half a second, and the Console shows `Reloaded <file>`. A Python script's module runs again and
each running object switches to the new class: its attribute values (including Inspector fields) stay,
only the methods change, then `on_reload` runs. Attributes the new version first sets in `on_create`
don't exist on objects that were already running, so set defaults as class attributes. Saving a helper
module that scripts import reloads it and every script. A script with a mistake (a syntax error, an
exception while its module runs, a renamed class) keeps running its old version and the mistake goes
to the Console. ForgeScript scripts reload the same way and receive `when reloaded`
(see the ForgeScript guide).

## Math

### `Vec2`, `Vec3`, `Vec4`
Constructors: `Vec3()`, `Vec3(scalar)`, `Vec3(x, y, z)`, `Vec3((x, y, z))`.

| Member | Vec2 | Vec3 | Vec4 |
|--------|:----:|:----:|:----:|
| `x y` / `z` / `w` (read/write) | ✓ | ✓ | ✓ |
| `r g b a` (read/write) | | | ✓ |
| `+ - * /` (vector or scalar), unary `-` | ✓ | ✓ | ✓ (no unary `-`) |
| `+= -= *=` | | ✓ | |
| `length()`, `normalized()`, `dot(other)` | ✓ | ✓ | |
| `length_squared()`, `cross(other)`, `distance(other)`, `lerp(other, t)` | | ✓ | |
| `copy()`, indexing, `len()`, iteration, `==` | ✓ | ✓ | ✓ |

### `Quat`
`Quat()` (identity), `Quat(w, x, y, z)`, `Quat.from_euler(radians: Vec3)`,
`Quat.from_euler_degrees(degrees: Vec3)`, `Quat.from_axis_angle(axis: Vec3, radians)`.
Members: `w x y z`, `q1 * q2`, `q * vec3` (rotates the vector), `to_euler()`,
`to_euler_degrees()`, `normalized()`, `inverse()`, `slerp(other, t)`.

### Functions
`lerp(a, b, t)`, `clamp(value, low, high)`. `bellows.VERSION` is the engine version string.

## Time, input and logging

| Function | Description |
|----------|-------------|
| `delta_time()` | Seconds since the previous frame. |
| `time()` | Seconds of game time since play started; it stops while the game is paused. |
| `frame()` | Frames since play started. |
| `set_paused(paused=True)` / `is_paused()` | Pauses or resumes the game. While it is paused, physics, `time()` and scripts stop, except scripts marked **Run While Paused** (`ScriptComponent.run_while_paused`), whose `delta_time()` is the real frame time. The UI still answers and every script still gets UI events like `clicked`, so a pause menu works; tick Run While Paused on its script if it waits or animates (the Inspector warns about a ForgeScript script whose UI handlers wait). ForgeScript waits hold the same way and carry on with the time they had left; a marked script's waits count down in real time. Audio keeps playing. A new scene (`load_scene`) starts unpaused. This is the game's own pause, separate from the editor's Pause button. |
| `is_key_down(key)` / `is_key_pressed(key)` / `is_key_released(key)` | Held / went down this frame / went up this frame. `key` is a `Key` value (`Key.W`, `Key.Space`, `Key.LeftShift`, `Key.F1`, `Key.D0`…`Key.D9`, `Key.Up`, …) or its name as a string (`"W"`). |
| `is_mouse_down(button=MouseButton.Left)` / `is_mouse_pressed(...)` / `is_mouse_released(...)` | Mouse buttons: `MouseButton.Left`, `Right`, `Middle`, `Button3`, `Button4`. |
| `mouse_position()` | `Vec2` in viewport pixels, top-left origin. |
| `mouse_delta()` / `scroll_delta()` | Movement this frame (`Vec2`). |
| `viewport_size()` | Size of the game view in pixels (`Vec2`). |
| `is_action_down(action)` / `is_action_pressed(action)` / `is_action_released(action)` | Input actions by name (`"jump"`): down while any input bound to the action is held; pressed / released on the frame the action starts / ends. Unknown actions raise `ValueError`. |
| `action_names()` | The project's input actions, sorted by name. |
| `is_gamepad_down(button)` / `is_gamepad_pressed(button)` / `is_gamepad_released(button)` | Buttons of the first gamepad by name: `"A"`, `"B"`, `"X"`, `"Y"`, `"LeftBumper"`, `"RightBumper"`, `"Back"`, `"Start"`, `"Guide"`, `"LeftThumb"`, `"RightThumb"`, `"DPadUp"`, `"DPadRight"`, `"DPadDown"`, `"DPadLeft"` (Xbox names; any capitalization). Unknown names raise `ValueError`. |
| `gamepad_connected()` | True while a gamepad is connected. |
| `ui_wants_pointer()` / `ui_wants_keyboard()` | True while the game UI is using the pointer (it is over a panel or button, a press started on one, or a modal canvas is shown) / the keyboard and gamepad (a modal canvas is shown). Input the UI uses reads as not held, pressed or released, so most games never need to ask. |
| `ui_focused()` / `ui_hovered()` / `ui_clear_focus()` | The UI entity with keyboard and gamepad focus / under the pointer (or `None`); takes focus away from the UI. |
| `save_screenshot(path="", max_width=1920)` | Saves what the player sees (the game camera's image, never editor overlays) and returns the file's full path. PNG, or JPEG (quality 85) for `.jpg`/`.jpeg`. `""` names it `Screenshots/<scene>-<date>-<time>.png`; relative paths are inside `user_folder()`; missing folders are created. A wider image is scaled down to `max_width` (0 keeps the full size). Raises `ValueError` before anything is drawn, for other formats, or when the file can't be written. |
| `user_folder()` | The game's own writable folder for saves, screenshots and reports: `%APPDATA%\MageCapBellows\Games\<project name>` on Windows, `~/.local/share/MageCapBellows/Games/<project name>` on Linux (`$XDG_DATA_HOME` if set; `BELLOWS_USER_DATA_DIR` overrides the base folder). |
| `log_folder()` | `user_folder()` plus `Logs`. A game playing in its own window (exported, or started with the editor's Play in a game window) writes its log there as `Game.log`, and keeps the last run's as `Game.previous.log`; lines logged before the project loaded are written to it too. If the old log can't be renamed (another copy of the game has it open), the new lines are added to the end of `Game.log`. Playing inside the editor logs to the Console and the editor's own `Editor.log` instead. |
| `save_data(name, value)` | Saves `value` as `user_folder()/Saves/<name>.json`, to read back in a later session; returns `False` (with a Console warning) when the file can't be written. `value`: `None`, bools, numbers, `str`, `Vec3`, and lists and dicts with text keys of these; an `Entity` (it only exists while the game plays), infinite numbers and other types raise `ValueError`. The file is written next to the old save and then renamed over it, so a crash never leaves half a save. `name`: 1 to 64 letters, digits, spaces, `-` and `_`, not starting or ending with a space and not a Windows device name (`CON`, `COM1`, ...); others raise `ValueError`. |
| `load_data(name, default=None)` | The value saved under `name`, or `default` when there is none. A save that can't be read is renamed to `<name>.damaged.json` with a Console warning, and `default` is returned, so a broken save never stops a game. Vectors come back as `Vec3`, numbers as floats. |
| `has_data(name)` / `delete_data(name)` | Whether a value is saved under `name` / deletes it (`False` when there was none). |
| `real_time()` | Real-world seconds since January 1, 1970 (UTC). Unlike `time()`, it keeps counting while the game is closed, for things that happen while the player is away. |
| `emit(name, *values)` | Sends an event to every running script, right away and in the order the scripts were created: Python's `on_event(self, name, *values)` and ForgeScript's `when <name> with ...`. Values: `None`, bools, numbers (they arrive as floats), `str`, `Vec3`, `Entity`, lists and dicts with text keys; anything else raises `ValueError`. (A ForgeScript value Python can't read, such as a function, arrives as `None` with a Console warning.) Events that keep answering each other stop with a `ValueError` after 64 levels. Python only (ForgeScript uses `trigger`). |
| `log(*values)` / `log_warning(*values)` / `log_error(*values)` | Write to the Console (values are joined with spaces). `print()` also goes to the Console. |

In the editor, game input is only delivered while the Viewport has focus during play.

**Input actions** let a game ask for "jump" instead of naming keys, so the same script works with a
keyboard, a mouse and a gamepad, and players' controls can change without touching code. Set them
up in **Settings → Project → Input actions**: each action has a list of inputs (`Key:Space`,
`Mouse:Left`, `Gamepad:A`). New projects start with `move_left`, `move_right`, `move_forward`,
`move_back`, `jump`, `interact` and `pause` (WASD, the arrow keys, the D-pad, Space, E, Escape and
the matching gamepad buttons); **Add default actions** adds any that are missing. Every project also
has the actions the game UI navigates with, added when it is opened if they are missing: `ui_accept`
(Enter, gamepad A), `ui_cancel` (Escape, B), `ui_up`, `ui_down`, `ui_left`, `ui_right` (arrow keys,
D-pad), `ui_next` (Tab, right bumper; Shift+Tab goes back) and `ui_previous` (left bumper). The project file
stores them under `"Input": { "Actions": { "jump": ["Key:Space", "Gamepad:A"] } }`.

```python
if bellows.is_action_pressed("jump") and self.on_ground:
    self.body.add_impulse(Vec3(0, 5, 0))
```

## Scene and entities

| Function | Description |
|----------|-------------|
| `create_entity(name="Entity", parent=None)` | Creates an entity (with a Transform) and returns it. |
| `destroy(entity)` / `entity.destroy()` | Destroys the entity and its children (at the end of the frame during play). |
| `find_entity(name)` | First entity with that name, or `None`. |
| `get_entity(id)` | Entity by its persistent ID (`entity.id`), or `None`. |
| `get_entities(component=None)` | All entities, optionally only those with a component (`get_entities(bellows.RigidBody)` or `get_entities("RigidBody")`). |
| `instantiate(prefab, position=None, parent=None)` | Spawns a `.bprefab` and returns its root entity. |
| `primary_camera()` | The entity whose Camera is primary, or `None`. |
| `screen_to_world_ray(screen_position)` | `(origin, direction)` through a viewport pixel, or `None` without a camera. |
| `world_to_screen(world_position)` | `Vec2` pixel position, or `None` if behind the camera. |
| `scene_name()`, `is_playing()` | Current scene name; whether the game is running. |
| `load_scene(path)` | Switches to another scene at the end of the frame. Returns `False` if it does not exist. Entities marked `keep_across_scenes` stay, with their children, scripts (no second `on_create`), physics and playing sounds; everything else is destroyed (`on_destroy` runs). If the new scene has an entity with a kept entity's ID (the player placed in every level), the kept one stays and the scene's copy is left out. `time()` keeps counting (a kept ForgeScript script's `wait` finishes on time; a wait in a script that goes is cancelled), the game starts unpaused, and every script then gets `scene loaded`. A kept script still holding an entity from the old scene gets the usual "no longer exists" error when it uses it. |
| `quit(exit_code=0)` | Quits the game (in the editor: stops play mode). |
| `started_from_editor()` | `True` while the game plays in the editor: in its viewport, or in a game window the editor started. `False` in an exported game or a runtime started by hand. For tools that should behave differently while you develop (a crash reporter that ignores editor runs). |
| `get_gravity()` / `set_gravity(Vec3)` | Scene gravity (default `(0, -9.81, 0)`). |
| `raycast(origin, direction, max_distance=1000)` | First physics collider hit, as a `RaycastHit` (`entity`, `point`, `normal`, `distance`) or `None`. Play mode only. |

### `Entity`

| Member | Description |
|--------|-------------|
| `id` | Persistent 64-bit ID (stable across saves). |
| `valid` / `bool(entity)` | `False` once destroyed. |
| `name` | Read/write. |
| `keep_across_scenes` | Read/write. When true, the entity and its children stay when `load_scene` changes the scene (the Inspector's **Keep Across Scenes**, saved in the scene). A kept child of an entity that goes moves to the top level and stays where it is in the world. |
| `position` | **World** position (read/write). |
| `local_position` | Position relative to the parent. |
| `rotation` | Local rotation as Euler angles in **degrees**. |
| `scale` | Local scale. |
| `forward`, `right`, `up` | World-space unit axes (forward is -Z). |
| `parent` | Read/write (`None` = root). Setting keeps the world transform; cycles raise `ValueError`. |
| `children`, `find_child(name)` | Direct children; recursive search by name. |
| `translate(delta)`, `rotate(degrees)`, `look_at(target, up=Vec3(0,1,0))` | Movement helpers (`rotate` is in local space). |
| `transform` | The `Transform` component. |
| `add_component(c)`, `get_component(c)`, `has_component(c)`, `remove_component(c)`, `components` | `c` is a component class (`bellows.RigidBody`) or name (`"RigidBody"`). `add_component` returns the component (existing or new); `get_component` returns `None` when missing. |
| `value` | A widget's value: a `UIToggle`'s `on`, a `UISlider`'s `value`, a `UIDropdown`'s `selected` or a `UITextInput`'s text (see Widgets). |
| `text`, `visible`, `interactable`, `focus()` | Everyday UI shortcuts: `text` is its `Text` component's text; `visible` is its `UIRect`'s (or `UICanvas`'s) visibility; `interactable` is its `UIRect`'s; `focus()` gives it keyboard and gamepad focus and returns `False` when it can't take it. An entity without the component they need raises `RuntimeError` naming the entity. |
| `script` | The running script instance (play mode), or `None`. |

## Components

Component objects are lightweight views: keep the entity and call `get_component` again rather
than storing components for a long time.

### `Transform`
`position` / `translation` (local, `Vec3`), `rotation` (local Euler, **radians**),
`rotation_degrees`, `quaternion` (`Quat`), `scale`, read-only `world_position`, `forward`, `right`, `up`.

### `MeshRenderer`
`mesh` (asset path: `.gltf`/`.glb` or `Builtin/...`), `material` (asset path, `Material` object or
`None` for the mesh's own materials), `color` (`Vec4`; setting it gives the entity its own material
instance), `cast_shadows`, `visible`.

### `Camera`
`projection` (`"Perspective"`/`"Orthographic"`), `fov` (vertical, degrees), `orthographic_size`
(visible height in meters), `near_clip`, `far_clip`, `primary`.

### Lights
* `DirectionalLight`: `color`, `intensity`, `cast_shadows` — shines along the entity's forward (-Z).
* `PointLight`: `color`, `intensity`, `range`.
* `SpotLight`: `color`, `intensity`, `range`, `inner_angle`, `outer_angle` (degrees) — forward (-Z).
* `SkyLight`: `environment` (`.hdr` equirectangular image; empty = procedural sky), `intensity`,
  `rotation` (degrees), `skybox_blur` (0 = sharp background).

### `Text`
Text drawn in the world, crisp at any distance (multi-channel signed distance field fonts).
* `text` (UTF-8; `"\n"` starts a new line), `font` (`.ttf` or `.otf`; empty = the built-in Inter),
  `size` (height of one em in meters, or reference pixels under a UI canvas; a line is about 1.2 em), `color` (`Vec4`, alpha included).
* `alignment` (`"left"`, `"center"`, `"right"`), `vertical_alignment` (`"top"`, `"middle"`,
  `"bottom"`), `line_spacing` (1 = normal).
* `box_size` (`Vec3`; x is the width and y the height in meters, z is unused; 0 sizes the box to
  the text), `wrap` (wrap at the box's width), `overflow` (`"visible"`, `"clip"` cuts the text at
  the box, `"ellipsis"` ends the last line that fits with "...").
* `outline_width` (in ems, 0 to 0.1; 0 = no outline), `outline_color`.
* `face_camera` (always turned toward the camera, like a name over a character), `visible`,
  `theme_color` (screen text: the color comes from its canvas's theme; see Themes).
* `measured_size` (read-only `Vec3`): the laid-out text's width and height in meters.

The text is centered on the entity and faces its forward axis (-Z) unless `face_camera` is set; the
entity's scale and rotation apply. Unknown alignment or overflow names raise `ValueError`. A font
that can't be loaded logs an error and falls back to the built-in font.

```python
label = entity.add_component("Text")
label.text = "Score: 0"
label.size = 0.4
label.outline_width = 0.04
```

### Screen UI (`UICanvas`, `UIRect`, `UIImage`)
A screen UI is a `UICanvas` entity with UI entities under it. It is drawn over the game at the
screen's resolution, after tonemapping and anti-aliasing, so it is never blurred or bloomed, and
screenshots include it. A canvas covers the whole screen whatever its own parent is. A `Text` under a
canvas is screen text: its `size` is in reference pixels and its box is the entity's `UIRect`.

* `UICanvas`: `reference_size` (the screen size the UI is designed for, default 1920 × 1080; x and y
  of a `Vec3`), `scale_mode` (`"fit_height"`, `"fit_width"`, `"expand"` (default: the whole
  reference area fits), `"constant_pixels"`), `sort_order` (higher draws on top), `visible`, and
  `scale` (read-only: screen pixels per reference pixel right now).
* `UIRect`: where the entity sits inside its parent's rectangle, like Godot's anchors and offsets.
  `anchor_min` and `anchor_max` are fractions of the parent ((0, 0) top-left, (1, 1) bottom-right);
  equal anchors pin a fixed-size rect to that point, different anchors stretch it with the parent.
  `offset_min` and `offset_max` move the anchored corners, in reference pixels (y points down).
  `pivot` is the point that stays put when the size changes; `position` (the pivot relative to the
  anchors) and `size` move and resize the rect through the offsets. `visible` (hidden rects hide
  everything under them), `interactable`, and read-only `screen_position` and `screen_size` (pixels,
  as of the last frame drawn). An entity without a `UIRect` fills its parent's rect.
  In the editor, the viewport toolbar's **UI view** button (or **View → UI View**) shows the game's
  screen at a set size (**View → UI View Size**, or the size label under the viewport). There, drag
  the selected rect to move it, its edges and corners to resize it, and the green anchor markers to
  move its anchors while it stays put; hold **Ctrl** to snap to 8 reference pixels (anchors to 0.05).
  The Inspector's **Anchors** button picks a preset; **Shift**+click also moves the rect there. Rects
  a `UILayout` places only show their outline.
* `UIImage`: a filled rectangle: `texture` (empty = plain color), `color` (multiplies the
  texture), `corner_radius` and `border_width` (reference pixels; the border is inside the rect),
  `border_color`, `visible`, and `slice_border` (9-slice: `[left, top, right, bottom]` in texture
  pixels; those edges keep their size, one texture pixel per reference pixel, and only the middle
  stretches, so a framed panel texture fits any size; all 0 stretches the whole texture).
* `UILayout`: arranges the entity's children that have a `UIRect` (hidden ones leave the layout):
  `direction` (`"vertical"`, `"horizontal"` or `"grid"`, which fills rows left to right),
  `spacing` and `padding` (`[left, top, right, bottom]`, reference pixels), `main_alignment` (where
  the children sit along the direction when none fills) and `cross_alignment` (where a child that
  doesn't fill sits across it): `"start"`, `"center"` or `"end"`; `columns` for a grid. Each child's
  `UIRect.width_sizing` and `height_sizing` (`"fixed"`: its own `size`; `"fill"`: a share of the
  space left over) say how big it is; the layout ignores its anchors and offsets otherwise. A grid
  gives every column the same width and each row the height of its tallest child.
* `UIScroll`: scrolls and clips the entity's children (laid out in its rect, or by its `UILayout`):
  `vertical` (default) and `horizontal`, `scroll_position` (reference pixels, kept in range),
  `wheel_speed` (reference pixels per wheel notch), `show_scrollbars`, and read-only `content_size`
  and `viewport_size`. Nothing under it draws or can be pointed at outside its rect. The mouse wheel
  scrolls the scroll area under the pointer, and keyboard or gamepad focus moving to something out of
  view scrolls it in.

**Widgets.** Add one to an entity with a `UIRect`; each takes the pointer and keyboard and gamepad
focus, draws its own parts in the MageCap colors (navy, slate, off-white, orange), and uses the
entity's `Text` as its label. The editor's **Create > UI** menu makes each one ready to use.
* `UIButton`: colors the entity's `UIImage` by state: `normal_color`, `hover_color`,
  `pressed_color`, `disabled_color` (when its rect isn't interactable). Each comes from the canvas's
  theme (and the rect's style class) until it is set; reading one gives the color the button uses.
  `clear_colors()` goes back to the theme's. Clicks raise `clicked` like any UI.
* `UIToggle`: a check box at the left, its label beside it. `on`. A click or `ui_accept` flips it
  and raises `changed (on, entity)`.
* `UISlider`: a track with a knob. `min`, `max`, `step` (values snap to steps from `min`; 0 for any
  value) and `value` (kept between `min` and `max`, and snapped). Pressing on it, or dragging (even
  past its ends), moves the knob; while focused, `ui_left`/`ui_right` move it a step (a twentieth of
  the range when `step` is 0) instead of moving focus. Each change raises `changed (value, entity)`.
* `UIDropdown`: `options` (a list of text), `selected` (an index, or -1 for none; out of range raises
  `ValueError`) and read-only `selected_text`. A click or `ui_accept` opens a list of the options, one
  row each, under it (or above it when there is no room below). The open list takes every input:
  pointing highlights, a click or `ui_accept` picks, `ui_up`/`ui_down` move, and `ui_cancel` or a click
  outside closes it. Picking another option raises `changed (index, entity)`. The entity's `Text`
  always shows the selected option.
* `UITextInput`: a field the player types into. Its text is the entity's `Text` (whose font, size and
  color it uses), so `entity.text` reads it. `placeholder` (shown dimmed while it's empty),
  `multi_line` (Enter starts a new line, Ctrl+Enter submits, and long lines wrap at the field's
  width), `password` (a bullet per character, on one line, never copied) and `max_length`
  (characters; 0 for no limit; negative raises `ValueError`). A one-line field scrolls sideways
  instead.
  - A click focuses it and places the caret; dragging selects. While it has focus it takes the whole
    keyboard (the game sees no keys), and the caret blinks.
  - Keys: typing, Backspace and Delete (Ctrl: a word), Left/Right (Ctrl: by words; Shift: select),
    Home/End, Ctrl+A, Ctrl+C/X/V (the system clipboard), Up/Down between lines when multi-line, Tab
    to the next focusable UI, Escape to leave the field (`cancelled`, then `unfocused`).
  - Each edit raises `changed (text, entity)`. Enter raises `submitted (text, entity)` and keeps
    focus; in a multi-line field Enter starts a new line and Ctrl+Enter submits.

**Themes.** Widgets draw their own parts (check boxes, slider tracks and knobs, dropdown backgrounds,
arrows and lists, the focus ring, text selections) and button colors in their canvas's theme. A label
(`Text`) with `theme_color` on takes its color from the theme too, for its widget's state; without it,
the label keeps its own `color`, at half strength while its widget isn't interactable. The editor's
**Create > UI** widgets start with `theme_color` on and the theme's look.
* `UICanvas.theme`: the `.btheme` asset to use; `""` (or `"Builtin/MageCap.btheme"`) is the built-in
  MageCap look (navy, slate, off-white, an orange accent). Name the built-in theme to keep a UI looking
  the same whatever theme a game uses elsewhere.
* `UIRect.style_class`: one of the theme's named styles; `""` or a name the theme doesn't have uses its
  plain style. The built-in theme has `"primary"` (an orange button with navy text) and `"danger"` (a
  red button) for buttons. Classes can't change the focus ring, so focus always shows the same way.
* A `.btheme` file is JSON and lists only what it changes from the built-in theme. Colors are
  `"#RRGGBB"`, `"#RRGGBBAA"` or `[r, g, b, a]` from 0 to 1. Keys:
  - `Font`, `TextSize`, `TextColor`, `Accent` (focus ring, toggle check, slider fill, selection),
    `AccentLight` (a dragged knob, the selected dropdown option), `FocusRingWidth`, `FocusRingGap`.
  - `Panel`, `Button`, `Toggle`, `Slider`, `Dropdown`, `DropdownList` and `TextInput`, each with
    `CornerRadius` and states `Normal`, `Hover`, `Pressed`, `Focused` and `Disabled`, each with
    `Background`, `Border`, `BorderWidth` and `Text`. `Normal` sets every state first, so list only the
    states that differ after it. A slider's track is its `Background` and its knob its `Text` with its
    `Border`; a dropdown list's highlighted row is its `Hover` background.
  - `Classes`: `{ "danger": { "Toggle": { ... } } }`, each starting from the theme's own styles.
  Wrong values are reported in the Console and keep the built-in value. A theme that can't be read
  draws with the built-in one. Edited themes are picked up when reloaded (Content panel, or
  `bellows.editor.reload_asset`).

```json
{
	"Accent": "#00C8FF",
	"Toggle": { "CornerRadius": 2, "Normal": { "Background": "#101820", "Border": "#00C8FF" } },
	"Classes": { "danger": { "Dropdown": { "Normal": { "Background": "#B03030" } } } }
}
```

`Entity.value` is the shortcut: a toggle's `on` (`True`/`False`), a slider's `value`, a dropdown's
`selected`, or a text input's text (kept to what typing could enter: one line unless multi-line, at
most `max_length` characters), read or set, checked and snapped the same way. An entity with none of them raises
`RuntimeError` naming the entity. Setting a value from a script raises no `changed` event (only the
player's input does).

Unknown scale modes, layout directions, alignments and sizings raise `ValueError`, and so do edge lists
that aren't 4 numbers. Python vectors for UI use x and y; z is ignored.

**UI input.** Each frame, before scripts run, the UI looks at the pointer, the keyboard and the
gamepad, and the game only sees what the UI leaves it:
* The pointer uses the frontmost interactable UI under it that stops it: one with a visible
  `UIImage`, a focusable `UIRect` or a `UIScroll`. Text and bare rects let it through. While it is over such UI,
  or a press started on it, the mouse buttons and wheel read as up for the game. A press that started
  in the game stays the game's until the button is let go.
* Pressing and letting go on the same UI entity raises `clicked`; pressing on something focusable
  gives it focus, and pressing anywhere else takes focus away.
* `UIRect.focusable` lets an entity take keyboard and gamepad focus (and stops the pointer). While
  something has focus (or a modal canvas is shown), `ui_up`/`ui_down`/`ui_left`/`ui_right` move to
  the nearest focusable UI that way, `ui_next`/`ui_previous` go through it in hierarchy order,
  `ui_accept` raises `clicked` and `ui_cancel` raises `cancelled` on the focused entity. Without
  focus, the arrow keys stay the game's.
* `UICanvas.modal`: while a modal canvas is shown, every key, gamepad button and pointer press is the
  UI's, only its own UI can be pointed at or focused, and `ui_cancel` with nothing focused raises
  `cancelled` from the canvas itself (a pause menu's "back").
* `UIRect` read-only `hovered`, `pressed` and `focused`; `focus()` gives it focus (`False` when it
  cannot: not focusable, hidden, not interactable, or behind a modal canvas). A widget made this
  frame can't take focus until the UI has laid it out (the next frame), so `focus()` returns
  `False` until then; call it again from `on_update`. Interactable `False`
  on a rect turns off the pointer and focus for everything under it too.

UI events are sent to the scripts on the UI entity, then on each of its parents up to the root (through
entities that are not UI too), with the entity as the last value: `clicked (entity)`, `focused
(entity)`, `unfocused (entity)`, `cancelled (entity)`, and from widgets `changed (value, entity)` and
`submitted (text, entity)`.
Python receives them in `on_event`:

```python
class Menu(bellows.Script):
    def on_event(self, name, *values):
        if name == "clicked" and values[-1].name == "Play":
            bellows.load_scene("Scenes/Level1.bscene")
```

```python
hud = bellows.create_entity("HUD")
hud.add_component("UICanvas")
score = bellows.create_entity("Score", parent=hud)
rect = score.add_component("UIRect")
rect.anchor_min = rect.anchor_max = Vec3(1.0, 0.0, 0.0)   # the top-right corner
rect.pivot = Vec3(1.0, 0.0, 0.0)
rect.position, rect.size = Vec3(-24.0, 24.0, 0.0), Vec3(300.0, 60.0, 0.0)
label = score.add_component("Text")
label.text, label.size, label.alignment = "Score: 0", 32.0, "right"
```

### Physics
* `RigidBody`: `type` (`"Static"`, `"Dynamic"`, `"Kinematic"`), `mass`, `friction`, `restitution`,
  `linear_damping`, `angular_damping`, `gravity_scale`, `is_trigger`, `lock_rotation_x/y/z`,
  `continuous`; during play: `velocity`, `angular_velocity`, `add_force(Vec3)`,
  `add_impulse(Vec3)`, `add_torque(Vec3)`, `teleport(Vec3)`.
  Type, mass, locks and colliders apply when the body is created (start of play or when the entity
  is created); friction, restitution, damping and gravity scale apply immediately.
* `BoxCollider`: `half_extents`, `offset`. `SphereCollider`: `radius`, `offset`.
  `CapsuleCollider`: `radius`, `half_height` (cylinder part), `offset` (Y-up capsule).
  `MeshCollider`: `use_mesh_renderer` (triangle mesh from the MeshRenderer; static bodies only).
  Collider sizes are multiplied by the entity's scale. Several colliders on one entity form a
  compound shape.

### `ScriptComponent`
`path`, `class_name` (empty = first `bellows.Script` subclass in the file), `run_while_paused`
(keeps updating while the game is paused), `fields` (dict of overridden values),
`set_field(name, value)`, `instance` (the running object or `None`).
(The Python class is named `ScriptComponent` because `bellows.Script` is the script base class;
`"Script"` also works as a component name.)

### Audio
* `AudioSource`: `clip` (`.wav`, `.mp3`, `.flac`, `.ogg`), `volume`, `pitch`, `loop`,
  `play_on_start`, `spatial` (3D), `min_distance`, `max_distance`, `play()`, `stop()`, `pause()`,
  `is_playing`.
* `AudioListener`: `active`. Without an active listener the primary camera hears the scene.
* `AudioSource.bus`: the bus the source plays on (default `"Effects"`).

Audio buses group sounds like a mixer: `"Master"` (everything), `"Music"` and `"Effects"` (both feed
Master). Each play starts from the volumes in Project Settings → Audio buses. Unknown bus names
raise `ValueError`.

| Function | Description |
|----------|-------------|
| `play_sound(path, volume=1.0, bus="Effects")` | Plays a one-shot 2D sound; `False` if the clip can't be loaded. |
| `set_master_volume(volume)` / `get_master_volume()` | The Master bus volume (1 = normal, 0 = silent). |
| `bus_names()` | The audio buses: `["Master", "Music", "Effects"]`. |
| `set_bus_volume(bus, volume)` / `get_bus_volume(bus)` | A bus's volume (1 = normal, 0 = silent). |
| `set_bus_muted(bus, muted=True)` / `is_bus_muted(bus)` | Mutes or unmutes a bus; its volume is kept for unmuting. |

## Tweens

A tween moves a property smoothly over time: a door swinging open, a menu sliding in, a sound
fading out. Tweens run while the game plays.

```python
door_tween = bellows.tween_by(door, "rotation", (0, 90, 0), 1.0, "out")
bellows.tween(menu, "UIRect.position", (0, 0, 0), 0.4, "back out")
bellows.tween(music, "AudioSource.volume", 0.0, 2.0, "linear", delay=3.0)
```

| Function | Description |
|----------|-------------|
| `tween(entity, property, to, seconds, ease="in out", delay=0.0)` | Moves `property` from where it is to `to` over `seconds`, and returns a `Tween`. |
| `tween_by(entity, property, amount, seconds, ease="in out", delay=0.0)` | The same, adding `amount` to the property. A rotation turns by the whole amount, so `(0, 720, 0)` spins twice. |
| `stop_tweens(entity, property="")` | Stops the entity's tweens (only those of `property` when it is given); returns how many it stopped. |

- **Properties** are named as scripts name them. On the entity: `position`, `local_position`,
  `rotation` and `scale`. On a component: `"Component.property"` for any of its number or vector
  properties, such as `"UIImage.color"`, `"Text.color"`, `"UIRect.position"`, `"UIRect.size"`,
  `"AudioSource.volume"`, `"PointLight.intensity"` or `"Camera.fov"`. A name that can't be tweened
  raises `ValueError` listing the ones that can.
- **Values** fit the property: a number, a `Vec3` (or a list of 3 numbers), a `Vec2` (or 2 numbers),
  and 4 numbers for a color, such as `Vec4(1, 1, 1, 0)` or `[1, 1, 1, 0]`.
- **Easing styles** are ForgeScript's `ease()` styles: `"linear"`, `"in"`, `"out"`, `"in out"`,
  `"sine in"`, `"sine out"`, `"sine in out"`, `"back in"`, `"back out"`, `"bounce out"` and
  `"elastic out"`.
- `seconds` and `delay` are from 0 to 1,000,000; `seconds=0` sets the value at once. A tween reads
  its starting value when its delay ends, so tweens with growing delays follow on from each other.
- **One at a time:** a new tween of the same entity and property replaces the running one, which
  stops where it is. Euler-angle properties (`rotation`, `Transform.rotation`,
  `Transform.rotation_degrees`) turn the shortest way.
- Tweens run in game time before scripts' `on_update`, so `set_paused` holds them, except those with
  `while_paused` set (for pause menus). They keep going through `load_scene` for kept entities; a
  destroyed entity or a removed component ends its tween quietly. A scene can have 10,000 running.
- Tweening a dynamic `RigidBody`'s position fights the physics; make the body kinematic.
- When a tween reaches its end, every script gets the event **`tween finished`** with the entity,
  the property and the tween's `id` (`on_event`, or ForgeScript's `when tween finished with ...`).
  Stopped tweens and those repeating forever send none.

`Tween` (what `tween` and `tween_by` return):

| Member | Description |
|--------|-------------|
| `id`, `entity`, `property` | What it was made for (still readable once it has ended). |
| `running` | True until it ends; paused, or waiting out its delay, counts as running. |
| `finished` | True once it reached its end; false when it was stopped or replaced. |
| `progress` | 0 to 1 over every repeat (the current run when repeating forever). |
| `repeat` | How many more runs after the first: 0, a count, or -1 for forever. |
| `yoyo` | Every other run goes back the way it came. |
| `while_paused` | Keeps moving while the game is paused. |
| `stop()`, `pause()`, `resume()` | `stop` sends no `tween finished`. Each returns `False` once the tween has ended. |
| `complete()` | Jumps to the end, repeats included; `tween finished` follows with the next frame. |

In ForgeScript, wait for a tween with `wait until`:

```forge
when clicked
	set slide to tween(self, "UIRect.position", vector(0, 0, 0), 0.4, "back out")
	wait until slide.finished
	say "The menu is in"
end
```

## Materials

```python
gold = bellows.Material("Gold", color=(1.0, 0.77, 0.34, 1.0), metallic=1.0, roughness=0.2)
entity.get_component(bellows.MeshRenderer).material = gold
gold.save("Materials/Gold.bmat")          # Optional: write it into the project
```

`Material(name="Material", color=None, metallic=0.0, roughness=0.5)` creates a runtime material;
`Material.load(path)` opens a `.bmat`. Properties: `name`, `path`, `color`, `metallic`,
`roughness`, `emissive` (`Vec3`), `emissive_intensity`, `alpha_mode` (`"Opaque"`, `"Mask"`,
`"Blend"`), `alpha_cutoff`, `double_sided`, `normal_strength`, `uv_tiling`, `uv_offset`, and the
texture slots `albedo_map`, `normal_map`, `metallic_roughness_map` (glTF layout: roughness in G,
metallic in B), `roughness_map` and `metallic_map` (grayscale, as downloaded texture sets have them),
`occlusion_map`, `emissive_map` (asset paths; `None` clears). Texture files can be PNG, JPG, TGA,
BMP or OpenEXR (`.exr`); a `.hdr` image works too. As material maps, EXR and HDR images are stored
at 8 bits per channel (values above 1 are clipped). Sky Light environments take `.hdr` or `.exr`. `save(path)` writes
a `.bmat`.

## Editor automation (`bellows.editor`)

Inside the editor (Console, `--run-script` automation and the agent server) the additional module
`bellows.editor` controls the editor itself: projects, scenes, play mode, undo, selection,
screenshots and exporting. See [EditorAutomation.md](EditorAutomation.md).

## Using the API from ForgeScript

Every function, class and property on this page that is not marked Python-only is also available
to [ForgeScript](ForgeScript/LanguageGuide.md), with the same names:
`find_entity("Player").position`, `self.add_component("RigidBody")`, `body.add_force(vector(0, 5, 0))`,
`Material("Gold", [1, 0.8, 0])`, `Material.load("Materials/Gold.bmat")`.

The Python example at the top of this page, written in ForgeScript (`Scripts/Player.forge`):

```forge
set speed to 5.0                             # Shown and editable in the Inspector

when update with delta
	set direction to vector()
	if is_key_down("W") then set direction to direction + my forward end
	if is_key_down("S") then set direction to direction - my forward end
	self.translate(direction * speed * delta)
end
```

### ForgeScript entity scripts

A `.forge` file attached with a Script component is one script for that entity (there is no class
to choose). Each entity gets its own copy of the script, with `self` set to the entity; `my name`
is short for `self.name`. The script's top level runs when the entity's script starts, then the
engine sends these events, which the script handles with `when`:

| Python callback | ForgeScript event | When |
|-----------------|-------------------|------|
| `on_create(self)` | `when created` | Once, after every script in the scene has started. |
| `on_update(self, dt)` | `when update with delta` | Every frame; `delta` is in seconds. |
| `on_fixed_update(self, dt)` | `when physics update with delta` | Every physics step. |
| `on_destroy(self)` | `when destroyed` | When the entity is destroyed or play stops. |
| `on_collision_enter(self, other)` / `on_collision_exit` | `when collided with other` / `when collision ends with other` | Started/stopped touching `other`. |
| `on_trigger_enter(self, other)` / `on_trigger_exit` | `when entered by other` / `when exited by other` | `other` entered/left this trigger, or this entity entered/left a trigger. |
| `on_reload(self)` | `when reloaded` | After the script's file was saved during play and its new code took over (`when created` does not run again). |

- **Inspector fields.** Top-level lines that set a name to a plain value appear in the Inspector:
  numbers, text, `true`/`false`/`yes`/`no`, `vector(x, y)`, `vector(x, y, z)` and colors
  (`[r, g, b]` or `[r, g, b, a]`). Names starting with `_` stay hidden. Values edited in the
  Inspector replace the script's own values after the top level has run and before `when created`.
- **Decimal or whole number?** How you write the starting value decides the Inspector field:
  `set speed to 4.0` makes a decimal field (you can type 4.5), while `set lives to 4` makes a
  whole-number field (4, 5, 6...). Write `.0` when a setting should take decimals.
- **Waiting.** `wait` and `start` work in every handler (`wait 2 seconds`, `wait until ready`);
  waiting tasks continue each frame.
- **Talking to other scripts.** `trigger door opened with self` reaches the matching `when` handlers
  of every running script, including the sender's, right away. It also goes to the engine's script
  event hub (`EmitScriptEvent` in `Scripting/ScriptApi.h`), so Python scripts receive it in
  `on_event(self, name, *values)`, and `bellows.emit(name, *values)` from Python reaches ForgeScript's
  `when` handlers. Values cross
  languages as nothing, true/false, numbers, text, vectors, entities, lists and maps with text keys;
  anything else (a function, a kind's object) arrives as nothing in the other language, with one
  Console warning per event name. Events that keep answering each other stop with an error after
  64 levels, or after 10,000 events caused by one event.
- **Other files.** `use "Scripts/Helpers"` loads `Assets/Scripts/Helpers.forge`.
- **Errors** appear in the Console as `Scripts/Player.forge line 12: <message>`; a script with a
  mistake is not started, and one that fails 10 times is switched off, as in Python. A frame that
  runs a loop without any `wait` for too long is stopped with an error instead of freezing the game.
- **Hot reload.** Saving any `.forge` file during play reloads every ForgeScript script (helpers
  loaded with `use` are shared). Each script's top level runs again in the new version, so a line
  there that spawns entities spawns them again. Script variables keep their current values, except
  constants and variables whose kind of value changed (`2` became `"fast"`). Inspector values fill
  only settings the new version adds. Waiting tasks of the old version stop, so restart them in
  `when reloaded`. A version with mistakes is not used: the mistakes go to the Console and the old
  code keeps running. Scripts are also read again every time play starts.
- **Engine phrases.** Some engine calls also read as English, in ForgeScript only:

  | Phrase | Same as |
  |--------|---------|
  | `save screenshot` | `save_screenshot()` |
  | `save screenshot to (path)` | `save_screenshot(path)` |
  | `user folder` | `user_folder()` |
  | `show (widget)` | `widget.visible = True` |
  | `hide (widget)` | `widget.visible = False` |
  | `enable (widget)` | `widget.interactable = True` |
  | `disable (widget)` | `widget.interactable = False` |
  | `focus (widget)` | `widget.focus()`, giving back true or false |
  | `pause the game` | `set_paused(True)` |
  | `resume the game` | `set_paused(False)` |
  | `the game is paused` | `is_paused()` |
  | `keep (entity) across scenes` | `entity.keep_across_scenes = True` |
  | `move (entity) to (position) over (seconds) seconds` | `tween(entity, "position", position, seconds)` |
  | `move (entity) by (offset) over (seconds) seconds` | `tween_by(entity, "position", offset, seconds)` |
  | `turn (entity) to (angles) over (seconds) seconds` | `tween(entity, "rotation", angles, seconds)`, in degrees |
  | `turn (entity) by (angles) over (seconds) seconds` | `tween_by(entity, "rotation", angles, seconds)` |
  | `scale (entity) to (size) over (seconds) seconds` | `tween(entity, "scale", size, seconds)`; a number scales all three axes |
  | `fade (entity) to (opacity) over (seconds) seconds` | `tween` of `"UIImage.color"` (or `"Text.color"`) to the same color with that alpha, 0 to 1 |
  | `push (entity) by (impulse)` | `entity.rigid_body.add_impulse(impulse)` |

  Every tween phrase also takes `second` (`over 1 second`) and ends with an optional
  `with (style)` for the easing, which is `"in out"` otherwise: `move door to p over 2 seconds with
  "bounce out"`. They give back the `Tween`, so a script can wait for it (example below). A
  script's own `move` function or `fade` variable still works, but where it is visible, it hides the
  phrase that starts with the same word.

  Phrases give back the same values, so `set shot to save screenshot to "bug.png"` works. The widget
  phrases are written without brackets (`show pause_menu`): `show (pause_menu)` with brackets is still
  ForgeScript's `show` function, which prints the value. A non-entity gets "'hide' needs a UI entity,
  like: hide find_entity("Pause Menu")".

```forge
when clicked
	set slide to move self to vector(0, 2, 0) over 0.5 seconds with "back out"
	wait until slide.finished
	fade self to 0 over 1 second
end
```

How values look in ForgeScript:

| Python | ForgeScript |
|--------|-------------|
| `Vec3` | `vector(x, y, z)` (a list of 3 numbers is accepted too) |
| `Vec2` | a vector with `z = 0` (a list of 2 numbers is accepted too) |
| colors (`Vec4`) | a list `[r, g, b, a]`; `[r, g, b]` or a vector means opaque |
| `Quat` | a list `[w, x, y, z]` |
| `bellows.Key.W`, `MouseButton.Left` | names: `"W"`, `"space"`, `"left"` |
| entity ids (`Entity.id`) | text, because a number cannot hold every 64-bit id exactly |
| `None` | `nothing` |
| `RigidBody`, `"RigidBody"` (component arguments) | `"RigidBody"` |

Python-only: `Vec2`/`Vec3`/`Vec4`/`Quat` classes and their operators (ForgeScript has its own
`vector`), the `Key` and `MouseButton` enums, `lerp`, `clamp` and `log*` (ForgeScript has built-in
`lerp`, `clamp`, `say` and `print`), `Entity.script`, and `ScriptComponent.fields`, `set_field` and
`instance`. Engine errors become ForgeScript errors with the same message, so `try ... catch` works.

