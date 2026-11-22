# ForgeScript changes

Each version of the language and its `forgescript` tool, newest first. The version is set in
`Source/ForgeScript/ForgeScript.h` and shown by `forgescript --version`, the language server's
`serverInfo` and MageCap Bellows' `Install.json` (`ForgeScriptVersion`). The minor number goes up
with new language or tool features, the patch number with fixes.

## 1.7.0 (not released yet)

- **Dates and the real clock** (the last of Roadmap 2.0 Tier 4 #3's built-ins): `real_time()` is
  seconds since 1970 by the computer's clock, which keeps counting while the game is paused or
  closed; `date([moment])` and `utc_date([moment])` give a map of year, month, day, hour, minute,
  second, weekday, weekday_number, month_name and day_of_year; `format_date(moment [, pattern])`
  writes a moment as text; `days_between(from, to)` counts calendar days in local time, for daily
  rewards. Calendar math is the same on every platform; moments outside 1970 to 9999, infinity and
  not-a-number are refused with a clear mistake. No new words: a script's own name still wins.
- **Fixed: Rename could break a script that wasn't open** (reported by MFC). Renaming a function or
  variable checked only the scripts open in the editor, so with `Main.forge` closed, renaming
  `double` in the `Helpers.forge` it uses went through and left Main calling a name that was gone.
  The language server now looks at every script in the game's folder (open or not, and scripts that
  reach this one through other scripts' `use`), and refuses only when one of them actually says the
  name: "'Main.forge' uses this script and says 'double' on line 2, and rename only changes this
  script. Rename 'double' in both by hand." The old advice to close that script first is gone,
  since closing it didn't help.
- **A warning for `repeat forever` with no `wait`** (audit B-18): the editor, the language server
  and `forgescript --check` now warn "This 'repeat forever' loop never waits, so nothing else in
  the game can run while it does" when the loop has no `wait`, `stop`, `return` or `fail` and calls
  nothing that might wait (the script's own functions, phrases and methods might; built-in and
  engine functions never do). A warning never stops a script.
- **Hosts share `ease()`'s curves** (for Bellows' tweens): `ForgeScript/Core/Easing.h` has
  `TryEase(style, t, outValue)` and `EaseStyleNames()`, and `ease()` now uses them, so an engine's
  tweens and a script's `ease()` give exactly the same numbers. Nothing a script sees changes.
- **A list that holds itself no longer freezes or crashes** (audit S-9, older than 1.6.0). A list,
  map or object that holds itself is now shown as `[...]`, `{...}` or `Name {...}` where it comes
  back: `[1, [...], [...]]`. Before, a list added to itself four times took 27 seconds to `say`, and
  `"{l}"` closed the game. A value that would make more than 100 million bytes of text, such as a
  list holding the same big list many times, now stops with "Turning this into text would make more
  than 100000000 bytes of text", which `try` can catch. Running out of memory while filling in
  `"{...}"` is a script error too. The editor's Variables pane writes out only what it shows, so a
  huge value no longer hangs the debugger.
- **Text and lists that outgrow memory are a script error, not a crash** (audit S-8, older than 1.6.0).
  A loop doing `set s to s + s` or `set s to "{s}{s}"` now stops at 100 million bytes with "Joining
  this text would make more than 100000000 bytes of text", the same limit as repeating text. A
  list (or anything else `+` builds) that can't get the memory it needs fails with "There isn't
  enough memory for this", which `try` can catch, instead of closing the game or the editor.
