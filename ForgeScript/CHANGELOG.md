# ForgeScript changes

Each version of the language and its `forgescript` tool, newest first. The version is set in
`Source/ForgeScript/ForgeScript.h` and shown by `forgescript --version`, the language server's
`serverInfo` and MageCap Bellows' `Install.json` (`ForgeScriptVersion`). The minor number goes up
with new language or tool features, the patch number with fixes.

## 1.4.0 (2026-11-18)

Events raised by one entity (Phase 2's UI events):
- `when "Start" is clicked ... end` runs only when the entity named Start raised the event, with
  names compared ignoring letter case. With it, one script on a menu handles each of its buttons
  without an `if` per button. The names after `with` get the event's values as usual. A text after
  `when` that isn't followed by `is` is still an event name, so `when "door opened"` keeps its
  meaning.
- `VM::Emit` takes the name of the entity that raised the event; events without one (a script's
  `trigger`, `EmitAll`) never run these handlers.
- In MageCap Bellows, events raised by an entity reach its own script first, then each parent's,
  whatever language each script is written in.

## 1.3.0 (2026-11-17)

Error messages:
- A misspelled name gets a hint naming the one it most likely meant: "'helth' has not been given a
  value yet. Did you mean 'health'?" (before, the hint was "Use: set helth to ..."). This covers the
  script's variables and functions, a function's own variables, built-in functions, the functions
  and values of lists, text, maps and vectors, a kind's fields and functions (also through
  `parent`), the engine's objects, and the names in a script used with `use`. The match ignores
  letter case and counts two swapped letters as one mistake; with nothing close, the message is
  unchanged.
- Calling a value with brackets, like `items.length()`, says it is a value and to write it without
  them.
- `ClosestName` and `DidYouMean` (`Core/Suggest.h`) give hosts the same matching.

## 1.2.0 (2026-11-17)

Tools for editors:
- The language server gives an outline (`textDocument/documentSymbol`): functions, phrases, kinds
  with their fields, functions and handlers, `when` handlers, script variables and constants, with
  the lines each covers, also for a script with mistakes.
- Signature help (`textDocument/signatureHelp`, shown after `(` and `,`): the function being called
  with the parameter you are typing marked, for the script's functions and kinds' functions, the
  engine's functions and methods (narrowed to what the value is when the script shows it) and the
  built-in functions and methods.
- `FindOutline` gives the outline to hosts without the language server.

The ForgeScript icon:
- A smith's hammer and a speech bubble, in the MageCap family's style (`Icons/ForgeScript.svg`, with
  the PNG and ICO made from it by `Icons/GenerateForgeScriptIcon.py`).
- `forgescript.exe` shows it on Windows, and the VS Code extension (1.1.0) uses it for itself and
  for `.forge` files.

## 1.1.0 (2026-11-17)

Tools for editors:
- `forgescript --lsp [--api <file>]`, a language server (LSP 3.17 over standard input and output):
  mistakes as you type, also on the `use` line of a script whose used script has mistakes;
  completion, narrowed after a `.` to what the value is when the script shows it; hover with
  signatures, help and ForgeScript type names; go to definition, also on a `use` path. The engine
  API comes from `forgescript-api.json` next to the program.
- `forgescript --check` reports every mistake, with `--json`, `--stdin` and `--name <path>`, and a
  `use` whose script cannot be found. `VM::FindMistakes` does the same for hosts.
- `use` finds scripts from the nearest project's asset folder (`"AssetDirectory"` in its
  `.bproject`), like the MageCap Bellows engine and editor.
- A debugger API for hosts: breakpoints, pause, continue, step over, into and out, the call stack
  and variables (`VM::SetBreakpoints` and the rest, see `docs/ForgeScript/Embedding.md`).

Language:
- `is` compares the contents of lists and maps: `[1, 2] is [1, 2]` is true.
- Hot reload for hosts: `VM::CarryOverVariables`, `VM::GetGlobalNames`, `VM::ClearModuleCache`; in
  MageCap Bellows, entity scripts reload during play and get `when reloaded`.
- `trigger` events reach other languages through the host (MageCap Bellows' script event hub). An
  event triggered from inside 64 other events stops with an error the script can catch.

In MageCap Bellows 0.3.0 (the engine binding):
- Engine floats reach scripts as the number their shortest text means: a position of 0.1 is 0.1.
- A script stopped by the debugger holds the rest of the frame.

Fixes:
- A function, phrase, kind or `when` handler containing `else if` or `otherwise if` no longer hides
  every line after it (the script stopped there without an error, and checkers missed later
  mistakes).
- A `when` handler that triggers its own event no longer crashes with a stack overflow.

## 1.0.0 (2026-11-15)

The first version: the language (natural-language statements, phrases, kinds, events, tasks with
`wait`, `use`), its compiler and virtual machine, the standard library, the `forgescript` tool with
its interactive console, and the C++ embedding API.
