# AGENTS.md - Working on ForgeScript

Guidelines for changing the ForgeScript language (this folder). The root `AGENTS.md` still applies; this file adds what is specific to the language.

## What this is

ForgeScript is the natural-language scripting language of MageCap Bellows and its flagship way to write game logic: a static library (`Source/ForgeScript/`), the `forgescript` command line tool (`CLI/`), tests (`Tests/`), examples and a VS Code extension, with docs in `docs/ForgeScript/`. It is production software: stable, tested and friendly to beginners.

The language core must stay **engine-independent**: nothing in `Source/ForgeScript/` includes Bellows headers or third-party code. The engine binds to ForgeScript from `Bellows/Source/Bellows/Scripting/`. This keeps the language testable on its own and lets it be split into its own repository later.

## Build and test

ForgeScript is built and tested with the rest of MageCap Bellows (see the root `AGENTS.md`):

```bash
cmake --preset linux-debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug -R ForgeScript --output-on-failure   # or: build/linux-debug/bin/ForgeScriptTests
```

Before every commit that touches ForgeScript, also run its tests under the sanitizers:

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBELLOWS_BUILD_EDITOR=OFF \
	-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
cmake --build build/asan --target ForgeScriptTests && build/asan/bin/ForgeScriptTests
```

A change is done only when it builds with warnings as errors, every test passes (normal, GC stress and sanitizers), and the docs in `docs/ForgeScript/` match the behavior.

## How the pieces fit

1. **Lexer** (`Compiler/Lexer.cpp`) turns text into tokens. New lines end statements, except inside `( ) [ ] { }`, but they matter again inside a `function ... end` written within brackets.
2. **Compiler** (`Compiler/Compiler.cpp`) is a single-pass compiler that emits bytecode (`Core/Chunk.h`). It pre-scans for phrases, script-level names and hoisted `function`/`phrase` definitions, and stops at the first error with a friendly message.
3. **VM** (`VM/VM.cpp`) runs bytecode in **tasks** (coroutines, `VM/Task.h`). Every `when` handler and `start` is a task. `wait` suspends a task; `VM::Update` resumes it.
4. **Library** (`VM/Library.cpp`) holds built-in functions, phrases and methods for text, lists, maps and vectors.
5. **Garbage collector** (mark and sweep, in `VM.cpp`) runs only at instruction boundaries.

## Code style

The root `AGENTS.md` Hazel-style rules apply, with one difference: the language uses `namespace ForgeScript` (not `Bellows`), because it does not depend on the engine.

- Tabs for indentation, braces on their own line (Allman style), `#pragma once` in headers.
- `PascalCase` for types, functions and methods; `camelCase` for locals and parameters.
- Members: `m_Name`; statics: `s_Name`; plain data structs use public `PascalCase` fields (like Hazel components).
- Everything lives in `namespace ForgeScript`. Include paths are rooted at `ForgeScript/Source` (`#include "ForgeScript/VM/VM.h"`).
- Include order: the file's own header first, then other ForgeScript headers, then standard headers, each group sorted.
- Comment the *why*, in plain English a beginner can follow. Messages and comments use American English.
- No raw `new`/`delete` outside the VM allocator and the GC sweep. No global mutable state.

## Rules that keep ForgeScript safe

**Garbage collection.** The collector runs only between instructions (`Execute` checks at the top of the loop) and never while compiling (`GCPause`). So:
- Native code may allocate several objects in a row safely.
- If native code calls back into scripts (`VM::CallFromNative`), any object it created and still needs must be rooted first: keep it in a `Handle`. See `sort_by`, `map` and `filter` in `Library.cpp`.
- Every new object field that points at another object must be marked in `VM::BlackenObject`.
- New root containers in the VM must be marked in `VM::MarkRoots`.

**Stacks.** Task stacks are `std::vector` and can reallocate. Never keep a pointer or reference into `Task::Stack` across a push or a call; use indices. Re-fetch `frame` after anything that can push or pop frames (`FORGESCRIPT_REFRESH`).

**Errors.** Native code reports script errors by throwing `ForgeScript::RuntimeError`. Nothing else may escape into the VM. Every error message must say what went wrong in plain words and, where possible, what to write instead.

**Names.** A new reserved word breaks existing scripts that use it as a name. Prefer contextual words (checked with `CheckWord`/`MatchWord`) over new keywords.

## Adding things (checklists)

**A built-in function or phrase:** add it in `VM::RegisterLibrary`, validate arguments with `CallContext` helpers, add a test in `Tests/Scripts`, list it in `docs/ForgeScript/Reference.md`, and explain it in `docs/ForgeScript/LanguageGuide.md` if beginners will use it.

**A method on a built-in type:** use `RegisterMethod` with the right table (`ListMethods`, `TextMethods`, `MapMethods`, `VectorMethods`). Same test and doc steps.

**A statement or expression form:**
1. Lexer: new keyword only if unavoidable (see Names).
2. Compiler: a `...Statement()` function; if it opens a block, update `IsBlockOpener` in the compiler **and** `IsBlockOpenerWord` in the lexer.
3. Opcode (if needed): add it to `OpCode` with its operand comment, then handle it in `VM::Execute`.
4. Tests: an expectation script, plus compile-error tests in `Tests/Source/LanguageTests.cpp` for its mistakes.
5. Docs: guide, reference, and the VS Code grammar in `Editor/vscode-forgescript`.

## Tests

- `Tests/Scripts/*.forge` run automatically. Write `# expect: <line>` for each line a script must print, in order, and `# expect error: <part of message>` for each error the host must receive. Each script runs twice: normally and with the garbage collector running on every instruction.
- Scripts used with `use` live in `Tests/Scripts/modules/`.
- C++ tests are doctest `TEST_CASE`s; `Tests/Source/TestFramework.h` adds `RunScript` and `CHECK_CONTAINS`.
- `Examples/EngineIntegration` runs as a test too; keep it working, since it is the reference for engine integration.

## Commits

Follow the root `AGENTS.md`: review the full diff with the `bellows-code-review` skill, then commit with an imperative summary line and a description of what changed and why.