- **`forgescript --profile`** (Roadmap 2.0 Tier 4 #5, step 3): runs a script, then lists the functions
  that executed the most instructions, with each one's share, its calls and where it is; `--output`
  writes the table to a file. Hosts use `VM::StartProfile` and `VM::TakeProfile`. Counts are exact
  and machine-independent; work inside built-in functions isn't counted. Profiling costs nothing
  while it's off.
- **Fixed: `forgescript --help`** listed `--benchmark` between `--format` and `--format`'s own
  `--check` and `--stdin`.
- **Callbacks are about ten times faster** (Tier 4 #5, step 2). `map`, `filter`, `sort_by`, `any`,
  `all`, `max_by` and every other built-in that calls a script function used to make a new task, with
  its own stack, for every item, which also kept the garbage collector busy. A task that finished
  cleanly is now used again for the next call. `Benchmarks/closures.forge` went from 147 ms to 15 ms
  (release build); nothing a script sees changes.

## 1.6.0 (2026-11-22)

- **Repeating text has a limit** (audit S-6, older than 1.6.0): `"ab" * 1e9` built 2 GB of text in
  one step, and `"ab" * infinity` looped until memory ran out, where the instruction limit can't stop
  it. Both `text * count` and `text.repeat(count)` now refuse to make more than 100 million bytes,
  and `*` refuses infinity. `color`, `random_chance` and `hsv`'s alpha refuse infinity and
  not-a-number too, like the other new built-ins (S-7).
- **`forgescript --benchmark`** (Roadmap 2.0 Tier 4 #5, step 1): runs scripts five times and prints the
  instructions each executed (exact, the same on every machine) and the median time. Seven benchmarks
  in `Benchmarks/` cover calls, methods, closures, lists, text, events and tasks, and the
  `ForgeScriptBenchmarks` test fails when one's instruction count grows by more than 5%. Hosts can
  count too: `VMConfig::CountInstructions` and `VM::GetInstructionsRun` (off by default, since
  counting costs a little time).
- **Fixed: `is a function` inside a function hid the rest of the script.** The compiler's look-ahead
  read the `function` of the type test `x is a function` as the start of a function block, so the
  `end` that closed the real function was taken as that block's, and every line after the function
  silently never ran (with no mistake reported). `is a function` and `is an function` now open
  nothing, as `is a kind` already didn't, in the compiler, the lexer and the editor tools. Scripts
  that never wrote `is a function` inside a function or kind are unchanged.
- **`forgescript --format`** (Roadmap 2.0 Tier 4 #4) lays a script out the standard way: a tab per
  open block or bracket, `else`/`otherwise`/a match's `is`/`catch`/`finally` one level out, carried-on
  lines one further, no spaces at line ends, at most two blank lines, one line break at the end. It
  changes only whitespace around lines, never text, long text, comments or spacing inside a line,
  and checks that the result reads as the same tokens. `--format --check` exits with 1 when a file
  isn't laid out (for CI), and `--format --stdin` prints the result. The language server answers
  `textDocument/formatting`. `ForgeScriptDocExamples` now also checks that every doc example is laid
  out this way.
- **New built-ins** (Roadmap 2.0 Tier 4 #3): `ease(t, style)` with eleven easing curves; `noise(x [, y
  [, z]])`, smooth Perlin noise from -1 to 1 that is the same every run; `color(r, g, b [, a])`,
  `color("#ff8800")` and `hsv(h, s, v [, a])`, each the `[r, g, b, a]` list engine colors take;
  `random_chance(p)`, `random_direction()` and `random_point_in_circle(radius)`, which follow
  `random_seed`. Text gets `capitalize()`, `title()` and `count(part)`; lists get `unique()`,
  `flatten()`, `chunk(size)`, `max_by(f)` and `min_by(f)`. No new words: a script's own function
  or variable with one of these names still wins, as with every built-in.
- **Infinity where a whole number goes is a mistake** (from MCAudit's review of the new built-ins).
  `random(infinity)`, `[1].chunk(infinity)` and any other built-in that takes a whole number used to
  convert infinity (or a number past 9.2e18) to an integer, which is undefined behavior in C++; now
  each says what it needs. `noise`, `ease`, `hsv` and `random_point_in_circle` refuse infinity and
  not-a-number the same way.
- **The Cookbook** (`docs/ForgeScript/Cookbook.md`, Roadmap 2.0 Tier 4 #2): thirty recipes for
  common game tasks (moving, combat, items, saving, state machines, quests, menus, easing), each with
  how it works and how to go further. All are checked against the engine API, and the seventeen
  that don't need the engine carry `test` blocks. `ForgeScriptDocExamples` now also runs the tests
  of any doc example that has them, so a recipe whose results change fails the build.
- **Find all uses, and rename** in the language server (Roadmap 2.0, Tier 4 #1):
  `textDocument/references` and `textDocument/rename` (with `prepareRename`). They work from what
  the compiler resolved, not from spelling, so two parameters both named `amount` are two different
  names, and a function inside a function changes with the outer variable it uses. Rename covers a
  script's variables, parameters, loop and `catch` names, functions and kinds, in that script. It
  refuses, saying why, instead of guessing: fields and methods (`my health`, `goblin.health`; the
  kind isn't known), names a script doesn't make, a script with mistakes, a keyword, built-in
  function or name over 255 characters as the new name, a new name that already means something where the old one is used, a
  name also written in a phrase's words, and a script-level name of a script another open script
  `use`s. Each rename is checked by compiling the renamed script: every name must mean what it
  meant, with no new mistake or warning. Hosts get the compiler's answer from `VM::FindNameUses`.
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
- **List verbs as sentences** (Tier 1 #4): `sort scores`, `sort enemies by health` (each item's field
  or a map's key), `sort words by function(w) ... end`, `shuffle deck` and `reverse items`, the same
  as `.sort()`, `.sort_by(...)`, `.shuffle()` and `.reverse()`. On text, `reverse name` changes the
  variable. The words aren't reserved: names, calls like `sort(x)` and a script's own phrases keep
  their meaning.
- **Reacting to a change** (Tier 2 #9): `when my health changes with now, before ... end` inside a
  kind runs right after a write that changes the field (by `is`), as a task like `trigger`'s, so it
  may `wait`. A field's first value isn't a change, a write inside the field's own handler doesn't
  run it again, and a parent kind's handlers run first. Kinds without handlers, and fields without
  them, write as fast as before. At the top level, `when my health changes` is still the event of
  that name; inside a kind the line was an error before.
- **Fixed before release (Audit 4):** the list words (`the`, `first of`, `item 2 of`) and
  `every`/`any` count toward the 256-level nesting limit, so a very long chain is a mistake
  instead of a crash (S-4). A function with many different values, like a long `choices` list, no
  longer takes quadratic time to compile: 20,000 choices check in a fraction of a second instead
  of half a minute (S-5). A timer with a negative time (`every -1 seconds`) gets a warning.
- **Optional types** (Tier 2 #11): `function heal(amount as number, who as text is "you")`. Types
  never change what runs: `--check`, the language server and the editor warn when a call to one of
  the script's functions gives a plain value of another type ("'heal' takes a number for 'amount'
  (amount as number), but this is text."). The types are the words of `is a`, `kind`, or a kind's
  name; a misspelled type word is a mistake. Signatures in the language server show them.
  `function` and `kind` after `as` open no block. `as` after a parameter was an error before.
- **Tests in scripts** (Tier 2 #13): `test "armor lowers damage" ... end` at the top level of a
  script, with `expect damage_after_armor(10, 3) is 7` lines. Tests never run when the script runs;
  `forgescript --test game.forge` runs the script, then each test (as a task, so it may `wait`),
  prints PASS or FAIL with the reason, and exits with 1 when one failed. `expect` works anywhere:
  "Expected total(2, 3) is 6, but total(2, 3) is 5." Hosts run tests with `VM::GetTestNames` and
  `VM::RunTest`. The language server, outline and VS Code indent rule know `test` blocks. `test`
  and `expect` aren't reserved: both lines were errors before, and `expect(x)` is still a call.
- **Shared values** (Tier 2 #10): `shared count is 0` inside a kind makes one value for the kind
  and every object made from it, read and changed as `Enemy.count` (also in the kind's own
  functions, and as `Boss.count` for a kind that extends it). It is worked out once, when the kind
  is made. `goblin.count` says to write `Enemy.count`, and `set Enemy.x to ...` for a name that
  isn't shared says how to share it. `shared` isn't reserved: the line was an error inside a kind
  before, and `set shared to 2` still works.
- **Any number of values** (Tier 2 #14): `function total(numbers...)`. The last parameter, written
  with `...`, gets the values left over as a list (`[]` when there are none); the ones before it can
  have defaults and be named. It works in kinds' functions and function values, and the language
  server shows it in signatures. `...` after a parameter was an error before.
- **Going through an object's fields** (Tier 2 #12): `for each field, value in goblin do` goes
  through an object made from a kind, field names as text, in the order they were first set (a
  parent kind's first); `for each field in goblin` gives the names. It was an error before ("'for
  each' needs a list, a map or text"); that message now also names objects.
- **Choices (enums)** (Roadmap 2.0 Tier 2 #8): `choices State are idle, running and jumping` at the
  top level makes the constant `State`, where `State.idle` is the text `"idle"`. `State.idel` is a
  compile error that lists the choices, and a `match` whose cases are all one set's choices, with no
  `otherwise`, warns about each missing one. The language server lists the set like a constant.
  `choices` and `are` aren't reserved; the line was an error before, and a script phrase starting
  with `choices` still wins.
- **Every and any** (Tier 1 #7): `every score in scores is at least 70`, `any word in words
  contains "a"` and `every enemy in enemies is defeated` (the field is true), the same as
  `scores.all(...)` and `words.any(...)`. One comparison follows the list; `and`/`or` after it belong
  to the whole condition. Each was an error before; `every 3 seconds` is still a timer, and a
  script's own `every` or `any` keeps its meaning.
- **List words** (Tier 1 #5): `the length of items`, `first of items`, `the last item of items`,
  `item 2 of items` and `item (i + 1) of items`, the same as `length(items)`, `items.first`,
  `items.last` and `items[2]`. `the` before a name in a value changes nothing (`say the score`).
  Each was an error before; a script's own `the`, `first`, `last` or `item` (or a built-in of that
  name) keeps its meaning.
- **Number words** (Tier 1 #6): `x is even`, `x is odd` and `x is a whole number`, with `is not`
  forms. `even` and `odd` are whole numbers only (`2.5 is odd` is false), and need a number;
  `is a whole number` is false for anything that isn't one. The words aren't reserved: where a
  variable, parameter or script name `even` or `odd` is in reach, `x is even` compares with it as
  before.
- **Mistakes from other languages teach the ForgeScript** (Roadmap 2.0, Tier 1 #1). Errors keep
  their message and add the form that works for common habits: `x += 1` and `x++` (`add 1 to x`),
  `increase x by 2`, `def` / `func`, `var` / `let` / `const`, `elif`, a line ending in `:`, `&&` and
  `||`, `//`, `"{price:.2}"`, and at run time `True` / `None` / `null`, `len`, `str`, `int`, `console`,
  and methods such as `list.append`, `text.toUpperCase` and `number.toString`. Each one keeps its old
  first sentence, so tools that match it keep working.
- Nothing changes meaning: `wait for` was an error, and `every`/`after` start a timer only on a line
  that ends in a time word (they stay usable as names; a script phrase starting with them still wins).
- **Keeping things across scenes** (MageCap Bellows 0.5.0). New engine phrase `keep (entity) across
  scenes` (`keep self across scenes`), the same as `set entity.keep_across_scenes to true`. The
  guide's "Moving between scenes" covers kept entities, their carried-over waits and timers, and
  `when scene loaded with name, path`.
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
