---
name: bellows-add-component
description: Add a new component type (or a new field on an existing component) to MageCap Bellows end to end - data, serialization, editor UI, Python API, docs and tests. Use for any change to Scene/Components.h.
---

# Add a component to MageCap Bellows

A component is plain data in `Bellows/Source/Bellows/Scene/Components.h`; systems read it. Work
through every step - a component missing from one of them is a bug (lost on save, invisible in the
editor, or unreachable from Python).

1. **Data** - add `struct FooComponent` to `Components.h` with defaulted fields (PascalCase,
   units in comments). Runtime-only state goes in fields prefixed `Runtime` and is never serialized.
   Add it to the `AllComponents` list so `Scene::Copy`, duplication and prefabs pick it up.
2. **Serialization** - `Scene/SceneSerializer.cpp`: write and read it under the key `"Foo"` using
   `JsonRead` so missing keys keep defaults (older files must still load). Extend the round-trip
   test in `BellowsTests/Source/SceneTests.cpp`.
3. **Systems** - implement the behavior in the owning system (`SceneRenderer` via
   `Scene::OnRender`, `PhysicsWorld`, `SceneAudio`, ...). Handle the component being added or
   removed during play.
4. **Editor** - `BellowsEditor/Source/Panels/InspectorPanel.cpp`: a `DrawComponent<FooComponent>`
   section using `UI::Property*` helpers (tooltips for anything non-obvious) and an entry in
   `DrawAddComponentMenu`. Optionally an icon in `SceneHierarchyPanel.cpp` and gizmo lines in
   `EditorLayer::SubmitEditorOverlays`. Icons come from `Tools/GenerateIconHeader.py`.
5. **Python** - `Bellows/Source/Bellows/Scripting/ScriptBindings.cpp`: a `py::class_<FooRef>`
   with snake_case properties (validate/clamp input; raise `ValueError` for bad values) and a
   `MakeComponentInfo<FooComponent>("Foo")` entry in `GetComponentInfos()` plus
   `ComponentDisplayName`. Changes that must reach a live system (physics, audio) call into it
   from the setter.
6. **Tests** - doctest coverage for the system behavior; add the component to
   `Projects/FeatureTest/Assets/Scripts/FeatureTest.py` (`test_components` checks every property)
   and, if it is visible, to `Tools/BuildFeatureTest.py` so the sample scene uses it; rebuild the
   scene with `BellowsEditor --headless --project Projects/FeatureTest --run-script Tools/BuildFeatureTest.py --exit`.
7. **Docs** - `docs/ScriptingAPI.md` (component section) and, if editor-facing, README features.
8. Run `bellows-build-and-test`, then `bellows-code-review`.
