# MageCap Bellows roadmap

Where the engine stands today, what it needs to compete with Godot, and how 2D games, the
ForgeScript language and MageCap Forge fit in. Written November 2026 against Godot 4.7.2 (current
stable) and Godot 4.8 (beta). This is a planning document: update it as milestones land.

## 1. Where Bellows stands against Godot

Godot is a mature engine with a large contributor base: 4.7 alone shipped 1,265 fixes from 309
contributors, and 4.8 has passed 1,500 fixes since 4.7. Bellows cannot out-feature it in the short
term. It can compete by being excellent at a focused set of things and by offering what Godot does
not: an editor built to be driven by AI agents, a natural-language scripting language, and tight
integration with MageCap Forge.

| Area | Godot 4.7 / 4.8 | Bellows today | Gap |
|------|-----------------|---------------|-----|
| 3D rendering | Forward+/Mobile/Compatibility renderers, SDFGI/VoxelGI/lightmaps, AreaLight3D, HDR output, SSR, volumetric fog, decals, texture streaming (4.8), contact shadows (4.8) | PBR, IBL, CSM soft shadows, SSAO, bloom, tonemapping, FXAA, transparency | Large: GI, area lights, SSR, fog, decals, particles, LOD, occlusion culling, texture streaming, HDR output, TAA/upscaling |
| 2D | Full 2D engine: sprites, TileMapLayer, 2D lights/shadows, 2D physics, Line2D, polygons, parallax, one-way collisions | None | Complete subsystem missing (see section 2) |
| Game UI | Control nodes, containers, themes, RichTextLabel, auto-fit fonts (4.8), accessibility (AccessKit) | None (Dear ImGui is editor-only) | Critical gap: no menus, HUDs or text in games |
| Animation | AnimationPlayer/Tree, skeletal animation, blend spaces, IK (4.6), tweens with signal waits (4.7) | None | Large |
| Physics | Jolt (default since 4.6), Godot Physics 2D | Jolt 3D, triggers, raycasts | 2D physics, joints, character controller, shape casts |
| Audio | Buses, effects, streams, spatial audio | 2D/3D sources, listener, one-shots | Buses, effects, streaming music |
| Scripting | GDScript (with LSP, debugger, refactoring), C#, GDExtension (C/C++/Rust) | Python 3.13 with a full API | Debugger, hot reload, signals/events, native plugin API, ForgeScript |
| Editor | Docks, scene tabs, inspector, Asset Store (4.7), runtime 2D manipulation (4.8), symbol renaming (4.8), profiler, remote scene tree | Docked editor, inspector, content browser, console, undo, play mode, agent server | Multiple scenes, script editor, profiler, plugin API, asset store |
| Platforms | Windows, macOS, Linux, Android, iOS, Web, XR (Android XR, Steam Frame), visionOS (4.8) | Windows, Linux (Vulkan) | macOS, mobile, Web, consoles, XR |
| Other | Navigation, multiplayer, localization (pseudolocalization in 4.8), input map, resources, signals | Prefabs, JSON scenes, export | Navigation, networking, localization, input actions |
| AI automation | None built in | Agent server, `bellows.editor` API, CLI, skills | Bellows leads |

## 2. Supporting 2D games

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

## 3. Becoming an engine an AAA studio could use

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
* **Platforms** - macOS (Metal via MoltenVK first), Android, Web (WebGPU), then consoles. Console
  SDKs are under NDA: studios typically license them, so console support means a private,
  partner-only branch.
* **Release discipline** - semantic versioning, LTS branches, migration guides, CI on every
  platform (already started in `.github/workflows/ci.yml`), a public issue tracker, and a real
  documentation site.

A business decision belongs here too: an open-source license (Godot is MIT) attracts contributors
and trust; a commercial license with source access funds development. Decide before inviting
outside contributors.

## 4. ForgeScript

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

What is missing is the engine side: `.forge` files cannot yet be attached to entities.

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
   for pybind11 (`Scripting/ScriptBindings.cpp`). It becomes a shared **script API registry**:
   every type, property, method and function is registered once with typed C++ lambdas, and two
   backends turn the registry into the Python module and the ForgeScript bindings. Names are the
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

1. Script API registry with the Python backend, replacing `ScriptBindings.cpp` without changing
   the Python API (`FeatureTest.py` must pass unchanged).
2. ForgeScript backend for the registry, plus `.forge` support in `ScriptEngine` and the
   Script component, lifecycle events and Inspector fields.
3. `FeatureTest.forge`, `docs/ScriptingAPI.md` examples in both languages, editor support
   (create ForgeScript script, highlighting in the content browser).
4. Language additions that make game code easier: "did you mean ...?" suggestions for misspelled
   names, warnings for variables set but never used, default parameter values, a `match`
   statement, task handles (`set t to start patrol()`, `stop t`), whole numbers and bit flags,
   JSON, `finally`, enums.
5. Tooling: a ForgeScript Language Server (completion, hover docs, go-to-definition, diagnostics,
   rename), the TextMate grammar in the Bellows script editor, a formatter, and a debugger through
   the Debug Adapter Protocol.

## 5. MageCap Forge integration

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

## 6. Suggested sequence

| Milestone | Contents |
|-----------|----------|
| 0.2 ForgeScript in the engine | Script API registry (Python and ForgeScript backends), `.forge` scripts on entities, lifecycle events, Inspector fields, FeatureTest.forge |
| 0.3 Foundation | Windows build verified in CI, script debugger basics, hot reload, signals/events, input actions, audio buses |
| 0.4 Game UI and text | MSDF text, UI components (buttons, labels, panels, layout containers), themes |
| 0.5 2D | Sprites, Camera2D, Box2D, tilemaps, sprite animation, 2D editor mode, 2D templates |
| 0.6 ForgeScript tooling and MageCap Forge | ForgeScript language additions, LSP, debugger; Forge Link for Bellows, Assay bug reports, Chisel/Whetstone ForgeScript support |
| 0.7 Animation and VFX | Skeletal animation, animation timeline, particles, decals |
| 0.8 Scale | Job system, asset import pipeline, streaming, profiler, D3D12 backend |
| 0.9 Platforms | macOS, Android, Web |
| 1.0 | Stable API, documentation site, LTS policy |

## Sources

* [Godot 4.7 release page](https://godotengine.org/releases/4.7/)
* [Godot 4.7.2 maintenance release](https://godotengine.org/article/maintenance-release-godot-4-7-2/)
* [Godot 4.8 beta 1](https://godotengine.org/article/dev-snapshot-godot-4-8-beta-1/)
* [Godot 4.6 release page](https://godotengine.org/releases/4.6/)
