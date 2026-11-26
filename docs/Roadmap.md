# MageCap Bellows roadmap

Where the engine stands today, what it needs to compete with Godot, and how 2D games, the
ForgeScript language and MageCap Forge fit in. Written November 2026 against Godot 4.7.2 (current
stable) and Godot 4.8 (beta); updated November 24, 2026, after 0.6.0 shipped, with Ron's
priorities (section 2). This is a planning document: update it as milestones land.

## 1. Where Bellows stands against Godot

Godot is a mature engine with a large contributor base: 4.7 alone shipped 1,265 fixes from 309
contributors, and 4.8 has passed 1,500 fixes since 4.7. Bellows cannot out-feature it in the short
term. It can compete by being excellent at a focused set of things and by offering what Godot does
not: an editor built to be driven by AI agents, a natural-language scripting language, and tight
integration with MageCap Forge.

| Area | Godot 4.7 / 4.8 | Bellows 0.6.0 | Gap |
|------|-----------------|---------------|-----|
| 3D rendering | Forward+/Mobile/Compatibility renderers, SDFGI/VoxelGI/lightmaps, AreaLight3D, HDR output, SSR, volumetric fog, decals, texture streaming (4.8), contact shadows (4.8) | PBR, IBL, directional/point/spot/sky lights, cascaded soft shadows, SSAO, bloom, tonemapping, FXAA, transparency, unlit materials, texture filtering and wrap modes, frustum culling | Large: GI, area lights, SSR, fog, decals, particles, LOD, occlusion culling, texture streaming, HDR output, TAA/upscaling |
| 2D | Full 2D engine: sprites, TileMapLayer, 2D lights/shadows, 2D physics, Line2D, polygons, parallax, one-way collisions | None (screen-space game UI only) | Complete subsystem missing (section 3) |
| Game UI | Control nodes, containers, themes, RichTextLabel, auto-fit fonts (4.8), accessibility (AccessKit) | Canvases, anchored rects, panels, images, buttons, toggles, sliders, dropdowns, text inputs, scroll views, layouts, MSDF text, Main Menu and HUD templates, a UI view in the editor | Themes, rich text, accessibility |
| Animation | AnimationPlayer/Tree, skeletal animation, blend spaces, IK (4.6), tweens with signal waits (4.7) | Skeletal animation (GPU skinning), clip crossfades, Animator with preview, tweens | Animation timeline, state machines, blend spaces, IK |
| Physics | Jolt (default since 4.6), Godot Physics 2D | Jolt 3D, triggers, raycasts | 2D physics, joints, character controller, shape casts |
| Navigation | Navmesh, agents, avoidance | Recast/Detour navmesh baking, path queries, Nav Agents with avoidance | Navigation links, runtime rebaking |
| Audio | Buses, effects, streams, spatial audio | 2D/3D sources, listener, one-shots, streaming of large files, buses (volume, mute) | Bus effects |
| Scripting | GDScript (with LSP, debugger, refactoring), C#, GDExtension (C/C++/Rust) | ForgeScript and Python 3.13 on one API, hot reload, script events, LSP, refactoring, a debugger with conditional breakpoints, log points and Watch | Native plugin API |
| Editor | Docks, scene tabs, inspector, Asset Store (4.7), runtime 2D manipulation (4.8), symbol renaming (4.8), profiler, remote scene tree | Docked editor, inspector, content browser, console, undo, play mode, prefabs, Project Manager, built-in Script Editor, agent server | Multiple scenes, profiler, plugin API, asset store |
| Platforms | Windows, macOS, Linux, Android, iOS, Web, XR (Android XR, Steam Frame), visionOS (4.8) | Windows, Linux (Vulkan); export for the editor's own platform only | Mobile and Web (planned, section 2), then macOS, consoles, XR |
| Other | Multiplayer, localization (pseudolocalization in 4.8), input map, resources, signals | Prefabs, JSON scenes, input actions, export | Networking, localization |
| AI automation | None built in | Agent server, `bellows.editor` API, CLI, skills | Bellows leads |

## 2. Ron's priorities (decided November 24, 2026)

After 0.6.0, Ron set what Bellows needs to compete with the other engines:

1. **Platforms:** Windows, Linux, mobile and Web now. macOS and consoles come later.
2. **2D and 3D:** Bellows must make 2D games as well as 3D ones (section 3).
3. **Particles and decals:** wanted (section 7, 0.7).
4. **Performance tools:** a must: profilers first, then the systems that keep large scenes fast.
5. **Lighting and quality:** wanted: global illumination, reflections, fog, area lights, temporal
   anti-aliasing and upscaling, HDR output.
