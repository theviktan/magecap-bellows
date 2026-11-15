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

## 4. ForgeScript (Forge)

Goal: ForgeScript is to Bellows what GDScript is to Godot - the first-class, easiest way to write
game logic, with an English-like style:

```forge
script Player
    property speed is 5
    property health is 100

    when the game starts
        set health to 100
    end

    every frame
        if key W is down then
            move forward by speed * delta
        end
    end

    when hit by damage
        reduce health by damage
        if health is below 1 then
            play sound "Audio/Death.wav"
            destroy me
        end
    end
end
```

### Architecture (recommended)

1. **Language specification first** - grammar (EBNF), keywords, types (number, text, yes/no,
   vector, color, entity, list, map), scoping, events, error messages. Written as
   `docs/ForgeScript.md` with a test file per rule. Natural-language languages live or die on
   unambiguous grammar: every sentence form must map to exactly one meaning.
2. **Compiler front end in C++** - lexer (indentation- and keyword-aware), parser to an AST,
   semantic analysis (names, types, unknown properties), helpful errors with line/column and
   "did you mean" suggestions.
3. **Back end: compile to Python** - the AST is lowered to Python code that uses the existing
   `bellows` API, with a source map so every error and stack trace points at the `.forge` line.
   This reuses the whole tested API, the exporter (games already bundle Python) and the
   FeatureTest coverage on day one. A dedicated bytecode VM (like GDScript's) can replace this
   back end later without changing the language, if profiling shows it is needed.
4. **Engine integration** - `.forge` becomes an asset type; the Script component accepts `.forge`
   or `.py`; scripts compile on load, cache by file hash, and recompile when edited. ForgeScript and
   Python scripts can call each other. `properties` become Inspector fields like Python annotations.
5. **Tooling** - a ForgeScript Language Server (LSP) for completion, hover docs, go-to-definition,
   diagnostics and rename; a TextMate grammar for syntax highlighting (VS Code, Monaco, and the
   Bellows script editor); a formatter; later a debugger via the Debug Adapter Protocol.
6. **Tests** - parser and compiler unit tests, plus a `FeatureTest.forge` that mirrors
   `FeatureTest.py` so the whole API is proven from ForgeScript too.

Naming note: "MageCap Forge" (the app) and "Forge" (the language) will be confused. Use
**ForgeScript** in all public material and `.forge` as the file extension.

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
| 0.2 Foundation | Windows build verified in CI, script debugger basics, hot reload, signals/events, input actions, audio buses |
| 0.3 Game UI and text | MSDF text, UI components (buttons, labels, panels, layout containers), themes |
| 0.4 2D | Sprites, Camera2D, Box2D, tilemaps, sprite animation, 2D editor mode, 2D templates |
| 0.5 ForgeScript 1.0 | Specification, compiler to Python, Inspector fields, LSP, FeatureTest.forge |
| 0.6 MageCap Forge | Forge Link for Bellows, Assay bug reports, Chisel/Whetstone ForgeScript support |
| 0.7 Animation and VFX | Skeletal animation, animation timeline, particles, decals |
| 0.8 Scale | Job system, asset import pipeline, streaming, profiler, D3D12 backend |
| 0.9 Platforms | macOS, Android, Web |
| 1.0 | Stable API, documentation site, LTS policy |

## Sources

* [Godot 4.7 release page](https://godotengine.org/releases/4.7/)
* [Godot 4.7.2 maintenance release](https://godotengine.org/article/maintenance-release-godot-4-7-2/)
* [Godot 4.8 beta 1](https://godotengine.org/article/dev-snapshot-godot-4-8-beta-1/)
* [Godot 4.6 release page](https://godotengine.org/releases/4.6/)
