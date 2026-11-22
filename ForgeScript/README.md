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
| [Cookbook](../docs/ForgeScript/Cookbook.md) | Thirty tested recipes for common game tasks: movement, combat, inventories, saving, timers, menus. |
| [Specification](../docs/ForgeScript/Specification.md) | The exact rules of the language, for precise answers and for tools. |
| [Coming from Python or GDScript](../docs/ForgeScript/ComingFromPythonAndGDScript.md) | The same code side by side in Python, GDScript and ForgeScript, and the differences that catch people out. |
| [C++ API](../docs/ForgeScript/Embedding.md) | How C++ code hosts the language: types, functions, phrases, events. |
| [AGENTS.md](AGENTS.md) | Working on the language itself: layout, rules, tests. |

## Building and testing

ForgeScript builds with the rest of Bellows (see the repository [README](../README.md)). After a build:

- the tests run as part of `ctest` (`ForgeScriptTests`, `ForgeScriptEngineExample`, `ForgeScriptCheckCommand`, `ForgeScriptLspCommand` and, in the full MageCap Bellows tree, `ForgeScriptDocExamples`, which checks every example in the docs),
- the command line tool is `build/<preset>/bin/forgescript` (`forgescript.exe` on Windows).

```
forgescript                          start the interactive prompt
forgescript game.forge               run a script
forgescript --check game.forge       check a script for mistakes without running it
forgescript --realtime game.forge    run with real time, so "wait 2 seconds" really waits
forgescript --test game.forge        run the script, then its 'test "..." ... end' blocks
forgescript --format game.forge      lay the script out the standard way
forgescript --benchmark a.forge ...  measure scripts: instructions executed and median time
forgescript --profile game.forge     run the script, then list where its instructions went
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
| `--json` | Print `{"file": ..., "diagnostics": [{"line", "column", "endColumn", "severity", "message"}]}` on standard output instead (the same shape as `BellowsEditor --check-script --json`). `endColumn` is one past the end of the mistake's line; `severity` is `"error"`, or `"warning"` for a warning (after the mistakes). A mistake the editor can fix in one click also has `"fix": {"title", "line", "column", "endLine", "endColumn", "newText"}`: replace from `line`:`column` up to `endLine`:`endColumn` (1-based bytes, the end not included) with `newText`. |
| `--stdin` | Read the script from standard input, for text an editor has not saved yet. |
| `--api <file>` | The engine API description (`forgescript-api.json`), as for `--lsp`; by default the one installed next to `forgescript`, if there is one. With it, engine phrases and functions are known, so `--check` reports exactly what the editor shows. A file given with `--api` that can't be read exits with `66`. |
| `--name <path>` | The script's path, used in messages and to find `use` scripts (from its project's asset folder, or next to it). Needed with `--stdin` so `use` and phrases from other scripts work. |

Exit codes: `0` no mistakes, `65` mistakes found, `66` the file could not be opened, `64` wrong usage. When running a script, `70` means it failed. Tools depend on the text format and exit codes, so they stay stable.

### Running a script's tests

`--test` runs the script, then each of its `test "name" ... end` blocks in order (see the Language Guide's "Testing your scripts"), and prints one line per test on standard output, then a summary:

```
PASS armor lowers damage
FAIL a hit always does at least 1
    Error: Expected damage_after_armor(2, 50) is 1, but damage_after_armor(2, 50) is 0.
        in 'test "a hit always does at least 1"' (combat.forge line 10)

2 tests: 1 passed, 1 failed.
```

Each test runs as a task, with game time stepping by 1/60 second (a test that still waits after 600 seconds of game time fails). Exit codes: `0` every test passed (or there were none), `1` a test failed, `70` the script itself failed before its tests, `66` and `64` as above. `--test` can't be combined with `--check` or `--realtime`.

### Laying scripts out

`--format` lays a script out the standard way and saves it (it writes only when something changes, and never leaves half a file):

- A tab for each open block, and for each open bracket. `else`, `otherwise`, a `match`'s `is` lines, `catch` and `finally` sit one level out, with their block's first line. A line carried on from the one before (after an operator or a `\`) goes one further. Comment lines move with the code under them.
- No spaces at the ends of lines, at most two blank lines in a row, and one line break at the end. Windows line breaks stay Windows line breaks.
- Nothing else changes: not text in quotes, long text, `#[ ]#` comments or the spacing inside a line. The result must read as exactly the same tokens as before, or the file is left alone.

```text
forgescript --format game.forge             rewrite the file: "game.forge: formatted." or "already formatted."
forgescript --format --check game.forge     change nothing; exit 1 when it isn't laid out (for CI)
forgescript --format --stdin < game.forge   print the laid-out script (--name gives its name for messages)
```

