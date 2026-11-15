# ForgeScript

**ForgeScript** is the natural-language scripting language of MageCap Bellows. It reads like plain English, is quick to learn, and has game features built in: events, timers and tasks that run side by side.

```forge
set health to 100

when hit by damage
	take damage from my health
	if my health is below 1 then
		say "{my name} was defeated!"
	end
end
```

ForgeScript runs on its own virtual machine, the way GDScript runs inside Godot. Game makers can write each script in ForgeScript or in Python.

## Documentation

| | |
|---|---|
| [Language Guide](../docs/ForgeScript/LanguageGuide.md) | Learn ForgeScript step by step. |
| [Quick Reference](../docs/ForgeScript/Reference.md) | Every statement, operator, function and method on one page. |
| [C++ API](../docs/ForgeScript/Embedding.md) | How C++ code hosts the language: types, functions, phrases, events. |
| [AGENTS.md](AGENTS.md) | Working on the language itself: layout, rules, tests. |

## Building and testing

ForgeScript builds with the rest of Bellows (see the repository [README](../README.md)). After a build:

- the tests run as part of `ctest` (`ForgeScriptTests` and `ForgeScriptEngineExample`),
- the command line tool is `build/<preset>/bin/forgescript` (`forgescript.exe` on Windows).

```
forgescript                          start the interactive prompt
forgescript game.forge               run a script
forgescript --check game.forge       check a script for mistakes without running it
forgescript --realtime game.forge    run with real time, so "wait 2 seconds" really waits
forgescript --version                show the version
```

Try the scripts in [`Examples/Scripts`](Examples/Scripts), for example `forgescript ForgeScript/Examples/Scripts/battle.forge`.

## Folder layout

```
Source/ForgeScript/   the language library (namespace ForgeScript, no engine dependencies)
  ForgeScript.h       the one header to include
  API/                types for native (C++) functions and objects
  Compiler/           lexer, phrases and the compiler
  Core/               values, objects, bytecode, UTF-8 helpers
  VM/                 virtual machine, tasks, garbage collector, standard library
CLI/                  the forgescript command line tool and interactive prompt
Tests/                doctest tests (Source/) and .forge expectation scripts (Scripts/)
Examples/             example scripts and a small stand-alone host
Editor/               Visual Studio Code extension (syntax highlighting)
```
