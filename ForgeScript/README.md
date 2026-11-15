# ForgeScript

**ForgeScript** is a scripting language for games by MageCap Games. It reads like plain English, is quick to learn, and is built to drive a game engine: entities, events, timers and game objects are part of the language itself.

```forge
set health to 100

when hit by damage
	take damage from my health
	if my health is below 1 then
		say "{my name} was defeated!"
		self.destroy()
	end
end

for each enemy in enemies do
	move enemy toward player by 2
end
```

- **Natural syntax:** `set ... to`, `add ... to`, `if health is below 10 then`, `for each enemy in enemies do`, `player's health`.
- **Phrases:** define your own English commands (`phrase heal (target) by (amount)`), and the engine can add its own (`move (thing) toward (target) by (amount)`).
- **Built for games:** `when` events, `trigger`, `wait 2 seconds`, `wait until door_open`, and `start` to run tasks side by side.
- **A complete language:** numbers, text with `{interpolation}`, lists, maps, vectors, functions and closures, kinds (classes) with inheritance, error handling, modules and constants.
- **Friendly errors:** every mistake names the file and line and says what to do, for example *"Use 'to' instead of '=', like: set health to 100"*.
- **Easy to embed:** one static C++17 library with no dependencies, a garbage collector, and a small API for exposing engine types, functions and phrases.

## Documentation

| | |
|---|---|
| [Language Guide](Docs/LanguageGuide.md) | Learn ForgeScript step by step, from your first script to kinds, events and tasks. |
| [Quick Reference](Docs/Reference.md) | Every statement, operator, function and method on one page. |
| [Embedding Guide](Docs/Embedding.md) | Add ForgeScript to a C++/CMake engine, expose entities, send events. |
| [AGENTS.md](AGENTS.md) | How to work on ForgeScript itself: layout, code style, testing rules. |

## Building

You need CMake 3.20 or newer and a C++17 compiler.

### Windows (Visual Studio 2022)

1. Install **Visual Studio 2022** with the **Desktop development with C++** workload (it includes CMake).
2. Open **x64 Native Tools Command Prompt for VS 2022** from the Start menu.
3. Go to this folder, for example: `cd C:\Dev\ForgeScript`
4. Configure: `cmake -S . -B build`
5. Build: `cmake --build build --config Release`
6. Run the tests: `ctest --test-dir build -C Release --output-on-failure`
7. The tool is at `build\ForgeScriptCLI\Release\forgescript.exe`.

You can also open the folder directly in Visual Studio (**File > Open > Folder...**) and it will configure CMake for you.

### Linux (Ubuntu 24.04+)

1. Install the tools: `sudo apt install build-essential cmake ninja-build`
2. Configure: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
3. Build: `cmake --build build`
4. Run the tests: `ctest --test-dir build --output-on-failure`
5. The tool is at `build/ForgeScriptCLI/forgescript`.

### Build options

| Option | Default | |
|---|---|---|
| `FORGESCRIPT_BUILD_CLI` | ON | The `forgescript` command line tool. |
| `FORGESCRIPT_BUILD_TESTS` | ON | The test suite. |
| `FORGESCRIPT_BUILD_EXAMPLES` | ON | The engine integration example. |
| `FORGESCRIPT_WARNINGS_AS_ERRORS` | OFF | Fail the build on any compiler warning. |

## Using the `forgescript` tool

```
forgescript                          start the interactive prompt
forgescript game.forge               run a script
forgescript --check game.forge       check a script for mistakes without running it
forgescript --realtime game.forge    run with real time, so "wait 2 seconds" really waits
forgescript --version                show the version
```

Try the scripts in [`Examples/Scripts`](Examples/Scripts): `forgescript Examples/Scripts/battle.forge`.

## Editor support

[`Editor/vscode-forgescript`](Editor/vscode-forgescript) adds syntax highlighting and auto-indent for `.forge` files to Visual Studio Code. Its README has the install steps.

## Project layout

```
ForgeScript/            the language library (link this into the engine)
  src/ForgeScript/
    ForgeScript.h       the one header to include
    API/          types for native (C++) functions and objects
    Compiler/     lexer, phrases and the compiler
    Core/         values, objects, bytecode, UTF-8 helpers
    VM/           virtual machine, tasks, garbage collector, standard library
ForgeScriptCLI/         the forgescript command line tool and interactive prompt
Tests/            C++ unit tests and Tests/scripts/*.forge expectation tests
Examples/         example scripts and a mini engine showing integration
Docs/             language guide, reference and embedding guide
Editor/           Visual Studio Code extension
```