Exit codes: `0` laid out (or already was), `1` with `--check` when it isn't, `65` when it can't be laid out (a block missing its `end`, an extra `end` or bracket, or text the lexer can't read; the message gives the line), `66` when the file can't be read or written, `64` for options that don't go together. It doesn't need the engine API: a script with mistakes that still adds up is laid out too. Every example in the docs is kept laid out this way by the `ForgeScriptDocExamples` test.

### Measuring speed

`--benchmark` runs each script five times, each in a new VM with game time stepping by 1/60 second (so `wait` takes no real time), and prints one line per script:

```text
calls.forge instructions 1800594 median 10.9 ms
```

The instruction count is exact and the same on every computer; the time is the median of the five runs. What the scripts print is left out. Exit codes: `0`, `70` when a script fails or executes a different number of instructions on another run (a benchmark must use `random_seed`), `66` when one can't be read.

The scripts in [`Benchmarks`](Benchmarks) measure calls, methods, closures, lists, text, events and tasks. The `ForgeScriptBenchmarks` test compares their instruction counts with `Benchmarks/instructions.txt` and fails when one grows by more than 5%, which catches changes that make the compiler emit more work. It can't catch a slower VM doing the same instructions, so the times are printed for people to compare, not tested. When a change lowers a count, lower its line in `instructions.txt` in the same pull request.

### Finding what's slow

`--profile` runs a script as usual (with game time stepping by 1/60 second), then prints which functions executed the most instructions:

```text
Profile of methods.forge: 1800029 instructions in script code
  instructions       %      calls  function
       1200000   66.7%     100000  bump (methods.forge line 5)
        600023   33.3%          1  (top of the script) (methods.forge line 2)
```

Each function counts the instructions of its own code, not of the functions it calls; `(a function value)` is a `function(x) ... end`. Work done inside built-in functions (the loop of `sort` or `map`, for example) isn't counted, so a line that calls `sort` on a big list costs more time than its count shows. The counts are exact and the same on every computer, so a profile from a player's machine matches yours. `--output profile.txt` writes the table to that file instead of the screen (it never writes next to the script on its own). Hosts get the same from `VM::StartProfile` and `VM::TakeProfile`.

### Language server for editors

`forgescript --lsp` is a [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) 3.17 server over standard input and output. An editor starts it and gets, for `.forge` files:

- **Mistakes as you type** (pushed `textDocument/publishDiagnostics`, the same mistakes as `--check`), and its warnings (severity 2, on the word they're about) once the script has no mistakes; the engine's functions count as built-ins there. A script it uses that has mistakes of its own is also marked on its `use` line, as `Helpers.forge line 3: ...`, since running stops there. Editing a script, or closing it unsaved, updates the open scripts that use it.
- **Completion**: keywords, built-in functions and phrases, the engine's functions, types and phrases, the script's own functions, phrases, kinds, variables and event values, and the scripts it uses (`helpers.` after `use "helpers" as helpers`). After a `.` it offers the members of what the value is when the script shows it: the engine entity after `my` and `self.`, an engine type after `set hero to find_entity("Hero")` or `set t to my transform`, text, list, map or vector methods after a value written as one, and a kind's fields and functions (with those of the kinds it extends) after `set d to new Dragon()`. Otherwise it offers every member any value could have.
- **Hover**: the signature and help of whatever is under the cursor. A script's own functions show the `#` comment lines written right above them. On any word of a phrase it explains the phrase the line uses: of engine phrases that share their words (`move ... to ... over ... seconds`, `... second`, `... seconds with (style)`), only the one written there, and the phrase rather than a variable that happens to share its first word's name. Engine types are shown with ForgeScript's names (`text`, `number`, `yes/no`, `vector`, `nothing`, `list of Entity`), although `forgescript-api.json` uses Python's.
- **Go to definition**: functions, phrases, kinds, variables and parameters, in the file and in scripts it uses; on the path of a `use` line, the script it names.
- **Find all uses** (`textDocument/references`) of the name under the cursor in the script: the uses of that very variable, as the compiler sees it, so another function's parameter with the same name isn't one of them. Fields and methods list every use of a field or method by that name, of any kind.
- **Quick fixes** (`textDocument/codeAction`, kind `quickfix`): for a mistake whose message says exactly what to write instead, one click makes that change. `set x = 5` becomes `set x to 5`, `if a = b` becomes `if a is b`, `damage = 10` among a function's parameters becomes `damage is 10`, `x += 5`, `x++` and `x--` become `add 5 to x`, `add 1 to x` and `take 1 from x`, `repeat while` loses its `repeat`, a misspelled type or choice becomes the one the message suggests, and a block missing its `end` gets one after its last indented line (when an inner block took the `end` meant for an outer one, the inner block gets it).
- **Formatting** (`textDocument/formatting`): the document laid out as `--format` would, as one edit, or the reason it can't be.
- **Rename** (`textDocument/rename`, with `prepareRename`): a variable, parameter, loop or `catch` name, function or kind of the script, in every place it is used there. When renaming could change what the script does it refuses and says why: a field or method (which kind's can't always be told), a name the script doesn't make, a script with mistakes, a keyword, a built-in function or more than 255 characters as the new name, a new name that already means something where the old one is used, a name also written in a phrase's words, or a script-level name that another script of the game says while it `use`s this one, directly or through other scripts, open in the editor or not (rename changes one script only; paths match in any letter case, as Windows opens them). Every rename is checked by compiling the renamed script first.
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
