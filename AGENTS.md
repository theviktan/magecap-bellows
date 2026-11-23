# AGENTS.md - working on MageCap Bellows

Guidelines for AI agents (and humans) changing this repository. Read this first; the skills in
`.claude/skills/` hold step-by-step procedures for common jobs.

## What this is

MageCap Bellows is a small, production-quality 3D game engine for Windows and Linux:
C++20, CMake, GLFW, NVRHI (Vulkan 1.3), EnTT, Jolt Physics, miniaudio, embedded CPython 3.13
(pybind11), Dear ImGui. Games are scripted in ForgeScript (`ForgeScript/`, Bellows' own
natural-language language and its flagship) or Python, with one shared engine API; the editor is
fully scriptable so agents can build games through it.

| Folder | Contents |
|--------|----------|
| `Bellows/` | The engine, a static library. `Source/Bellows/<Module>/` (Core, Animation: skeletons and clips, Navigation: navmeshes (Recast/Detour), Renderer, Scene, Physics, Audio, Scripting, Asset, Project, Serialization, Math, ImGui, Text: fonts with MSDF atlases and text layout, UI: game UI layout math) and `Shaders/` (GLSL, compiled to SPIR-V at build time). |
| `BellowsEditor/` | The editor executable: `EditorLayer` (scene/play/undo/viewport, Scene/Script workspaces), `Panels/` (including `ScriptEditorPanel`, the built-in code editor and Attach Script dialog), `ProjectManager` (the window shown when no project is open, and the New Project dialog), UI-free logic compiled into `BellowsTests` (`ScriptDocument`: script languages, templates, outlines, syntax checks; `ProjectList`: project names, scanning, sorting, new-project checks; `UITemplates`: what Create → UI makes, and the anchor presets, shared with `bellows.editor`; `ForgeScriptAssist`: ForgeScript completion and hover from the language server; `ScriptDebugger`: the ForgeScript debugger's breakpoints, stops and stepping; `GameWindowSession`: Play in a game window, running `BellowsRuntime` as a child process (`Core/Process`) and moving its log into the Console; `EditorSettings`), `EditorScriptAPI` (`bellows.editor`), `AgentServer`. |
| `BellowsRuntime/` | The game player. Exported games are this executable renamed. |
| `BellowsTests/` | doctest unit/integration tests (`Source/`) and editor automation tests (`Scripts/`). |
| `ForgeScript/` | The ForgeScript language (compiler, VM, standard library, `forgescript` tool, tests). Engine-independent, `namespace ForgeScript`; read `ForgeScript/AGENTS.md` before changing it. |
| `Python/` | The Python package games import (`bellows/__init__.py`, `bellows/editor.py`) and `_bellows.pyi`, the generated type stub of the native module (for editors and type checkers). |
| `Projects/FeatureTest/` | Sample project using every component; `Assets/Scripts/FeatureTest.py` tests the whole scripting API, after `FeatureTest.forge` tests it from ForgeScript. Rebuilt by `Tools/BuildFeatureTest.py`. |
| `Resources/` | Editor fonts and icons; the app icon (`Icons/Bellows.svg`, its source, and the generated `Bellows.png` and `Bellows.ico`; the build adds ForgeScript's `Icons/ForgeScript.png` from `ForgeScript/Icons`, shown on `.forge` files); `forgescript-api.json`, the generated script API description `forgescript --lsp` reads (copied next to the programs). |
| `Tools/` | `bellows_cli.py` (agent client), `BuildFeatureTest.py`, `GenerateIconHeader.py`, `GenerateTestFonts.py` (writes the CFF test font), `GenerateTestModels.py` (writes the skinned, animated test model), `GenerateAppIcon.py` (rebuilds an app icon's PNG and ICO from its SVG; by default `Resources/Icons/Bellows.svg`), `GeneratePythonStubs.py` (writes `Python/_bellows.pyi`), `PackageRelease.py` (ready-to-run package with bundled Python; CI uploads it for Windows). |
| `docs/` | `ScriptingAPI.md`, `EditorAutomation.md`, `ForgeScript/` (language guide, reference, C++ API), `ReleaseNotes/<version>.md` (the text of each GitHub Release), images. |
| `vendor/` | Third-party code as git submodules plus `vendor/CMakeLists.txt`. Never edit submodule contents. |

## Coordination between chats

Three chats work on this project: **MCBellows** (engine, editor, tools), **FSC** (the ForgeScript chat:
`ForgeScript/` and the ForgeScript runtime) and **MFC** (the MageCap Forge chat: the Forge app's Bellows
integration). They share the **MageCap Bellows Sync Board**
(https://claude.ai/artifact/YAjJThc6uiNbyX2ybiP8rL): current state, the phase plan, who owns what, the
contracts each side relies on, open requests, decisions and a change log. Chats talk to each other in its
comments, tagged `@MCBellows`, `@FSC`, `@MFC`, `@Ron` or `@All`. Read it before starting work, follow the
phase plan in order, and update it after every merged step.

## Build and test

```bash
git submodule update --init --recursive
cmake --preset linux-debug            # Windows: cmake --preset windows
cmake --build build/linux-debug       # Windows: cmake --build build/windows --config Debug
ctest --test-dir build/linux-debug    # Windows: ctest --test-dir build/windows -C Debug
```

Executables land in `build/<preset>/bin/`. Requirements: CMake ≥ 3.24, a C++20 compiler
(GCC 13 / Clang 17 / MSVC 2022), Python 3.13 with development files, `glslangValidator`
(Vulkan SDK on Windows, `glslang-tools` on Linux), a Vulkan 1.3 driver.

The tests:

| ctest name | What it covers |
|------------|----------------|
| `ForgeScriptTests` | The ForgeScript language: lexer, compiler errors, embedding API, and every `ForgeScript/Tests/Scripts/*.forge` script (also under GC stress). |
| `ForgeScriptEngineExample` | The small stand-alone host in `ForgeScript/Examples/EngineIntegration`. |
| `ForgeScriptCheckCommand` | `forgescript --check` text, `--json`, `--stdin`/`--name` output and exit codes, which editors and MageCap Forge depend on (`ForgeScript/Tests/CLI`). |
| `ForgeScriptBenchmarks` | `forgescript --benchmark` on `ForgeScript/Benchmarks/*.forge`: each one's instruction count stays within 5% of `Benchmarks/instructions.txt` (times are printed, not tested). |
| `ForgeScriptLspCommand` | `forgescript --lsp`, the language server, over standard input and output with a recorded session (`ForgeScript/Tests/LSP`). |
| `ForgeScriptDocExamples` | Every ```` ```forge ```` example in `docs/ForgeScript`, `docs/ScriptingAPI.md` and `ForgeScript/README.md` passes `forgescript --check` with the engine API, and an example with `test` blocks (the Cookbook's) also passes `forgescript --test` (`ForgeScript/Tests/Docs`). Examples with a `use` line are left out. |
| `BellowsUnitTests` | Core, math, meshes, scene, serialization, physics, audio, scripting, projects, export, Script Editor and Project Manager logic, editor settings, the agent server over loopback, and fuzz tests of every file loader (no GPU). |
| `BellowsGpuTests` | `[gpu]` test cases: renderer output, picking, shadows, validation-clean frames. |
| `BellowsEditorAutomationTests` | Headless editor driven by `BellowsTests/Scripts/EditorAutomationTest.py`. |
| `BellowsFeatureTest` | The runtime plays FeatureTest; `FeatureTest.forge` and `FeatureTest.py` check the entire API from both languages. |
| `BellowsEditorFeatureTest` | The same, in editor play mode. |
| `BellowsPackageTest` | `BellowsTests/Scripts/PackageTest.py`: packages the build with `Tools/PackageRelease.py`, checks that everything the programs load is in the package, then runs the packaged programs (check tools, FeatureTest, a game exported by the packaged editor). Add a file the programs load to its list. |

Without a GPU, run on the Mesa software rasterizer:
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ctest --test-dir build/linux-debug`.
Run a single doctest case with `build/linux-debug/bin/BellowsTests -tc="*name*"`.

`BellowsTests/Source/FuzzTests.cpp` fuzzes every loader that reads a file a user or a download can
hand the engine (images, glTF, fonts, audio, scenes, prefabs, materials, themes, `.bimport`,
`.bproject`, `.bnav` navmeshes). It mutates valid files from a fixed seed, so a run is the same everywhere, and writes
each input to `TestOutput/Fuzz/<target>/` before loading it, so a crash leaves its file there. A
loader that reads a new kind of file gets a fuzz test there too. CI runs a few hundred inputs per
loader; before a release, or after changing a loader, run longer and under AddressSanitizer, which
catches reads past the end of a buffer that don't crash:

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBELLOWS_BUILD_EDITOR=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan -j3 --target BellowsTests   # More jobs can run out of memory
cd build/asan/bin && ASAN_OPTIONS=detect_leaks=0 BELLOWS_FUZZ_ITERATIONS=20000 ./BellowsTests -tc="Fuzz:*"
```

`BELLOWS_FUZZ_SEED=<number>` tries different mutations. `BellowsTests/Data/CFFTest.otf`, the CFF font
the font tests read, is written by `Tools/GenerateTestFonts.py` (needs `pip install fonttools`).
`BellowsTests/Data/SkinnedColumn.glb`, the skinned and animated model the import, animation and fuzz
tests read, is written by `Tools/GenerateTestModels.py` (standard library only).
GPU tests run with Vulkan validation enabled and fail on any validation message.

## Versions and releases

* `CMakeLists.txt`'s `VERSION` is the **next** release. It moves up right after a release ships, at
  the start of the next cycle.
* Builds that aren't the release say so: `0.4.0-dev (bafec5f)` in the About box, logs, Install.json
  and `bellows.VERSION`. Only a build of the commit tagged `v<version>`, or one configured with
  `-DBELLOWS_RELEASE_BUILD=ON` (CI does that for version tags), reports plain `0.4.0`.
* To release:
  1. Finish `docs/ReleaseNotes/<version>.md` (started with the cycle): remove its "These notes
     grow" line, and have FSC date ForgeScript's "(not released yet)" heading in
     `ForgeScript/CHANGELOG.md` if it has one.
  2. Create the `v<version>` tag and release on github.com.
  3. CI checks that the tag matches the version and that both files are finished, builds and tests
     the package, and its release job (the only job with write access) attaches the zip, with the
     notes as the release text.
  4. Then move `VERSION` up and start the next `docs/ReleaseNotes/<version>.md`.
* CI's actions are pinned to full commits. To update one, change the commit and its version comment
  together.

## Rules

1. **Stay inside this repository.** Do not read, copy from or reference other code on the
   machine. Third-party code comes in only as a submodule in `vendor/` with a compatible license,
   recorded in `ThirdPartyNotices.md`.
2. **Everything is tested.** New engine behavior gets a doctest case; new script API gets a check
   in `FeatureTest.py` and, when ForgeScript uses it differently, `FeatureTest.forge` (and a line
   in `docs/ScriptingAPI.md`); new editor automation gets a check in `EditorAutomationTest.py`
   (and `docs/EditorAutomation.md`). Bug fixes get a regression test.
   All ctest suites must pass before committing.
3. **Warnings are errors** (`BELLOWS_WARNINGS_AS_ERRORS`, on by default). Fix the cause; do not
   silence warnings in first-party code.
4. **Review before committing.** Run the review checklist (`.claude/skills/bellows-code-review`)
   over the full diff. Commit messages: imperative summary line, then what and why.
5. **Keep docs true.** README, AGENTS.md, docs/ and skills describe the code as it is.
6. **No placeholders.** No TODO stubs, dead code, commented-out code or "hack" fixes in
   committed code. If something is out of scope, leave it out completely.

## Code style (Hazel-style C++)

```cpp
namespace Bellows {

	// Comments explain *why*, in full sentences.
	class SceneRenderer
	{
	public:
		void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform);
		uint32_t GetDrawCount() const { return m_DrawCount; }
	private:
		void FlushDrawList();
	private:
		uint32_t m_DrawCount = 0;
		static SceneRenderer* s_Instance;
	};

}
```

* Types, functions, methods, enum values, public struct fields: `PascalCase`.
  Locals and parameters: `camelCase`. Members `m_`, statics `s_`, constants `c_` (file scope).
* Tabs for indentation, Allman braces, `#pragma once`, namespace `Bellows` (nested:
  `Bellows::UI`, `Bellows::Scripting`). Namespace bodies are indented.
* Includes: own header first, then project headers (`"Bellows/..."`), then third-party, then
  standard library, each group sorted. Engine `.cpp` files rely on `bwpch.h`; headers include what
  they use and must compile on their own.
* `Ref<T>`/`Scope<T>` (`CreateRef`, `CreateScope`) for ownership; raw pointers/references are
  non-owning. Prefer `std::filesystem::path` for paths; asset paths are `std::string` with `/`.
* Errors: `BW_CORE_ERROR`/`BW_ERROR` (engine/game) with a useful message; `BW_CORE_ASSERT` /
  `BW_CORE_VERIFY` only for programmer errors. User mistakes (bad files, bad script calls) must
  never crash: log, raise a Python exception, or return false.
* Python: PEP 8, 4 spaces, type annotations on script fields, docstrings on public classes.
* GLSL: `#version 460`, `PascalCase` functions, `u_`/`v_`/`o_` prefixes for uniforms/varyings/outputs,
  shared code in `Bellows/Shaders/Include/`. C++ mirrors of uniform blocks live in
  `Renderer/ShaderInterop.h` with `static_assert`ed layouts.

## Engine conventions worth knowing

* World space is right-handed, Y up, forward is -Z. Clip space is Y-up with depth 0..1: NVRHI's
  Vulkan backend flips the viewport, so projections are **not** Y-flipped and front faces are
  counter-clockwise. Fullscreen-pass UV (0,0) is the top-left (`UVToNDC`/`NDCToUV` in `Common.glsl`).
* Descriptor set *n* = NVRHI `registerSpace` *n* (`setRegisterSpaceAndDescriptorSet`). Set 0 is
  per-frame scene data, set 1 is per-material, set 2 (skinned draws only) the frame's joint matrices.
  Push constants are per-draw.
* Entities are identified by `UUID` (persistent) and drawn with render ID = `entt` handle + 1
  (0 = nothing, used by picking). Scripts (Python and ForgeScript) hold `(sceneID, UUID)` handles,
  never pointers.
* During play, `DestroyEntity` is deferred to the end of the frame; scripts may create and destroy
  entities freely.
* The editor serializes the edited scene for undo (one snapshot per committed change) and plays a
  copy (`Scene::Copy`), so play never modifies the edited scene.
* Editor automation runs at a *frame boundary* (`EditorLayer::RunAtFrameBoundary`), which is the
  only place `RunFrames` may be called.
* Adding a component touches: `Scene/Components.h` (+ `AllComponents`), `SceneSerializer`,
  `Scene::Copy` (via `AllComponents`), the inspector, the hierarchy icon, the script API
  (`Scripting/ScriptApi.h`/`.cpp` component table and `ScriptApiDefinition.h`), `docs/ScriptingAPI.md`,
  `FeatureTest.py` and tests. See `.claude/skills/bellows-add-component`.
* The script API is described **once**, in `Scripting/ScriptApiDefinition.h`, using the neutral
  types of `Scripting/ScriptApi.h`. Two binders turn it into a language: `ScriptBindings.cpp`
  (Python module `_bellows`) and `ForgeScriptBindings.cpp` (ForgeScript). Never add an engine
  function to only one binder; only language-specific extras (Python operators, `__repr__`, script
  objects) belong in a binder. After any change to the Python API, regenerate `Python/_bellows.pyi`
  with `BellowsEditor --headless --no-agent-server --run-script Tools/GeneratePythonStubs.py --exit`;
  `BellowsEditorAutomationTests` fails while it is out of date. Likewise regenerate
  `Resources/forgescript-api.json` (a third binder, `ScriptApiDescription.cpp`) with
  `BellowsEditor --write-forgescript-api Resources/forgescript-api.json`; a unit test fails while it
  is out of date.

## Building games as an agent

Use the running editor through `Tools/bellows_cli.py` (or `BellowsEditor --run-script`). The
procedure is in `.claude/skills/bellows-make-game`; the API is in `docs/EditorAutomation.md` and
`docs/ScriptingAPI.md`. Always verify with screenshots and a play-test before reporting success.
