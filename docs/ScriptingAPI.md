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
`sys.path`, so scripts can import each other (`from Scripts.Helpers import clamp01`). Edited
scripts are reloaded the next time play starts.

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
| Vectors | Anywhere a `Vec2/Vec3/Vec4` is expected you may pass a tuple or list, e.g. `entity.position = (0, 1, 0)`. |
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

**Exposed fields.** Class attributes with a type annotation of `bool`, `int`, `float`, `str`,
`Vec2`, `Vec3` or `Vec4` appear in the Inspector. Values edited there are saved with the scene and
assigned to the instance before `on_create`.

`Script` also offers the shortcuts `self.transform` and `self.get_component(component)`.

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
| `time()` | Seconds since play started. |
| `frame()` | Frames since play started. |
| `is_key_down(key)` / `is_key_pressed(key)` / `is_key_released(key)` | Held / went down this frame / went up this frame. `key` is a `Key` value (`Key.W`, `Key.Space`, `Key.LeftShift`, `Key.F1`, `Key.D0`…`Key.D9`, `Key.Up`, …) or its name as a string (`"W"`). |
| `is_mouse_down(button=MouseButton.Left)` / `is_mouse_pressed(...)` / `is_mouse_released(...)` | Mouse buttons: `MouseButton.Left`, `Right`, `Middle`, `Button3`, `Button4`. |
| `mouse_position()` | `Vec2` in viewport pixels, top-left origin. |
| `mouse_delta()` / `scroll_delta()` | Movement this frame (`Vec2`). |
| `viewport_size()` | Size of the game view in pixels (`Vec2`). |
| `is_action_down(action)` / `is_action_pressed(action)` / `is_action_released(action)` | Input actions by name (`"jump"`): down while any input bound to the action is held; pressed / released on the frame the action starts / ends. Unknown actions raise `ValueError`. |
| `action_names()` | The project's input actions, sorted by name. |
| `is_gamepad_down(button)` / `is_gamepad_pressed(button)` / `is_gamepad_released(button)` | Buttons of the first gamepad by name: `"A"`, `"B"`, `"X"`, `"Y"`, `"LeftBumper"`, `"RightBumper"`, `"Back"`, `"Start"`, `"Guide"`, `"LeftThumb"`, `"RightThumb"`, `"DPadUp"`, `"DPadRight"`, `"DPadDown"`, `"DPadLeft"` (Xbox names; any capitalization). Unknown names raise `ValueError`. |
| `gamepad_connected()` | True while a gamepad is connected. |
| `save_screenshot(path="", max_width=1920)` | Saves what the player sees (the game camera's image, never editor overlays) and returns the file's full path. PNG, or JPEG (quality 85) for `.jpg`/`.jpeg`. `""` names it `Screenshots/<scene>-<date>-<time>.png`; relative paths are inside `user_folder()`; missing folders are created. A wider image is scaled down to `max_width` (0 keeps the full size). Raises `ValueError` before anything is drawn, for other formats, or when the file can't be written. |
| `user_folder()` | The game's own writable folder for saves, screenshots and reports: `%APPDATA%\MageCapBellows\Games\<project name>` on Windows, `~/.local/share/MageCapBellows/Games/<project name>` on Linux (`$XDG_DATA_HOME` if set; `BELLOWS_USER_DATA_DIR` overrides the base folder). |
| `log(*values)` / `log_warning(*values)` / `log_error(*values)` | Write to the Console (values are joined with spaces). `print()` also goes to the Console. |

In the editor, game input is only delivered while the Viewport has focus during play.

**Input actions** let a game ask for "jump" instead of naming keys, so the same script works with a
keyboard, a mouse and a gamepad, and players' controls can change without touching code. Set them
up in **Settings → Project → Input actions**: each action has a list of inputs (`Key:Space`,
`Mouse:Left`, `Gamepad:A`). New projects start with `move_left`, `move_right`, `move_forward`,
`move_back`, `jump`, `interact` and `pause` (WASD, the arrow keys, the D-pad, Space, E, Escape and
the matching gamepad buttons); **Add default actions** adds any that are missing. The project file
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
| `load_scene(path)` | Switches to another scene at the end of the frame. Returns `False` if it does not exist. |
| `quit(exit_code=0)` | Quits the game (in the editor: stops play mode). |
| `get_gravity()` / `set_gravity(Vec3)` | Scene gravity (default `(0, -9.81, 0)`). |
| `raycast(origin, direction, max_distance=1000)` | First physics collider hit, as a `RaycastHit` (`entity`, `point`, `normal`, `distance`) or `None`. Play mode only. |

### `Entity`

| Member | Description |
|--------|-------------|
| `id` | Persistent 64-bit ID (stable across saves). |
| `valid` / `bool(entity)` | `False` once destroyed. |
| `name` | Read/write. |
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
`path`, `class_name` (empty = first `bellows.Script` subclass in the file), `fields` (dict of
overridden values), `set_field(name, value)`, `instance` (the running object or `None`).
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
metallic in B), `occlusion_map`, `emissive_map` (asset paths; `None` clears). `save(path)` writes
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
  of every running script, including the sender's.
- **Other files.** `use "Scripts/Helpers"` loads `Assets/Scripts/Helpers.forge`.
- **Errors** appear in the Console as `Scripts/Player.forge line 12: <message>`; a script with a
  mistake is not started, and one that fails 10 times is switched off, as in Python. A frame that
  runs a loop without any `wait` for too long is stopped with an error instead of freezing the game.
- **Reloading.** Scripts are read again every time play starts, so edits apply on the next Play.

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

