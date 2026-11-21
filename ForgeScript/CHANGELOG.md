# ForgeScript changes

Each version of the language and its `forgescript` tool, newest first. The version is set in
`Source/ForgeScript/ForgeScript.h` and shown by `forgescript --version`, the language server's
`serverInfo` and MageCap Bellows' `Install.json` (`ForgeScriptVersion`). The minor number goes up
with new language or tool features, the patch number with fixes.

## 1.6.0 (not released yet)

- **Waiting for an event** (Roadmap 2.0, Tier 1 #2). `wait for door opened` pauses the task until its
  script gets the event; `wait for hit with damage, source` puts the event's values in the names, and
  `wait for "Start" is clicked` waits for that entity's event. The script's `when` blocks run first,
  then the waiting tasks; a wait begun during the event waits for the next one. It ends when the event
  comes, even while the game is paused. `VM::HasHandler` now also reports a module that waits for the
  event, so hosts send it events it has no `when` block for.
- **Timers** (Tier 1 #3). `every 3 seconds ... end` runs its block again and again, and
  `after 2 seconds ... end` runs it once, each as a task of its own; the line after runs right away.
  They take `wait`'s units, work anywhere (also in functions and kinds), hold while the game is
  paused unless the script is Run While Paused, and end with their script. `stop` inside `every` ends
  it.
- Nothing changes meaning: `wait for` was an error, and `every`/`after` start a timer only on a line
  that ends in a time word (they stay usable as names; a script phrase starting with them still wins).
- The language server, outline, `FindScriptVariables` and `FindHandlersThatWait` know both forms; a UI
  handler that starts a timer counts as one that waits. Help has entries for `every` and `after`.

## 1.5.0 (2026-11-20)

New checker and tool features (warnings, `--check` with the engine API) and new docs, so the minor
number goes up; it was called 1.4.1 while only fixes were in.

- A `match` with no `is` choice before `otherwise` or `end` now says so: "A 'match' needs at least
  one 'is' choice before 'otherwise' or 'end', like: match weapon / is "sword" then ... /
  otherwise ... / end". Before, it said "Expected the end of the line" on the `match` line and then
  "This 'otherwise' needs an 'if' before it." A script's own phrase named `match` is untouched.
- 1.4.0's date is the day it first shipped, in MageCap Bellows 0.3.2.
- After that message, the checker skips the rest of that `match` through its `end`, so it no longer
  also reports "This 'otherwise' needs an 'if' before it." and "This 'end' does not close anything."
  (inside functions too).
- `forgescript --check` reads the engine API the way `--lsp` does: `--api <file>`, or else the
  `forgescript-api.json` installed next to `forgescript`. It uses the language server's own checks,
  so engine phrases aren't mistakes and engine functions count for the warnings: `--check` and the
  editor report the same things. The text and JSON formats and exit codes are unchanged; a file given
  with `--api` that can't be read exits with `66`. `--api` without `--lsp` or `--check` is still
  wrong usage (`64`).
- **Every example in the docs is checked.** The new `ForgeScriptDocExamples` test runs
  `forgescript --check` with the engine API on each ```` ```forge ```` block in the ForgeScript docs,
  the Scripting API page and the README (83 today). It found one: the guide's "Naming the values in a
  call" example had `...` for a body, which isn't ForgeScript; it now has a real one.
- **Pausing the game** (MageCap Bellows 0.4, step 2.11). New phrases `pause the game`, `resume the
  game` and `the game is paused` (the same as `set_paused(true)`, `set_paused(false)` and
  `is_paused()`). While paused, only scripts marked Run While Paused get `when update`, and only
  their waits count down, in real time; every other wait holds and carries on with the time it had
  left, and `time()` stands still. Hosts call the new `VM::UpdatePaused(delta, keepsRunning)`.
  `FindHandlersThatWait(source, events)` lists the `when` blocks for given events that wait,
  directly or through the script's own functions; the Bellows Inspector uses it to warn about a
  pause menu's buttons that wait. Nothing changes for a game that never pauses.
- **`use` stays inside the script's place** (audit S-3). The path must be relative, without `..`, and end in
  `.forge` or have no extension; anything else is a mistake ("'use' only reaches scripts inside this
  script's place: the project's Assets folder, or the folder this script is in and the folders below
  it. ..." or "'use' only loads ForgeScript scripts: ..."). The VM refuses such paths before a host's
  loader sees them, and the loaders of the checker, the language server, `forgescript` and MageCap
  Bellows also refuse a link that leads outside the folder. This is the one deliberate exception to
  "nothing that works changes meaning": a script that reached outside its project could read any file
  on the computer. No script in the repository did.
- **Deep nesting is a mistake, not a crash** (audit S-1 and S-2). Code nested more than 256 levels
  deep (brackets, lists, maps, calls, blocks, or chains of `-`, `not` and `^`) gets "This is nested
  too deeply (more than 256 levels of brackets or blocks). Split it into smaller pieces, like a
  function for the inner part." The compiler also stops there if reading has used 512 KB of stack,
  well inside the 1 MB a Windows program's main thread gets. Before, 10,000 nested brackets (a 20 KB file) overflowed the stack
  and crashed every checker, the editor's Script Editor included. Checking deeply nested blocks is
  also fast now: 100,000 nested `if`s check in under half a second instead of 20 seconds, because
  where to go on after a mistake is only worked out when there is one. No real script comes near
  256 levels; every script in the repository runs as before.
- **Coming from Python or GDScript** (`docs/ForgeScript/ComingFromPythonAndGDScript.md`, ForgeScript
  Roadmap item 13): the same code side by side in all three languages, starting with the
  differences that catch people out. Its ForgeScript examples run as a test
  (`Tests/Scripts/33_coming_from_python_and_gdscript.forge`).
- **The Specification** (`docs/ForgeScript/Specification.md`, ForgeScript Roadmap item 12): the exact rules
  of the language, for precise answers and for tools, checked against the compiler.
- **Warnings** (ForgeScript Roadmap item 12): things that are allowed but probably not meant. They
  never stop a script or change what it does; `VM::FindWarnings` lists them, beside `FindMistakes`.
  - A value named like a built-in function the script also calls: "Naming a value 'max' hides the
    built-in function 'max', so max(...) on line 2 won't call it. Pick another name, like
    'max_value'." A script variable counts everywhere; a parameter or a function's own value only
    where the call finds it. A name the script never calls as a function (a game's
    `set round to 1`) gets none.
  - `show (pause_menu)` with a space, for an engine phrase that gives way to calls: "With brackets,
    'show (...)' is a call to a function named 'show', not the 'show' phrase. To use the phrase, leave the
    brackets out, like: show pause_menu". Not for a script that makes its own function of that name.
  - `forgescript --check` prints them after the place as `warning: ...` (JSON `"severity":"warning"`),
    with the same exit codes. The language server sends them with severity 2, marking the word, and
    counts the engine's functions as built-ins. Both show warnings only once a script has no mistakes.

## 1.4.0 (2026-11-19)

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

Text for players to read, and list helpers:
- `format_number(x [, decimals])` groups thousands and fixes the decimals, for scores and money:
  `format_number(1250.5, 2)` is "1,250.50". It never depends on the computer's language settings.
- `format_time(seconds)` makes a clock for timers: "1:05", and "1:02:03" from an hour on.
- Text: `pad_start(width [, fill])` and `pad_end(width [, fill])`, counting letters, not bytes:
  `"7".pad_start(3, "0")` is "007".
- Lists: `count(function or value)`, `any(function)`, `all(function)`, `min()`, `max()` and
  `average()` (the last three give nothing for an empty list).

Choosing by one value (ForgeScript Roadmap item 11):
- `match weapon`, then lines `is "sword" then ...`, `is "bow", "crossbow" then ...` (any one of them),
  `is above 100 then ...`, and `otherwise ...`, closed by `end`. The value is worked out once and the
  first choice that fits runs. Each `is` takes everything an `if`'s `is` does (above, at least,
  between, in, empty, not).
- `match` is not reserved: it only starts a match at the start of a statement when the next line
  starts with `is`, which was always a mistake before. So `set match to 3`, a function or phrase
  called `match`, and `match` as a property keep their meaning.
- New messages: "This 'match' is missing its 'end'.", "Put 'otherwise' last in a 'match', after every
  'is'.", and a line starting with `is` elsewhere now says "A line can't start with 'is' here. Each
  'is' choice goes inside a 'match', like: match weapon".
- The language server pairs a match with its `end` (outline, variables), `help match` explains it,
  and the VS Code extension (1.2.0) colors `match` and indents its choices.

Menus and HUDs (Phase 2 step 2.6, ForgeScript side):
- In MageCap Bellows: `show (widget)`, `hide (widget)`, `enable (widget)`, `disable (widget)` and
  `focus (widget)`, over Entity's `visible`, `interactable` and `focus()`. A new guide chapter,
  "Menus and HUDs", covers buttons (`when "Start" is clicked`), these phrases and UI input.
- `VM::RegisterPhrase(..., yieldToCalls)`: a host phrase whose first word may already be a name
  (`show` is a ForgeScript function) only applies where that word can't be a value: `show menu` is
  the phrase, while `show(value)`, `show (value)`, `show of x` and `show + 1` keep their old meaning.
  Yielding goes by the first word, so every phrase starting with that word yields too. The API
  description marks such phrases (`"yields_to_calls": true`), and the language server reads it.

Taking a list apart (ForgeScript Roadmap item 10):
- `set name, score to entry`: one name per item, exactly; a vector gives x, y (and z). The value
  is worked out first, so `set a, b to [b, a]` swaps. The names follow the usual rules of `set`.
  The language server lists each name. Before, a comma after the name was always a mistake.

Naming the values in a call (ForgeScript Roadmap item 9):
- `attack(goblin, crit: 5)`: values can be named after the ones given in order. A skipped value
  gets its default even when a later one is named. Works for script functions, kinds (and `new`),
  function values and `start`; built-in functions refuse names with a clear message.
- Mistakes name the value: "'attack' has no value called 'dmg'.", "'attack' needs a value for
  'target'.", "'attack' was given 'target' twice."
- Signature help highlights the parameter a named value is for. Before, `name:` in a call was always
  a mistake, so no script changes meaning.
For hosts:
- `ScriptError::Frames`: an uncaught error's call stack as data (file, line, function, repeats),
  innermost first, beside the text `Trace`. Errors that go on after `finally` code keep theirs.

Default values (ForgeScript Roadmap item 8):
- `function attack(target, damage is 10)`: calls may leave out values that have a default. Each
  default is worked out at the call (so `bag is []` is a new list each time), can use the values
  before it, and comes after the values without one. Works in kinds, `setup` and function values.
- A call with the wrong number of values says the range: "'attack' needs 1 to 2 values".
- The language server shows defaults in outlines and signature help. Before, `is` in a parameter
  list was always a mistake, so no script changes meaning.

Cleanup that always runs (ForgeScript Roadmap item 7):
- `try ... catch problem ... finally ... end`. The finally code runs however the `try` is left: at
  the end, after `catch`, on an error, or on `return`, `stop` or `skip`. Without `catch`, the error
  goes on after the finally code, still reported at the line where it happened.
- `finally` is not a reserved word; where a script uses it as a name, it keeps its old meaning.
  A `try` without `finally` compiles exactly as before.
- The VS Code extension (1.2.0) colors `finally` and indents after it.

Long text (ForgeScript Roadmap item 6):
- `"""` ... `"""` for text over several lines, with `"` inside and no escapes. The indentation all
  its lines share is removed, as are the line break after the opening quotes and a last line holding
  only the closing ones. `{ }` and escapes work as in other text. Before, `"""` was always a mistake,
  so no script changes meaning.
- The VS Code extension (1.2.0) colors long text.

Checking a range (ForgeScript Roadmap item 5):
- `if x is between 1 and 10`, and `is not between`. Both ends count, in either order, for numbers or
  text, and the value is worked out once. `between` is not a reserved word: where a script has its
  own `between`, `x is between low and high` keeps its old meaning.
- `1 < x < 10` still fails, but its message now says to write `x is between 1 and 10`.

Picking one of two values (ForgeScript Roadmap item 4):
- `set size to "big" if x > 3 otherwise "small"`. Only the picked value runs, so
  `target.name if target otherwise "no one"` is safe. It binds looser than `or` and chains in
  order. Before, a value followed by `if` was always a mistake, so no script changes meaning.

Game math and data, like GDScript's and Python's:
- `move_toward(from, to, step)` (numbers or vectors), `inverse_lerp`, `remap`, `smoothstep`,
  `wrap`, `snapped`, and `random_seed(n)` for repeatable random numbers.
- `range(from, to [, step])` (both ends included, like `for i from 1 to 5`), `zip(first, second)`,
  `map.merge(other)`, `list.remove_where(function)`, `text.reverse()`.
- `to_json(value)` and `from_json(text)`, with errors that say what JSON can't hold.

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
