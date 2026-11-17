# ForgeScript changes

Each version of the language and its `forgescript` tool, newest first. The version is set in
`Source/ForgeScript/ForgeScript.h` and shown by `forgescript --version`, the language server's
`serverInfo` and MageCap Bellows' `Install.json` (`ForgeScriptVersion`). The minor number goes up
with new language or tool features, the patch number with fixes.

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
