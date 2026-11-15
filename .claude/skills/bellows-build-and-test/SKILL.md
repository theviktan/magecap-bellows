---
name: bellows-build-and-test
description: Build MageCap Bellows and run its test suites (unit, GPU, editor automation, FeatureTest), including headless runs without a GPU. Use before every commit and whenever a change needs verifying.
---

# Build and test MageCap Bellows

1. Make sure submodules are present: `git submodule update --init --recursive`.
2. Configure once per build tree:
   * Linux: `cmake --preset linux-debug` (also `linux-release`, `linux-dist`)
   * Windows: `cmake --preset windows` (Visual Studio 2022 solution in `build/windows`)
3. Build: `cmake --build build/linux-debug` (Windows: `cmake --build build/windows --config Debug`).
   Shaders are compiled to `bin/Shaders/*.spv` as part of the build; a GLSL error fails the build.
4. Run everything: `ctest --test-dir build/linux-debug --output-on-failure`
   (Windows: add `-C Debug`). All five suites must pass:
   `BellowsUnitTests`, `BellowsGpuTests`, `BellowsEditorAutomationTests`, `BellowsFeatureTest`,
   `BellowsEditorFeatureTest`.
5. No GPU (CI, containers): prefix with
   `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json` (Mesa lavapipe). A windowed editor can
   run under `xvfb-run -a`.

## Faster loops

* One doctest case: `build/linux-debug/bin/BellowsTests -tc="*Directional light*"`.
  List cases: `-ltc`. Only non-GPU cases: `--test-case-exclude=*[gpu]*`.
* Feature test alone (prints `FEATURE TEST PASSED (N checks)`):
  `BELLOWS_FEATURE_TEST=1 build/linux-debug/bin/BellowsRuntime --project Projects/FeatureTest --headless --width 640 --height 360 --frames 5000`
* Editor automation script alone:
  `BELLOWS_CONFIG_DIR=/tmp/bellows-config build/linux-debug/bin/BellowsEditor --headless --no-agent-server --run-script BellowsTests/Scripts/EditorAutomationTest.py --exit`
  (`BELLOWS_CONFIG_DIR` keeps your real editor settings untouched; exit code 0 = pass.)
* GPU test images are written to `build/<preset>/bin/TestOutput/` - look at them when a render
  test fails.

## When something fails

* Read the first error, not the last. For Vulkan problems run with validation
  (`--validation` on the editor/runtime; the GPU tests always validate).
* A failing FeatureTest prints `FeatureTest: FAILED - <check>` lines naming each failed check.
* Never weaken a test to make it pass; fix the code or, if the test is wrong, fix the test and
  say why in the commit message.