6. **Ecosystem** (documentation site, tutorials, community): later, once Bellows can compete with
   the other engines on features.

## 3. Supporting 2D games

2D is a parallel engine inside the engine. Recommended order:

1. **Sprite renderer** - a batched quad renderer (one draw per texture atlas) with an orthographic
   2D camera, sorting layers and order-in-layer, nine-slice sprites, and pixel-perfect mode.
   Components: `SpriteRenderer`, `Camera2D` (follow, limits, zoom, shake).
2. **Text** - MSDF font atlases so text stays sharp at any size; shared with the game UI.
3. **2D physics** - Box2D v3 (MIT) as a second physics world: rigid bodies, box/circle/capsule/
   polygon colliders, one-way platforms, joints, raycasts, triggers.
4. **Tilemaps** - tilesets with per-tile collision, auto-tiling (terrain rules), animated tiles,
   multiple layers, and a tile painting mode in the editor.
5. **Animation** - sprite-sheet animation clips (`SpriteAnimator`) and a general property
   animation timeline that 3D can share.
6. **2D lighting** - point/spot lights for sprites with normal maps and soft shadows from
   occluders; particles (CPU first, GPU later).
7. **Editor 2D mode** - a 2D viewport with snapping, rulers and guides, a sprite slicer, and a
   tilemap painter. The same scene format holds 2D and 3D entities; `Transform` stays 3D (Z is the
   draw depth), which keeps one entity model.
8. **Templates** - platformer, top-down, puzzle (Tetris-style) starter projects, plus a FeatureTest2D
   project that tests the whole 2D API like FeatureTest does for 3D.

## 4. Becoming an engine an AAA studio could use

Large studios rarely switch engines; the realistic path is indie and AA studios first, while
building the foundations bigger teams require:

* **Performance architecture** - a job system (multi-threaded scene update, culling, physics and
  asset loading), GPU-driven rendering with frustum/occlusion culling and LOD, texture and mesh
  streaming, large-world (double-precision) coordinates.
* **Direct3D 12 on Windows** - NVRHI already supports D3D12, so a second backend is mostly
  shader compilation (DXC/Slang) and testing. Godot 4.6 made D3D12 its Windows default.
* **Asset pipeline** - an import step (source files to optimized, cached runtime formats), stable
  asset IDs instead of paths, dependency tracking, a derived-data cache shareable across a team, and
  packed archives for shipping.
* **Team workflows** - prefab variants and overrides, nested prefabs, text formats that merge
  cleanly in Git, Git LFS/Perforce awareness, multi-user scene locking.
* **Extensibility** - a stable C API / plugin SDK (like GDExtension) for native modules, and an
  editor plugin API (custom panels, inspectors, importers, gizmos).
* **Tooling** - CPU/GPU profiler (Tracy is BSD-licensed), memory tracker, frame debugger,
  script debugger with breakpoints, crash reporting with symbol upload.
