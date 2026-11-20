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
| [Specification](../docs/ForgeScript/Specification.md) | The exact rules of the language, for precise answers and for tools. |
| [C++ API](../docs/ForgeScript/Embedding.md) | How C++ code hosts the language: types, functions, phrases, events. |
| [AGENTS.md](AGENTS.md) | Working on the language itself: layout, rules, tests. |

## Building and testing

ForgeScript builds with the rest of Bellows (see the repository [README](../README.md)). After a build:

- the tests run as part of `ctest` (`ForgeScriptTests`, `ForgeScriptEngineExample`, `ForgeScriptCheckCommand` and `ForgeScriptLspCommand`),
- the command line tool is `build/<preset>/bin/forgescript` (`forgescript.exe` on Windows).

```
forgescript                          start the interactive prompt
forgescript game.forge               run a script
forgescript --check game.forge       check a script for mistakes without running it
forgescript --realtime game.forge    run with real time, so "wait 2 seconds" really waits
forgescript --version                show the version (see CHANGELOG.md)
```

### Checking scripts from editors and tools

`--check` lists every mistake it can find, one per line on standard error, sorted by line:

```
game.forge:12:5: 'to' is a ForgeScript keyword and cannot be used as a variable name.
game.forge:20:9: Expected a value here, but found '*'.
```

A script with no mistakes may still get **warnings**: things that are allowed but probably not meant. They follow on standard error, with `warning:` after the place, and never change the exit code:

```
game.forge:1:5: warning: Naming a value 'max' hides the built-in function 'max', so max(...) on line 2 won't call it. Pick another name, like 'max_value'.
```

There are two so far: a value named like a built-in function the script also calls (a script variable everywhere; a parameter or a function's own value where the call can find it), and `show (menu)` with a space before the bracket, which calls a function where an engine phrase was meant. Warnings show only once a script has no mistakes, and running a script never reports them.

After a mistake it skips the rest of that statement (or the block the statement opens) and carries on, so one typo is reported once. Unreadable text, like a string without its closing quote, ends the check, and so does a bracket that is never closed (everything after it reads as one line). A `use` whose script cannot be found counts as a mistake.

| Option | |
|---|---|
| `--json` | Print `{"file": ..., "diagnostics": [{"line", "column", "endColumn", "severity", "message"}]}` on standard output instead (the same shape as `BellowsEditor --check-script --json`). `endColumn` is one past the end of the mistake's line; `severity` is `"error"`, or `"warning"` for a warning (after the mistakes). |
| `--stdin` | Read the script from standard input, for text an editor has not saved yet. |
| `--api <file>` | The engine API description (`forgescript-api.json`), as for `--lsp`; by default the one installed next to `forgescript`, if there is one. With it, engine phrases and functions are known, so `--check` reports exactly what the editor shows. A file given with `--api` that can't be read exits with `66`. |
| `--name <path>` | The script's path, used in messages and to find `use` scripts (from its project's asset folder, or next to it). Needed with `--stdin` so `use` and phrases from other scripts work. |

Exit codes: `0` no mistakes, `65` mistakes found, `66` the file could not be opened, `64` wrong usage. When running a script, `70` means it failed. Tools depend on the text format and exit codes, so they stay stable.

### Language server for editors

`forgescript --lsp` is a [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) 3.17 server over standard input and output. An editor starts it and gets, for `.forge` files:

- **Mistakes as you type** (pushed `textDocument/publishDiagnostics`, the same mistakes as `--check`), and its warnings (severity 2, on the word they're about) once the script has no mistakes; the engine's functions count as built-ins there. A script it uses that has mistakes of its own is also marked on its `use` line, as `Helpers.forge line 3: ...`, since running stops there. Editing a script, or closing it unsaved, updates the open scripts that use it.
- **Completion**: keywords, built-in functions and phrases, the engine's functions, types and phrases, the script's own functions, phrases, kinds, variables and event values, and the scripts it uses (`helpers.` after `use "helpers" as helpers`). After a `.` it offers the members of what the value is when the script shows it: the engine entity after `my` and `self.`, an engine type after `set hero to find_entity("Hero")` or `set t to my transform`, text, list, map or vector methods after a value written as one, and a kind's fields and functions (with those of the kinds it extends) after `set d to new Dragon()`. Otherwise it offers every member any value could have.
- **Hover**: the signature and help of whatever is under the cursor. A script's own functions show the `#` comment lines written right above them. Engine types are shown with ForgeScript's names (`text`, `number`, `yes/no`, `vector`, `nothing`, `list of Entity`), although `forgescript-api.json` uses Python's.
- **Go to definition**: functions, phrases, kinds, variables and parameters, in the file and in scripts it uses; on the path of a `use` line, the script it names.
- **Signature help** after `(` and `,`: the function being called, with the parameter you are typing marked, for the script's own functions, the engine's functions and methods, and the built-in ones.
- **Outline** (document symbols): the script's functions, phrases, kinds with their fields, functions and handlers, `when` handlers, script variables and constants.

The engine's API comes from `forgescript-api.json`, which MageCap Bellows generates and installs next to `forgescript`; `--api <file>` points somewhere else. Without it the server knows only the language, so engine phrases show as mistakes. `use` resolves by the same rule as `--check`, and unsaved text of scripts open in the editor wins over the files on disk. The server syncs whole documents, counts positions in UTF-16 as LSP requires, and exits with `0` after `shutdown` then `exit`.

**Where `use` looks.** Inside a MageCap Bellows project (a folder with a `.bproject` file, found by walking up from the script), `use` paths start at the project's asset folder, the same as in the engine: `use "Scripts/Helpers"` in `Assets/Scripts/Main.forge` means `Assets/Scripts/Helpers.forge`. Anywhere else they start next to the script. Either way `use` stays in that folder and below, and loads only `.forge` scripts: absolute paths, `..` steps and other extensions are mistakes, and a link that leads outside the folder isn't followed.

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
Editor/               Visual Studio Code extension (syntax highlighting, icon)
Icons/                the ForgeScript icon: ForgeScript.svg and the PNG and ICO made from it
                      (rebuild all of them with Icons/GenerateForgeScriptIcon.py)
```