* **Platforms** - Android (Vulkan, which NVRHI already uses) and Web (a WebGPU backend, which NVRHI
  doesn't have, plus a Web build of Python) come first, per Ron's priorities. iOS needs Metal
  (MoltenVK) and Apple's tools, which run only on a Mac, so it lands with macOS later. Console SDKs
  are under NDA: studios typically license them, so console support means a private,
  partner-only branch.
* **Release discipline** - semantic versioning, LTS branches, migration guides, CI on every
  platform (already started in `.github/workflows/ci.yml`), a public issue tracker, and a real
  documentation site.

A business decision belongs here too: an open-source license (Godot is MIT) attracts contributors
and trust; a commercial license with source access funds development. Decide before inviting
outside contributors.

## 5. ForgeScript

Goal: ForgeScript is to Bellows what GDScript is to Godot - the flagship, easiest way to write game
logic, with an English-like style. Python stays fully supported alongside it, the way Godot offers
both GDScript and C#: each script file is either `.forge` or `.py`, and a game can mix them.

### Where it stands

The language itself is done and lives in `ForgeScript/` (guide: `docs/ForgeScript/LanguageGuide.md`):
variables and constants, text with `{interpolation}`, lists, maps, vectors, functions and closures,
**phrases** (`phrase heal (target) by (amount)`), **kinds** (classes) with inheritance, events
(`when hit by damage`, `trigger`), tasks (`wait 2 seconds`, `wait until ready`, `start patrol()`),
`try`/`catch`/`fail`, `use` for modules, a garbage collector, friendly compile and runtime errors,
the `forgescript` command line tool (runner, checker, interactive prompt) and a VS Code grammar.
It is covered by `ForgeScriptTests` (every test script also runs with the garbage collector on every
instruction) and is clean under AddressSanitizer and UndefinedBehaviorSanitizer.

ForgeScript now runs in the engine: `.forge` files attach to entities with the Script component,
receive the lifecycle and physics events, show their top-level settings in the Inspector, report
errors with file and line in the Console, and are edited in the editor's Script Editor.
`FeatureTest.forge` checks the engine API from ForgeScript in the FeatureTest scene. What is left is
the ForgeScript-only engine phrases (item 3 below), so scripts can read like the target:

```forge
# The target: an entity script written in ForgeScript.
set speed to 5

when update with delta
	if key "W" is down then
		move self forward by speed * delta
	end
end

when collided with other
	if other's name is "Spikes" then
		play sound "Audio/Hurt.wav"
		self.destroy()
	end
end
```

### Architecture (decided November 2026)

1. **Own virtual machine.** ForgeScript compiles to bytecode for its own VM (`ForgeScript/Source`),
   not to Python. That is what makes `wait`, tasks, events and plain-English runtime errors work
   naturally, and lets games that use only ForgeScript ship without a Python runtime later.
2. **One description of the engine API, two languages.** Today the API is written once, by hand,
   for pybind11. It is now a shared **script API description** (`Scripting/ScriptApiDefinition.h`):
   every type, property, method and function is described once with typed C++ lambdas, and two
   binders turn it into the Python module and the ForgeScript bindings. Names are the
   same in both languages (`entity.transform.position`, `rigid_body.add_force(...)`), so docs,
   examples and `FeatureTest` coverage carry over, and a new engine feature appears in both
   languages from one change.
3. **ForgeScript extras on top.** Engine phrases (`move (thing) toward (target) by (amount)`,
   `play sound (path)`, `key (name) is down`) are thin wrappers registered for ForgeScript only.
4. **Engine integration.** `ScriptComponent` accepts `.forge` or `.py`; one ForgeScript module per
   scripted entity with `self` bound to the entity; lifecycle callbacks map to events
   (`on_create` -> `when created`, `on_update(dt)` -> `when update with delta`,
   `on_fixed_update(dt)` -> `when physics update with delta`, `on_destroy` -> `when destroyed`,
   collisions and triggers -> `when collided with other` / `when collision ends with other` /
   `when entered by other` / `when exited by other`); script variables become Inspector fields;
   errors go to the Console with file and line; scripts recompile when edited.
5. **Tests.** Registry unit tests, ForgeScript binding tests, and a `FeatureTest.forge` that mirrors
   `FeatureTest.py`, so the whole API is proven from both languages.

### Next steps

1. Done: the script API is described once (`Scripting/ScriptApiDefinition.h`); the Python module
   is built from it without any change to the Python API, and the ForgeScript binder
   (`Scripting/ForgeScriptBindings.cpp`) exposes the same API to ForgeScript.
2. Done: `.forge` support in `ScriptEngine` and the Script component (`Scripting/ForgeScriptRuntime`),
   lifecycle and physics events, Inspector fields from top-level `set` lines
   (`ForgeScript::FindScriptVariables`), `FeatureTest.forge`, and `docs/ScriptingAPI.md` in both
   languages.
3. Done: editor support (the Script Editor with ForgeScript highlighting and checking, new
   ForgeScript scripts from templates, Attach Script). Next: the ForgeScript engine phrases
   (`move (thing) toward (target) by (amount)`, `play sound (path)`, `key (name) is down`).
4. Language additions that make game code easier: "did you mean ...?" suggestions for misspelled
   names, warnings for variables set but never used, default parameter values, a `match`
   statement, task handles (`set t to start patrol()`, `stop t`), whole numbers and bit flags,
   JSON, `finally`, enums.
5. Tooling: a ForgeScript Language Server (completion, hover docs, go-to-definition, diagnostics,
   rename), the TextMate grammar in the Bellows script editor, a formatter, and a debugger through
   the Debug Adapter Protocol.

## 6. MageCap Forge integration

Bellows already has the hard part: a local, token-protected agent server that can run code,
read logs and take screenshots (`docs/EditorAutomation.md`). Forge integration builds on it:

1. **Forge Link for Bellows** - the sixth Forge Link engine. Forge connects to the running
   editor's agent server (port and token from `AgentSession.json`), so no add-on install is needed
   in Bellows. Engine description in Forge's project picker: "MageCap Bellows - MageCap Games'
   own 3D engine with Python and ForgeScript."
2. **Assay (playtests)** - a "Report bug" action in Bellows (and a hotkey in exported games)
   that sends the scene, selected entity, camera position, recent log lines and a screenshot
   (`capture_window`) to Assay; Assay links back so a click opens Bellows at that spot.
3. **Chisel and Whetstone (code)** - ForgeScript support through the ForgeScript Language
   Server: highlighting, completion and real diagnostics, with Whetstone's hints built on the
   compiler's error messages (works offline, no AI needed). Python scripts get the same through a
   standard Python language server.
4. **Anvil (tasks)** - cards can reference entities, scenes and scripts by Bellows IDs; Forge can
   ask Bellows to focus them.
5. **Ember (AI)** - Ember drives Bellows through the agent server the same way Claude does today
   (`.claude/skills/bellows-make-game`), so "make me a Tetris clone" works from inside Forge.
6. **Sightglass (stats)** - an optional analytics hook in exported games (events, session
   length) that Sightglass can import.

## 7. Sequence

Shipped:

| Release | Contents (from `docs/ReleaseNotes`) |
|---------|----------|
| 0.3 | ForgeScript scripts in the engine, the Script Editor, materials (0.3.0); ForgeScript 1.2 (0.3.1); MSDF text, the first game UI, saving, crash reports (0.3.2) |
| 0.4 | The UI view and other editor UI tools, more game UI, pausing the game, ForgeScript 1.5, security fixes |
| 0.5 | Keeping entities across scenes, turning models that face the other way, safer delete and rename in the Content panel, damaged files no longer crash, ForgeScript 1.6 |
| 0.6 | Tweens, tags, skeletal animation, navigation, `field changed`, camera shake, data files, glTF samplers and unlit materials, Script Editor refactoring and a debugger with conditional breakpoints and Watch, ForgeScript 1.7 |

Planned, from Ron's priorities (section 2). Ron confirmed this order, with iOS waiting for macOS, on November 24, 2026 ("Yes the order you have is perfect."):

| Release | Contents |
|---------|----------|
| 0.7 | **Performance tools and effects:** a CPU and GPU profiler (Tracy is BSD-licensed) with frame stats in the editor; particles (CPU first, GPU later) and decals |
| 0.8 | **2D:** sprites, Camera2D, Box2D physics, tilemaps, sprite animation, a 2D editor mode and 2D templates (section 3) |
| 0.9 | **Lighting and quality:** fog, screen-space reflections, area lights, TAA and upscaling, HDR output, then global illumination; plus LOD and occlusion culling |
| 0.10 | **Android** on Vulkan, with touch input and Android export |
| 0.11 | **Web** through a WebGPU backend, with Web export |
| 1.0 | Stable API, migration guides, LTS policy; then the ecosystem (documentation site, tutorials, community) |
| Later | macOS and iOS (Metal through MoltenVK), consoles, multiplayer, editor plugin API, multiple open scenes |

### Editor redesign

Ron chose design F, "Bellows: its own personality", on November 25, 2026 ("Awesome, we are going
with F"), on the "MageCap Bellows Editor Redesign" canvas. Its rules: MageCap's navy surfaces,
shared with MageCap Forge; the ember accent only for the selection and Play, everything else neutral;
a hovered row never looks like a selected one; plain wording ("Running", not "Stoked"); "MageCap
Bellows" as the name everywhere. The work lands in steps, alongside the 0.7 features, each one
tested and shown to Ron against the design:

1. **Theme tokens** (0.7): the navy palette, neutral toggles and primary buttons, the warm
   selection, rounder fields and tabs, the "MageCap Bellows" title.
2. **Title bar and icon rail:** the search box (Ctrl+K), the project breadcrumb with the unsaved
   dot, Export game, the ember Play button; Scene, Script, Animation, Profiler and Content on a
   rail at the left.
3. **Panels:** rounded panels with gaps and grips between them, the panel headers, the warm
   selection in the Hierarchy and Content panels.
4. **Viewport:** one floating toolbar (tools, snapping, space, Play), the orientation gizmo, the
   CPU/GPU readout, a faint selection glow with an off switch in the View menu; it also moves the
   toolbar off the game's own UI.
5. **Inspector:** compact X/Y/Z drag fields, the uniform-scale lock, switches, and the ForgeScript
   card with the script inline (Open in Script, Debug).
6. **Content and status bar:** asset previews with type badges; the frame-time graph and plain
   status text.

## Sources

* [Godot 4.7 release page](https://godotengine.org/releases/4.7/)
* [Godot 4.7.2 maintenance release](https://godotengine.org/article/maintenance-release-godot-4-7-2/)
* [Godot 4.8 beta 1](https://godotengine.org/article/dev-snapshot-godot-4-8-beta-1/)
* [Godot 4.6 release page](https://godotengine.org/releases/4.6/)
