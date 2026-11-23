# ForgeScript Language Guide

ForgeScript is a scripting language for games. It reads like plain English, it is short to type, and it was built to drive a game engine: moving entities, reacting to events, waiting for time to pass, and organizing game objects.

This guide teaches the whole language step by step. Every example is real ForgeScript code you can run. When you know the basics, the [Cookbook](Cookbook.md) has thirty ready-made recipes for common game tasks.

**How to try the examples:** open a terminal in the folder that holds `forgescript.exe` (Windows) or `forgescript` (Linux) and either:

- type `forgescript` and press Enter to open the interactive prompt, then type code line by line, or
- save code in a file such as `test.forge`, then run `forgescript test.forge`.

---

## Contents

1. [Your first script](#1-your-first-script)
2. [Values](#2-values)
3. [Variables](#3-variables)
4. [Text](#4-text)
5. [Making decisions with `if`](#5-making-decisions-with-if)
6. [Loops](#6-loops)
7. [Lists](#7-lists)
8. [Maps](#8-maps)
9. [Functions](#9-functions)
10. [Phrases: your own English commands](#10-phrases-your-own-english-commands)
11. [Kinds: making your own objects](#11-kinds-making-your-own-objects)
12. [Events with `when` and `trigger`](#12-events-with-when-and-trigger)
13. [Time: `wait` and `start`](#13-time-wait-and-start)
14. [Handling errors](#14-handling-errors)
15. [Vectors](#15-vectors)
16. [Using other scripts](#16-using-other-scripts)
17. [Scripts attached to entities: `self` and `my`](#17-scripts-attached-to-entities-self-and-my)
18. [Finding bugs with the debugger](#18-finding-bugs-with-the-debugger)
19. [Rules worth remembering](#19-rules-worth-remembering)
20. [Menus and HUDs](#20-menus-and-huds)

For a one-page list of everything, see [Reference.md](Reference.md). The exact rules, for when you need a precise answer, are in [Specification.md](Specification.md). Already know Python or GDScript? [ComingFromPythonAndGDScript.md](ComingFromPythonAndGDScript.md) shows the same code in all three.

---

## 1. Your first script

Create a file named `hello.forge` with this line:

```forge
say "Hello, world!"
```

Run it with `forgescript hello.forge`. It prints:

```
Hello, world!
```

`say` shows a value. Text goes inside double quotes `" "`.

**Comments** are notes for people. ForgeScript ignores them:

```forge
# This is a comment. It runs to the end of the line.

#[
   This is a block comment.
   It can cover many lines.
]#
```

**One statement per line.** A new line ends a statement. If a line ends with `+`, `-`, `*`, `/`, `,`, `and`, `or` or `.`, it continues on the next line. You can also end a line with `\` to continue it:

```forge
set total to 10 +
	20 +
	30
```

Inside `( )`, `[ ]` and `{ }` you can spread values over as many lines as you like.

---

## 2. Values

ForgeScript has a small set of value types:

| Type | Examples | Notes |
|---|---|---|
| number | `42`, `3.5`, `-7`, `1_000_000`, `0xFF`, `2.5e3` | All numbers are one type. `_` makes big numbers readable. |
| text | `"Hello"`, `"Line one\nLine two"` | Always double quotes. |
| boolean | `true`, `false`, `yes`, `no` | `yes` means `true`, `no` means `false`. |
| nothing | `nothing` | "No value". Variables that were never set are nothing. |
| list | `[1, 2, 3]` | An ordered collection. |
| map | `{ name: "Ron", level: 3 }` | Named values (also called a dictionary). |
| vector | `vector(1, 2, 3)` | A 3D position or direction. |
| function | `function(x) return x * 2 end` | Code you can call. |
| kind / object | `new Enemy()` | Your own types (see section 11). |

**Math:**

```forge
say 1 + 2 * 3      # 7   (multiply happens before add)
say (1 + 2) * 3    # 9
say 10 / 4         # 2.5
say 7 % 3          # 1   (remainder)
say 2 ^ 10         # 1024 (power)
```

**True and false:** only `false` and `nothing` count as false. Everything else, including `0` and empty text `""`, counts as true. This keeps the rule short and easy to remember.

---

## 3. Variables

A variable is a name that holds a value. Make or change one with `set ... to`:

```forge
set health to 100
set player_name to "Ron"
set is_alive to yes
```

ForgeScript has English commands for changing numbers:

```forge
set gold to 50
add 25 to gold          # gold is now 75
take 10 from gold       # gold is now 65
multiply gold by 2      # gold is now 130
divide gold by 4        # gold is now 32.5
```

These also work on properties and list items:

```forge
add 10 to player.score
take 1 from lives[1]
```

**Constants** are values that must never change. Make them at the top of a script:

```forge
constant MAX_HEALTH is 100
```

Trying to `set MAX_HEALTH to 5` later is an error.

For a fixed set of named options, like an enemy that is idle, patrolling or chasing, use
`choices State are idle, patrol, chase` (see [Naming your choices](#naming-your-choices-with-choices)).

**Names** may use letters, digits and `_`, and must start with a letter or `_`. Words that ForgeScript uses itself (like `if`, `end`, `set`, `to`, `from`, `by`, `with`) cannot be variable names. See the full list in [Reference.md](Reference.md#reserved-words).

---

## 4. Text

Join text with `+`. Numbers and other values are turned into text automatically:

```forge
set name to "Ron"
say "Hello, " + name + "!"       # Hello, Ron!
say "Level " + 3                 # Level 3
```

**Putting values inside text** is even easier with `{ }`:

```forge
set level to 7
say "Hi {name}, you are level {level}."       # Hi Ron, you are level 7.
say "Next level: {level + 1}"                 # Next level: 8
```

To write a real `{` or `}` in text, put a backslash before it: `"\{"`.

**Special characters:** `\n` (new line), `\t` (tab), `\"` (a quote), `\\` (a backslash).

### Long text

For text over several lines, like dialogue, help pages or a quest description, use three quotes:

```forge
function show_quest(hero)
	say """
		Welcome, {hero}!
		The old mill is "haunted", they say.
		Bring back 3 lanterns.
		"""
end
```

That says three lines: `Welcome, Ron!`, `The old mill is "haunted", they say.` and `Bring back 3 lanterns.`

- Quotes inside need no backslash: `"haunted"` stays as it is.
- The indentation your code needs is removed. ForgeScript finds the spaces and tabs every line starts with and takes them off, so the text above starts at the left edge. A line indented further keeps the extra.
- When the opening `"""` ends its line, that line break isn't part of the text, and neither is the last line when it holds only the closing `"""`.
- `{ }` and the special characters work as in other text. To end the text with a quote, write it right before the closing quotes: `"""He said "hi""""` is `He said "hi"`.
- `"""like this"""` on one line works too.
- Each line ends with a plain new line, also in scripts saved with Windows line endings.

**Useful text tools:**

```forge
set title to "Dragon Quest"
say title.length              # 12
say title.upper()             # DRAGON QUEST
say title.lower()             # dragon quest
say title[1]                  # D   (positions start at 1)
say title[-1]                 # t   (negative positions count from the end)
say title.slice(1, 6)         # Dragon
say title.contains("Quest")   # true
say title contains "Quest"    # true (the same thing, in English)
say title.replace("Dragon", "Side")     # Side Quest
say "a,b,c".split(",")        # ["a", "b", "c"]
say "  padded  ".trim()       # padded
say "stressed".reverse()      # desserts
say "42".is_number()          # true
say number("42") + 1          # 43
say text(42) + "!"            # 42!
```

Text works with every language: `"café".length` is 4.

**Numbers for players to read.** A HUD wants `1,250` and `12.50`, not `1250` and `12.5`:

```forge
say "Credits: " + format_number(1250)         # Credits: 1,250
say "Price: $" + format_number(12.5, 2)        # Price: $12.50
say "Time: " + format_time(65)                 # Time: 1:05 (from an hour on: 1:02:03)
say "Level " + text(7).pad_start(3, "0")       # Level 007
say "Name".pad_end(10, ".") + "Score"          # Name......Score
```

---

## 5. Making decisions with `if`

```forge
set health to 40

if health is below 20 then
	say "Find a potion!"
else if health is below 50 then
	say "Be careful."
else
	say "Feeling strong."
end
```

- Start with `if`, put the condition, then `then` (optional but recommended).
- Add as many `else if` branches as you need. `otherwise` means the same as `else`.
- Close the whole thing with `end`.

Short checks can fit on one line:

```forge
if health is 0 then say "Game over" end
```

### Comparing values

| ForgeScript | Meaning |
|---|---|
| `a is b` | equal |
| `a is not b` | not equal |
| `a is above b` / `a is greater than b` | `>` |
| `a is below b` / `a is less than b` | `<` |
| `a is at least b` | `>=` |
| `a is at most b` | `<=` |
| `a is between 1 and 10` | from 1 to 10, both ends included |
| `a is equal to b` | equal |
| `a is empty` | text, list or map with nothing in it (or nothing itself) |
| `a is in list` / `list contains a` | membership |
| `a is a number` | type check (`number`, `text`, `boolean`, `list`, `map`, `function`, `vector`, `object`, `kind`) |
| `a is a Enemy` | is this object made from the kind `Enemy` (or a kind that extends it)? |
| `a is even` / `a is odd` | a whole number that divides by 2 (or doesn't); `2.5` is neither |
| `a is a whole number` | a number with nothing after the point (`4`, `-2`, but not `4.5`) |

Every one of these has an `is not` form: `a is not even`, `a is not a whole number`.

> **Your own `even`.** `even` and `odd` aren't reserved. If your script has its own variable,
> parameter or function called `even` (say `set even to 4`, anywhere in the script), then
> `x is even` in that script means "is x equal to even", as it always did. Rename yours if you want
> the number test.

If you prefer symbols, `==`, `!=`, `<`, `<=`, `>`, `>=` also work. A single `=` is **not** used in ForgeScript; the compiler will tell you to use `is` or `to` instead.

### Combining conditions

```forge
if health is above 0 and has_key then say "The door opens." end
if is_night or is_raining then say "Stay inside." end
if not is_alive then say "Respawning..." end
```

`and` and `or` stop early: in `a and b`, if `a` is false, `b` is never checked.

### Checking a range

To check that a value lies between two others, say so:

```forge
if health is between 1 and 30 then say "Find a potion!" end
if level is not between min_level and max_level then say "Wrong area." end
set grade to "pass" if score is between 50 and 100 otherwise "fail"
```

- Both ends count: `10 is between 1 and 10` is true.
- The ends can come in either order: `5 is between 10 and 1` is true too.
- It works for text in alphabetical order: `"m" is between "a" and "z"`.
- Each value is worked out once, so `roll_dice() is between 2 and 5` rolls once.
- Python lets you write `1 < x < 10`. ForgeScript doesn't: that compares `1 < x` (true or false) with 10, and the error tells you to write `x is between 1 and 10` instead.
- If your script (or one it brings in with `use`) has its own variable named `between`, `x is between low and high` still compares `x` with that variable, as it did before this form existed. A plain number or text after `between` (`x is between 1 and 10`) is always the range form.

`or` is also handy for default values: `set name to saved_name or "Player"` gives "Player" when `saved_name` is nothing.

### Picking one of two values

When only a value changes, put the `if` after it: the value, `if`, the test, `otherwise`, then the other value.

```forge
set size to "big" if width > 100 otherwise "small"
say "You win!" if score >= 100 otherwise "Keep going"
set speed to 8 if is_running otherwise 3
```

- Only the picked value is worked out, so `target.name if target otherwise "no one"` is safe even when `target` is nothing.
- You can chain them, and they read in order: `"A" if score >= 90 otherwise "B" if score >= 80 otherwise "C"`.
- `otherwise` is required: a value with only an `if` after it is a mistake, since ForgeScript wouldn't know what to use when the test is false.
- It works anywhere a value goes: inside text (`"You are {"fine" if health > 5 otherwise "low"}."`), lists, maps and function calls. For whole blocks of code, use the `if ... then ... end` statement above.
- On the first line of a block, before its `then` or `do`, an `if` still starts an `if` block, just as it always has: `for each enemy in enemies if enemy.awake then ... end end` checks each enemy. Put a picked value on its own line inside the block instead.

### Choosing by one value with `match`

When you check one value against many choices, `match` saves writing it again in every `else if`:

```forge
match weapon
is "sword" then
	say "Slash!"
is "bow", "crossbow" then
	say "Twang!"
otherwise
	say "Thud."
end
```

Read it as: match the weapon; if it is "sword", slash; if it is "bow" or "crossbow", twang; otherwise, thud. It does exactly what this `if` does:

```forge
if weapon is "sword" then
	say "Slash!"
else if weapon is "bow" or weapon is "crossbow" then
	say "Twang!"
otherwise
	say "Thud."
end
```

How it works:

- `match`, then the value, on its own line. Each choice starts a new line with `is`, then the block for it. `then` is optional, as in an `if`, and a short choice fits on one line: `is "sword" then say "Slash!"`.
- The value is worked out once, before the choices. The first choice that fits runs, and the rest are skipped.
- Each `is` works like the `is` of an `if`, so every comparison you know works: `is above 100`, `is at least 90`, `is between 80 and 89`, `is in [1, 2, 3]`, `is empty`, `is not "idle"`.
- Separate values with commas when any one of them should pick the choice: `is "bow", "crossbow"`. Each can be a comparison too: `is below 0, above 100`.
- `otherwise` comes last and runs when no choice fits. Leave it out and nothing happens then.
- Like `if weapon is above 100`, a choice that compares text with a number is a mistake. Matching a value that can be either? Put the text choices first, and check the number with `is above` only when it's a number.

`match` is not a reserved word. It only starts a match when it begins a line and the next line starts with `is`. So a variable or function named `match` (`set match to 3`) works as it always has.

A match pairs well with a set of **choices**:

```forge
choices State are idle, patrol, chase
set state to State.patrol

when update with delta
	match state
	is State.idle then wait_for_player()
	is State.patrol then walk_route()
	is State.chase then run_at_player()
	end
end
```

### Naming your choices with `choices`

Many things in a game can be one of a few states: an enemy is idle, patrolling or chasing; a door is
open, closed or locked. `choices` gives each one a name (other languages call this an *enum*):

```forge
choices Door are open, closed and locked

set door to Door.closed
if door is Door.locked then
	say "You need a key"
end
```

- `Door.closed` is the text `"closed"`, so `say door` prints `closed`, and the value saves and loads
  like any text.
- **Spelling mistakes are caught before the game runs.** `Door.lockd` is an error: "Door has no
  choice 'lockd'. Did you mean 'locked'? Its choices are: open, closed, locked."
- **A `match` that forgets a choice gets a warning.** When every case of a `match` is one of a set's
  choices and there is no `otherwise`, the checker (and the editor) warns about each missing one:
  "This match has no case for Door.locked, and no otherwise." Add the case, or an `otherwise`.
- `Door` can't be changed (it's a constant), and you can walk it: `for each d in Door do`, and count
  it: `Door.length`.
- Write `choices` at the top level of the script (not inside a function or kind), separated by
  commas, with an optional `and` before the last.
- `choices` isn't reserved: `set choices to 3` still works. It only makes choices at the start of a
  line followed by a name and `are`.

---

## 6. Loops

### Repeat a number of times

```forge
repeat 3 times
	say "Hello!"
end
```

### Count with `for ... from ... to`

```forge
for i from 1 to 5 do
	say i              # 1, 2, 3, 4, 5
end

for i from 10 to 0 by -2 do
	say i              # 10, 8, 6, 4, 2, 0
end
```

The end number is included. If the start is bigger than the end, ForgeScript counts down automatically.

### Go through every item with `for each`

```forge
set enemies to ["goblin", "orc", "troll"]

for each enemy in enemies do
	say "A wild {enemy} appears!"
end

# A second name gives you the position (lists) or the value (maps).
for each enemy, position in enemies do
	say "{position}. {enemy}"
end

for each key, value in { gold: 10, gems: 2 } do
	say "{key}: {value}"
end

for each letter in "abc" do
	say letter
end
```

### Loop while something is true

```forge
set fuel to 3
while fuel is above 0 do
	take 1 from fuel
end

until fuel is 10 do
	add 1 to fuel
end
```

### Loop forever

```forge
repeat forever
	say "tick"
	wait 1 second       # always wait inside endless loops (see section 13)
end
```

### Leave or skip

- `stop` leaves the loop right away.
- `skip` jumps to the next round of the loop.

```forge
for each number in [1, 2, 3, 4, 5] do
	if number is 2 then skip end
	if number is 4 then stop end
	say number          # 1, 3
end
```

---

## 7. Lists

A list holds values in order. **Positions start at 1.**

```forge
set items to ["sword", "shield"]
add "potion" to items           # ["sword", "shield", "potion"]
take "shield" from items        # ["sword", "potion"]
say items[1]                    # sword
say items[-1]                   # potion (last)
set items[1] to "axe"
say items.length                # 2
say length of items             # 2
```

**Reading a list like a sentence.** These say the same things in words:

```forge
set items to ["sword", "shield", "potion"]
say the length of items         # 3, the same as length of items
say first of items              # sword, the same as items.first
say the last item of items      # potion, the same as items.last
say item 2 of items             # shield, the same as items[2]
say item i of items             # the item at position i
say item (i + 1) of items       # a worked-out position goes in brackets
say item 2 of item 1 of grid    # a list in a list: grid[1][2]
```

- `the` can go before any name in a value, and it changes nothing: `say the score` is `say score`.
- `first of` and `last of` an empty list give `nothing`, as `.first` and `.last` do.
- **Your own names win.** These words aren't reserved. If your script has its own `the`, `first`,
  `last` or `item` (a variable, a parameter, a function, anywhere in the script), that word means
  yours, as it always did. For example, inside `items.map(function(item) ... end)`, `item` is the
  parameter.

**Asking about every item, or any.** `every` and `any` ask a question about all the items of a list:

```forge
set scores to [80, 95, 72]
if every score in scores is at least 70 then
	say "Everyone passed"
end
if any score in scores is above 90 then
	say "Someone did great"
end
say any word in words contains "a"     # true if one word has an "a"
say every enemy in enemies is defeated  # each enemy's 'defeated' field is true
say any enemy in enemies is not defeated
```

- After `every score in scores` comes one comparison about `score`: `is above 90`, `is 3`,
  `is even`, `contains "a"`, `< 5`. An `and` or `or` after it belongs to the whole `if`:
  `if every s in scores is above 0 and level is 2 then`.
- `is defeated` with nothing after it reads the item's field `defeated` and checks it is true, unless
  your script has its own `defeated`; then it compares with yours.
- `every` is true for an empty list, and `any` is false, the same as `scores.all(...)` and
  `scores.any(...)`.
- `every 3 seconds ... end` is still a timer: a name and `in` after `every` make it the question.
  And as with the list words, a variable or function of your own named `every` or `any` keeps its
  meaning.

**Taking a list apart.** When a list holds a few values that belong together, give each its own name in one line:

```forge
set entry to ["Ron", 99]
set name, score to entry        # name is "Ron", score is 99

set x, y, z to player.position  # a vector gives x, y and z (or just x and y)
set a, b to [b, a]              # the list is made first, so this swaps a and b
```

There must be exactly one name for each item: `set a, b to [1, 2, 3]` says "There are 2 names but the list has 3 items." The names follow the usual rules of `set`: inside a function they're the function's own variables.

**Everything a list can do:**

| Function | What it does |
|---|---|
| `list.add(x)` | Add to the end (same as `add x to list`). |
| `list.insert(position, x)` | Put `x` at a position. |
| `list.remove(x)` | Remove the first `x`. Returns true if found. |
| `list.remove_at(position)` | Remove and return the item at a position. |
| `list.pop()` | Remove and return the last item. |
| `list.clear()` | Remove everything. |
| `list.contains(x)` | True if `x` is in the list. |
| `list.index_of(x)` | Position of `x`, or nothing. |
| `list.first`, `list.last` | First / last item, or nothing when empty. |
| `list.length` | Number of items. |
| `list.is_empty()` | True when there are no items. |
| `list.sort()` | Sort numbers or text, smallest first. |
| `list.sort_by(function)` | Sort by a value the function gives for each item. |
| `list.reverse()` | Reverse the order. |
| `list.shuffle()` | Random order. |
| `list.pick()` | A random item. |
| `list.slice(start, end)` | A new list with the items from start to end. |
| `list.join(separator)` | All items as one text. |
| `list.sum()` | Total of a list of numbers. |
| `list.average()`, `list.min()`, `list.max()` | Average, smallest and largest of a list of numbers (nothing when empty). |
| `list.count(function or value)` | How many items the function says true for, or how many equal the value. |
| `list.any(function)` / `list.all(function)` | True when the function says true for at least one item / for every item. |
| `list.map(function)` | New list with the function applied to every item. |
| `list.filter(function)` | New list with only items where the function says true. |
| `list.find(function)` | First item where the function says true. |
| `list.remove_where(function)` | Remove every item where the function says true; gives back how many went. |
| `list.each(function)` | Call the function for every item. |
| `list.copy()` / `copy(list)` | A separate copy. |

```forge
set scores to [40, 95, 70]
say scores.filter(function(s) return s is above 50 end)    # [95, 70]

set monsters to ["dragon", "rat", "wolf"]
monsters.sort_by(function(m) return m.length end)
say monsters                                               # ["rat", "wolf", "dragon"]

set enemy_health to [0, 0, 12]
if enemy_health.all(function(h) return h is 0 end) then say "You win!" end
if every h in enemy_health is 0 then say "You win!" end    # the same, as a sentence
say enemy_health.count(0)                                  # 2
say scores.average()                                       # 68.333333333333
```

Sorting, shuffling and turning a list around can also be written as sentences:

```forge
set scores to [40, 95, 70]
sort scores                       # [40, 70, 95]
reverse scores                    # [95, 70, 40]

set deck to [1, 2, 3, 4, 5]
shuffle deck

kind Enemy
	set health to 10
end
set enemies to [new Enemy, new Enemy]
sort enemies by health            # by each enemy's health field (a map's key works too)
sort deck by function(card) return card % 3 end
```

Lists are **shared**: `set b to a` makes both names point at the same list. Use `copy(a)` when you need a separate one.

`is` compares what lists hold: `[1, 2] is [1, 2]` is `true`, and so is a list and its copy until one of them changes. `contains`, `is in`, `index_of`, `remove` and `take ... from` use the same rule, so `[[1, 2], [3, 4]] contains [3, 4]` is `true`.

---

## 8. Maps

A map stores values under names (keys):

```forge
set hero to { name: "Aria", health: 30, class: "mage" }

say hero.name           # Aria
say hero["class"]       # mage
set hero.health to 25
set hero["level"] to 2  # adds a new key
say hero.missing        # nothing (missing keys are nothing)
```

Keys can be text, numbers or true/false: `set grid[3] to "wall"`.

| Function | What it does |
|---|---|
| `map.keys()` | List of keys (in the order they were added). |
| `map.values()` | List of values. |
| `map.has(key)` / `map contains key` | True if the key exists. |
| `map.get(key, default)` | The value, or the default when missing. |
| `map.remove(key)` | Remove a key. |
| `map.merge(other)` | Copy every entry of `other` in, replacing values with the same keys. |
| `map.length` | Number of keys. |
| `map.clear()`, `map.copy()`, `map.is_empty()` | As for lists. |

Maps can hold functions, which makes them handy as simple objects:

```forge
set actions to { greet: function(who) return "Hi, " + who end }
say actions.greet("Ron")
```

Two maps are equal (`is`) when they have the same keys with equal values, in any order: `{ a: 1, b: 2 } is { b: 2, a: 1 }` is `true`. A list or map used as a **key** means that one particular list or map, not any list with the same items.

**Defaults with `merge`:** start from the defaults and lay the player's choices over them:

```forge
set settings to { volume: 0.5, music: true }
settings.merge({ volume: 0.8 })
say settings                       # {"volume": 0.8, "music": true}
```

**Maps and lists as JSON text.** Web services and other programs often speak JSON. `to_json` turns nothing, true/false, numbers, text, lists and maps (with text keys) into JSON text, and `from_json` reads it back:

```forge
set json to to_json({ name: "Starfall", ships: ["Comet"] })
say json                           # {"name":"Starfall","ships":["Comet"]}
say from_json(json).ships[1]       # Comet
```

JSON has no vectors, so write them as lists: `[v.x, v.y, v.z]`. To keep progress between play sessions, use `save_data` instead (chapter 17): it keeps vectors too.

---

## 9. Functions

A function is a named piece of code you can run again and again:

```forge
function greet(name)
	say "Hello, {name}!"
end

greet("Ron")
greet("Sam")
```

Functions can **give back** a value with `return`:

```forge
function damage_after_armor(damage, armor)
	return max(0, damage - armor)
end

say damage_after_armor(30, 12)     # 18
```

**Good to know:**

- You can call a function from anywhere in the script, even above the line that defines it.
- A function must be given the number of values it asks for, except values with a default, and a last value written with `...`, which takes any number (both below).
- A function with no `return` gives back `nothing`.
- `length of items` means `length(items)`. Any function that takes one value can be called with `of`: `sqrt of 16`, `abs of -3`.

### Saying what a value should be with `as`

You can say what kind of value a function expects, with `as` after the name:

```forge
function heal(amount as number, who as text is "you")
	say "{who} healed {amount}"
end

heal(5)
heal("ten")      # the editor warns: 'heal' takes a number for 'amount' (amount as number), but this is text.
```

- The types are the words of `is a`: `number`, `text`, `boolean`, `list`, `map`, `function`, `vector`,
  `object`, `kind`, or a kind's name (`target as Enemy`).
- **It never changes what runs.** It's a note for you and for the editor (and `forgescript --check`),
  which warns when a call to the function by name gives a plain value (like `"ten"`, `5`, `[1, 2]` or
  `nothing`) of another type.
- A function's own variable is checked too, when its type is certain: a variable that every line
  sets to the same type of plain value (`set name to "Ana"`, then `heal(name)`), or a parameter with
  `as` that the function never sets. Anything less certain (a value worked out, a loop's variable, a
  script variable) isn't checked, so a warning is never a guess.
- A misspelled type word, like `amount as nmber`, is a mistake: "'nmber' isn't a type. Did you mean
  'number'?"
- `as` goes before a default: `who as text is "you"`. A `...` value is always a list, so it has no type.
- Types work in kinds' functions and function values too, as notes; calls are checked for the script's
  own functions.

**What a function gives back** (1.7.0): write `as` and a type after the `)`.

```forge
function health_left(hp as number, damage as number) as number
	return max(hp - damage, 0)
end

function status() as text
	return 5         # the editor warns: 'status' says it gives back text (as text), but this gives back a number.
end
```

- Like the types of values, it never changes what runs.
- The editor warns when a `return` gives a plain value of another type, and when the function has no
  `return` with a value at all ("...it never returns a value, so it always gives back nothing").
- `return nothing` (or a bare `return`) is never warned about: it's how a function says "none found",
  like `function nearest_enemy() as Enemy`.

### Default values

Give a value a default with `is`, and calls may leave it out:

```forge
function attack(target, damage is 10)
	say "{target} takes {damage}"
end

attack("goblin")         # goblin takes 10
attack("dragon", 50)     # dragon takes 50
```

- Values with defaults come after the ones without: `function f(a, b is 1)`, not `function f(a is 1, b)`.
- The default is worked out each time a call leaves the value out, so `bag is []` gives every call its own new list. (In Python, that list would be shared between calls; in ForgeScript it is not.)
- A default can use the values before it and the script's variables: `function hit(damage, crit is damage * 2)`.
- Giving `nothing` is giving a value: `attack("goblin", nothing)` sets `damage` to `nothing`, not 10.
- Defaults work for functions in kinds (including `setup`, so `new Hero("Ron")` can leave out a level), and for functions written as values: `function(x, times is 2) return x * times end`.

### Naming the values in a call

When a function takes several values, you can name them in the call, after the others: the name, a colon, then the value.

```forge
function attack(target, damage is 10, crit is 2)
	say "{target} takes {damage}, or {damage * crit} on a critical hit"
end

attack(goblin, crit: 5)              # damage stays 10, crit is 5
attack(target: goblin, damage: 25)   # any order once they're named
```

- Named values come after the ones given in order: `attack(goblin, crit: 5)`, not `attack(crit: 5, goblin)`.
- A value you skip gets its default, even when a later one is named. A value without a default must be given, in order or by name.
- The names are the function's own parameter names. A misspelled one gets a "Did you mean" hint.
- It works for your functions, functions in kinds (and `new`), function values and `start`. Built-in functions such as `max` take their values in order.

### Any number of values

Put `...` after the last parameter's name, and it collects all the values left over into a list:

```forge
function total(numbers...)
	set sum to 0
	for each n in numbers do
		add n to sum
	end
	return sum
end

say total(1, 2, 3)      # 6
say total()             # 0: numbers is an empty list
say total of 5          # 5

function greet(greeting, names...)
	say "{greeting}, {names.join(" and ")}!"
end
greet("Hi", "Ada", "Bo")   # Hi, Ada and Bo!
```

- Only the last parameter can have `...`, and it can't have a default: with no values left over,
  it is an empty list `[]`.
- The parameters before it work as usual: they can have defaults, and they can be named
  (`spawn("orc", count: 3)` for `function spawn(what, count is 1, tags...)`). The `...` one can't be
  named; its values come in order, after the others.
- It works in kinds' functions and in function values too: `function(xs...) return xs.length end`.

### Variables inside functions

A variable you `set` inside a function belongs to that function and disappears when it finishes, **unless** the script already has a variable with that name:

```forge
set score to 0

function add_point()
	add 1 to score           # changes the script's score
	set bonus to 5           # bonus only exists inside add_point
end
```

### Functions as values

Functions are values, so you can store them and pass them around:

```forge
set double to function(x) return x * 2 end
say double(21)               # 42

function apply_twice(f, value)
	return f(f(value))
end
say apply_twice(double, 3)   # 12
```

A function made inside another function remembers the variables around it:

```forge
function make_counter()
	set count to 0
	return function()
		add 1 to count
		return count
	end
end

set next_id to make_counter()
say next_id()    # 1
say next_id()    # 2
```

---

## 10. Phrases: your own English commands

A **phrase** is a function you call with words instead of brackets. This is what lets ForgeScript scripts read like instructions.

```forge
phrase heal (target) by (amount)
	add amount to target.health
end

set hero to { health: 50 }
heal hero by 20
say hero.health          # 70
```

- Words in the pattern must be typed exactly.
- Each `(name)` is a slot. Whatever you write there becomes that variable.
- Slots can hold any expression: `heal hero by potion_strength * 2`.
- Phrases can give back values with `return`, so they work inside expressions:

```forge
phrase average of (a) and (b)
	return (a + b) / 2
end

say average of 4 and 8     # 6
```

- A phrase can be used anywhere in the script, even before the line that defines it.
- Phrases must start with a word, cannot have two slots in a row, and cannot use the block words (`if`, `for`, `end`, ...).
- The game engine can add its own phrases (for example `move (thing) toward (target) by (amount)`), and they work exactly the same way.

**Built-in phrases:**

```forge
say "Hello"                                 # print a value
say random number from 1 to 6               # dice roll
say random item from ["red", "blue"]        # random pick
say distance from vector(0, 0, 0) to vector(3, 4, 0)    # 5
```

If you start a phrase but do not finish it, ForgeScript shows every way the phrase can be written.

---

## 11. Kinds: making your own objects

A **kind** describes a type of object, like a blueprint. Objects made from it each get their own copy of its fields.

```forge
kind Enemy
	# Fields: every Enemy starts with these.
	set name to "enemy"
	set health to 50

	# 'setup' runs when a new Enemy is made.
	function setup(name)
		set my name to name
	end

	function hurt(amount)
		take amount from my health
		if my health is at most 0 then
			say "{my name} is defeated!"
		end
	end
end

set goblin to new Enemy("Goblin")
goblin.hurt(60)                 # Goblin is defeated!
say goblin.health               # -10
say goblin's name               # Goblin   ('s works like .)
```

- Inside a kind, `set` lines make **fields** and `function` blocks make **methods**.
- Inside a method, `my health` means "this object's health". `self` means "this object".
- `set health to 5` inside a method makes a temporary variable. Use `set my health to 5` to change the field.
- `new Enemy("Goblin")` makes an object and passes the values to `setup`. If a kind has no `setup`, use `new Enemy` or `new Enemy()`.

### Building on another kind with `extends`

```forge
kind Boss extends Enemy
	set health to 300

	function hurt(amount)
		parent.hurt(amount / 2)       # bosses take half damage
	end
end

set dragon to new Boss("Dragon")
dragon.hurt(100)
say dragon.health                     # 250
say dragon is a Boss                  # true
say dragon is a Enemy                 # true
```

`parent.name(...)` runs the version of a method from the kind you extended.

### Describing objects as text

Give a kind a `to_text` method to control how it prints:

```forge
kind Point
	set x to 0
	set y to 0
	function to_text()
		return "({my x}, {my y})"
	end
end
say new Point        # (0, 0)
```

### One value for the whole kind with `shared`

Fields belong to each object: every goblin has its own `health`. Sometimes you want **one** value for
the whole kind, like how many enemies there are. Write it with `shared`:

```forge
kind Enemy
	shared count is 0
	set health to 5
	function setup()
		add 1 to Enemy.count
	end
end

new Enemy()
new Enemy()
say Enemy.count        # 2
```

- Read and change it through the kind's name, `Enemy.count`, everywhere, inside the kind's
  functions too. (A plain `count` inside a function is a variable of its own, as always.)
- It's worked out once, when the kind is made, so `shared names is []` gives one list for all.
- A kind that `extends` Enemy shares the same value: `Boss.count` is `Enemy.count`.
- `goblin.count` (through an object) is an error that tells you to write `Enemy.count`.
- `shared` isn't reserved: `set shared to 2` still makes a field called `shared`.

### Reacting when a field changes

Inside a kind, `when my health changes` runs code every time an object's `health` changes, wherever
the change came from. It's the place for a health bar, a "you died" check or a sound:

```forge
kind Player
	set health to 100
	when my health changes with now, before
		say "health went from {before} to {now}"
		if now is at most 0 then
			trigger player died
		end
	end
end

set hero to new Player()
take 30 from hero.health      # health went from 100 to 70
set hero.health to 70         # nothing: it didn't change
```

- The names after `with` are optional: the first gets the new value, the second the old one.
- It runs right after the change, before the next line. Like a `when` event, it can `wait`; then
  the rest of the script goes on while it waits.
- A field's first value (the kind's `set` lines, when `new` makes the object) isn't a change. A
  change in `setup` is.
- Setting the same field inside its own `when ... changes` doesn't run it again, so
  `set my health to max(now, 0)` inside it is fine.
- A kind that `extends` Player keeps Player's `when ... changes` blocks and can add its own; the
  parent's run first.
- Changing what's *inside* a list field (`add sword to my items`) doesn't change the field itself,
  so it doesn't count.
- This works only inside a kind. At the top level of a script, `when my health changes` is still an
  ordinary event called "my health changes", as it always was.

### Going through an object's fields

`for each` goes through an object's fields, like a map's keys and values:

```forge
kind Enemy
	set health to 5
	set name to "Bo"
	function hit()
		take 1 from my health
	end
end

set goblin to new Enemy()
for each field, value in goblin do
	say "{field} is {value}"     # health is 5, then name is Bo
end
```

- The fields come in the order they were first set: a kind it `extends` first, then its own, then
  any set later (`set goblin.loot to "gold"`).
- With one name, `for each field in goblin`, you get just the field names, as text.
- Functions (`hit`) belong to the kind, so they aren't fields and don't come up.
- Inside the kind, `for each field, value in self do` goes through the object's own fields: handy for
  saving or showing a debug panel.

---

## 12. Events with `when` and `trigger`

Games are full of events: the game starts, a player is hit, a key is pressed. A `when` block runs every time its event happens.

```forge
when game starts
	say "Welcome!"
end

when hit by damage
	take damage from my health
end

when key pressed with key and shift
	if key is "space" and shift then say "Big jump!" end
end
```

- The event name is all the words after `when`, up to `with` or `by`.
- Names after `with` or `by` receive the values that come with the event. Separate them with `,` or `and`.
- MageCap Bellows sends these events to entity scripts: `created`, `update` (with the frame time), `physics update`, `destroyed`, `collided` and `collision ends` (with the other entity), and `entered` and `exited` for triggers. The full table is in [Scripting API: ForgeScript entity scripts](../ScriptingAPI.md#forgescript-entity-scripts).

Scripts can send events too:

```forge
trigger player died
trigger hit with 25
trigger score changed with 100, "bonus"
```

Every matching `when` block runs, right away, before the line after `trigger`. Missing values arrive as `nothing`; extra values are ignored.

A `when` block may trigger events too, but one that keeps triggering itself (or two that trigger each other forever) is stopped after 64 levels, with an error on the `trigger` line of the deepest one:

```forge
when echo
	trigger echo          # Error: The event 'echo' was triggered from inside 64 other events
end
```

Give such handlers a way to stop, like a counter or a condition.

---

## 13. Time: `wait` and `start`

### Waiting

`wait` pauses **only the current task** while the rest of the game keeps running:

```forge
when door opened
	say "The door creaks open..."
	wait 2 seconds
	say "...and slams shut!"
end
```

| Forms of wait | |
|---|---|
| `wait 2 seconds`, `wait 1 second`, `wait 0.5` | seconds (the default) |
| `wait 250 ms` / `milliseconds` | thousandths of a second |
| `wait 1 minute` | minutes |
| `wait 3 frames` | game frames |
| `wait` | one frame |
| `wait until door_open` | check every frame until true |
| `wait for door opened` | until this script gets the event |

### Waiting for an event with `wait for`

`wait for` pauses the task until the script gets an event, the same one a `when` block would hear. It's how you write "do this, then wait until that happens" in one place:

```forge
when "Talk" is clicked
	say "Guard: Halt! Show me your pass."
	wait for pass shown with name
	say "Guard: Welcome, {name}."
end
```

- The names after `with` (or `by`) get the event's values, in order. Missing values are `nothing`.
- `wait for "Start" is clicked` waits for the event from the entity named Start, like a `when "Start" is clicked` block.
- The script's `when` blocks for the event run first, then the tasks waiting for it.
- A `wait for` that starts while its event is being handled waits for the **next** time the event happens.
- An event wait ends when its event comes, even while the game is paused, the same way `when` blocks still run then.
- A script marked Keep Across Scenes keeps its waits across `load_scene`, so `wait for scene loaded with name, path` waits for the next level.

### Timers with `every` and `after`

`every` runs a block again and again on a timer, and `after` runs it once, later. The rest of the script carries on right away:

```forge
set spawned to 0

every 3 seconds
	add 1 to spawned
	say "Spawned enemy number {spawned}"
	if spawned is 5 then
		stop
	end
end

after 10 seconds
	say "The gates are closing!"
end
```

- They take the same times as `wait`: `every 500 ms`, `after 2 minutes`, `every 2 frames`.
- `every` waits first, then runs the block. The next wait starts when the block finishes, so a slow block never piles up.
- The time is worked out again each round, so `every delay seconds` speeds up when you lower `delay`.
- `stop` inside `every` ends the timer. A timer also ends when its script goes, for example when its entity is destroyed.
- Timers count game time. While the game is paused they hold, unless the script is marked Run While Paused.
- They work at the top of a script and inside `when` blocks, functions and a kind's functions.
- `every` and `after` are still fine as names. They only start a timer on a line that ends in a time word, like `seconds` or `frames`.

### Running things at the same time with `start`

`start` runs a function as its own task, so your script carries on right away:

```forge
function blink(count)
	repeat count times
		say "on"
		wait 0.5 seconds
		say "off"
		wait 0.5 seconds
	end
end

start blink(3)
say "This prints immediately, while blinking continues."
```

Every `when` block also runs as its own task, which is why handlers can `wait`.

**Important:** a loop that never waits freezes the game, because nothing else gets a turn. Always put a `wait` inside `repeat forever` and other long-running loops. The engine can stop runaway scripts with an error, and the editor (and `forgescript --check`) warns about a `repeat forever` (or `while true`, or `until false`) that has no `wait`, `stop` or `return` inside and calls nothing that might wait.

Code the engine calls directly and needs an answer from right away (for example, a sorting function you pass to `sort_by`) cannot `wait`. ForgeScript tells you clearly if this happens; use `start` for code that needs to wait.

---

## 14. Handling errors

When something goes wrong (dividing by zero, reading outside a list, calling something that does not exist), ForgeScript stops that task and reports a clear error with the file and line number:

```
Error: Position 5 is outside the list, which has 2 items.
    in 'pick_reward' (loot.forge line 12)
    in loot.forge line 30
```

When a name is misspelled, the error names the one you most likely meant:

```
Error: 'helth' has not been given a value yet. Did you mean 'health'?
    in game.forge line 8
```

This works for your variables and functions, built-in functions, the functions and values of lists, text and maps, a kind's fields and functions, and the names in a script you `use`. A value read with brackets, like `items.length()`, gets told to drop them.

When you write something the way another language (or plain English) says it, the error shows the ForgeScript for it:

| You wrote | The error ends with |
|---|---|
| `x += 1`, `x++` | `To add to a value, write: add 1 to x` |
| `increase score by 5` | `To make a number bigger, write: add 5 to score` |
| `def jump():`, `var x = 1` | `function jump() ... end`, `set x to ...` |
| `elif x is 2 then` | `else if x is 2 then` |
| `if x > 0:` | `Write 'then' after an if, or 'do' after a loop` |
| `a && b`, `a \|\| b` | `ForgeScript writes 'and'` / `'or'` |
| `7 // 2` | `floor(a / b)` |
| `"{price:.2}"` | `{format_number(price, 2)}` |
| `True`, `None`, `null` | `true`, `nothing` |
| `len(items)`, `str(x)`, `int(t)` | `length(list)`, `text(value)`, `number(text)` |
| `items.append(x)`, `name.toUpperCase()` | `add x to list`, `text.upper()` |

Catch errors you expect with `try ... catch`:

```forge
try
	set loaded to load_save_file()
catch problem
	say "Could not load: {problem}"
	set loaded to new_game()
end
```

- The name after `catch` holds the error (usually text). It is optional.
- `try ... end` with no `catch` ignores the error.
- Raise your own errors with `fail`:

```forge
function withdraw(amount)
	if amount is above gold then fail "Not enough gold." end
	take amount from gold
end
```

You can `fail` with any value, such as a map: `fail { code: 404, reason: "missing" }`.

### Cleanup that always runs: finally

Put code after `finally` when it must run however the `try` ends: normally, with an error, or because of `return`, `stop` or `skip`.

```forge
function save_slot(slot)
	show_spinner()
	try
		save_data("slot {slot}", progress)
	catch problem
		say "Saving failed: {problem}"
	finally
		hide_spinner()      # runs whether saving worked or not
	end
end
```

- `finally` goes on its own line, after `catch` if there is one: `try ... catch problem ... finally ... end`.
- With a `catch`, an error in the try code is caught first, then the finally code runs.
- **Without a `catch`, the error is not caught.** The finally code runs and the error goes on, with its original line, to the next `try` around it (or is reported). This is different from `try ... end` with nothing after it, which ignores the error.
- An error inside the catch code goes on in the same way, after the finally code.
- `return` inside the try code works out the value first, then runs the finally code, then returns. So if the try code says `return score` and the finally code says `set score to 0`, you get back the old score.
- A `return`, `stop` or `skip` inside the finally code itself takes over, and an error that was on its way is dropped. Keep finally code to cleanup.
- `finally` is not a reserved word. If a script (or one it brings in with `use`) uses `finally` as a name, a line starting with `finally` keeps its old meaning there.

`assert(condition, "message")` fails with the message when the condition is false. It is useful for checking your own assumptions.

---

## 15. Vectors

A vector holds `x`, `y` and `z`, which is perfect for positions, directions and speeds.

```forge
set position to vector(0, 2, 0)
set velocity to vector(1, 0, 0)

set position to position + velocity * 5
say position                    # (5, 2, 0)
say position.x                  # 5
set position.y to 10
add 1 to position.z
say position.length             # how far from (0, 0, 0)
```

`vector(x, y)` makes `z` zero. `vector()` is `(0, 0, 0)`.

| | |
|---|---|
| `+`, `-` | add / subtract vectors |
| `*`, `/` with a number | scale |
| `-v` | reverse direction |
| `v.length` | size |
| `v.normalized()` | same direction, length 1 |
| `a.dot(b)`, `a.cross(b)` | dot and cross products |
| `a.distance_to(b)`, `distance(a, b)`, `distance from a to b` | distance |
| `a.lerp(b, t)`, `lerp(a, b, t)` | blend from a to b (t from 0 to 1) |
| `v.copy()` / `copy(v)` | a separate copy |

### Game math

These come from GDScript, and they make movement and animation code short:

```forge
# Move 3 units a second toward the target, and stop exactly on it.
when update with delta
	set my position to move_toward(my position, target, 3 * delta)
end

say inverse_lerp(0, 10, 5)            # 0.5: where 5 sits between 0 and 10
say remap(health, 0, 100, 0, 300)     # health as a bar 300 pixels wide
say smoothstep(0, 1, 0.5)             # 0.5, easing in and out between 0 and 1
say wrap(370, 0, 360)                 # 10: angles and looping counters
say snapped(17, 5)                    # 15: lining things up on a grid
```

| | |
|---|---|
| `move_toward(from, to, step)` | from moved toward to by at most step, never past it (numbers or vectors) |
| `inverse_lerp(a, b, value)` | 0 at a, 1 at b: lerp backwards |
| `remap(value, from_low, from_high, to_low, to_high)` | value moved from one range to another |
| `smoothstep(a, b, x)` | 0 before a, 1 after b, a smooth curve between |
| `wrap(value, low, high)` | goes around: high itself becomes low |
| `snapped(value, step)` | the nearest multiple of step |
| `range(from, to, step)` | the list of numbers from from to to, **both included** (unlike Python's `range`), counting down when to is smaller |
| `zip(first, second)` | two lists paired up by position |
| `random_seed(n)` | the same random numbers every time for the same n |

### Easing, noise and colors

```forge
# A menu that pops up in half a second, overshooting a little.
set t to 0.3
say ease(t, "back out")               # a curve from 0 to 1; also "in", "out", "in out", "bounce out" ...

# Gentle wobble: noise changes smoothly as its input moves, from -1 to 1.
set wobble to noise(time() * 2) * 0.1

set orange to color("#ff8800")        # [1, 0.53, 0, 1], the list engine colors take
set sky to hsv(0.6, 0.5, 1)           # hue, saturation and value, each from 0 to 1

if random_chance(0.25) then say "A rare drop!" end
set spot to random_point_in_circle(5) # a point on the ground within 5 units
```

`noise` gives the same value for the same position every time, so a level built from it looks the same on every run; move to another part of it (`noise(x + 100)`) for a different pattern. A script's own function or variable named `ease`, `noise`, `color` or `hsv` still wins over the built-in one.

**Engine positions:** `set my position.x to 5` works on engine objects too. ForgeScript reads the position, changes `x`, and gives the new position back to the engine automatically.

---

## 16. Using other scripts

Put shared code in its own file, for example `helpers.forge`:

```forge
constant VERSION is "1.0"

function double(n)
	return n * 2
end

phrase announce (message)
	say "** {message} **"
end
```

Then use it from another script:

```forge
use "helpers"                 # brings in double, announce and VERSION
say double(21)
announce "It works"

use "helpers" as h            # or keep everything under one name
say h.double(5)
say h.VERSION
```

A script is loaded only once, no matter how many scripts use it, so they all share its variables.

**Where `use` looks.** In a MageCap Bellows project, the path starts at the project's Assets folder, so `use "Scripts/helpers"` means `Assets/Scripts/helpers.forge` from any script. Outside a project, it starts next to the script. `use` only reaches scripts in that place and the folders below it, and only `.forge` scripts: `use "../shared"`, a full path like `use "C:/Games/x"`, or `use "notes.txt"` is a mistake that says so. That keeps a script you got from someone else from reading files elsewhere on your computer.

---

## 17. Scripts attached to entities: `self` and `my`

When the engine attaches a script to an entity (a player, a door, an enemy), that script runs with `self` set to the entity. `my` is shorthand for `self.`:

```forge
# door.forge, attached to a door entity
set is_open to false

when interacted with player
	if is_open then return end
	set is_open to true
	say "{player.name} opens {my name}."
	repeat 30 times
		add 0.1 to my position.y
		wait 1 frame
	end
end
```

Each entity gets its own copy of the script's variables, so two doors using `door.forge` open and close independently.

Top-level lines that set a plain value, like `set is_open to false` or `set speed to 2.5`, appear in the Bellows Inspector as settings you can change for each entity without editing the script. Start a name with `_` (`set _timer to 0`) to keep it out of the Inspector. Write `4.0` instead of `4` when a setting should take decimals in the Inspector: `set speed to 4.0` gives a decimal field, `set lives to 3` a whole-number field.

**Changing a script while the game runs.** Save a script during play and Bellows swaps in the new version right away (hot reload). The script's variables keep their current values, so a door stays open and a score stays where it was. Waiting tasks from the old version stop, and the script receives `when reloaded`, which is the place to start them again:

```forge
when created
	start patrol()
end

when reloaded
	start patrol()      # the old patrol stopped when the new code took over
end
```

The script's top level runs again in the new version, so a line there that creates entities creates them again; put such lines in `when created`. If the new version has a mistake, the game keeps running the old one and the Console shows what to fix.

### Saving progress

`save_data(name, value)` keeps a value after the game closes, and `load_data(name, default)` gets it back the next time. The value can be a number, text, true/false, a vector, `nothing`, or a list or map of these, so one map usually holds a whole save:

```forge
set rent_per_second to 0.5
set save to nothing     # set at the top, so `when created` and `save_progress` share it

when created
	set save to load_data("starfall", { credits: 100, ships: [], saved_at: real_time() })
	# real_time() counts while the game is closed, so the station earns rent while you're away.
	set away to real_time() - save.saved_at
	add rent_per_second * away to save.credits
end

function save_progress()
	set save.saved_at to real_time()
	save_data("starfall", save)
end
```

- The first time, there is no save yet, so `load_data` gives back the default.
- Saves go in the game's own folder (`user_folder()`), in `Saves/<name>.json`. A name has letters, digits, spaces, `-` and `_`, up to 64 characters.
- Saving never leaves half a file, even if the game crashes. A save that can't be read is kept as `<name>.damaged.json`, and `load_data` gives the default, so the game still starts.
- `has_data(name)` tells whether something is saved; `delete_data(name)` removes it (a "New game" button).
- An entity can't be saved, because entities only exist while the game plays. Save its name, or the values you need from it.
- `real_time()` is real-world seconds since 1970; `time()` is seconds since play started.

**Dates.** `date(moment)` turns a `real_time()` moment into a map you can read (`today.weekday`, `today.month_name`, `today.hour` ...), in the player's time zone; `date()` alone is now. `format_date(moment, "Weekday, Month DD")` writes one as text, and `days_between(from, to)` counts calendar days, which is what a daily reward needs:

```forge
when created
	set save to load_data("rewards", { last_claim: 0, streak: 0 })
	set days to days_between(save.last_claim, real_time())
	if days is 1 then
		add 1 to save.streak
		say "Day {save.streak} in a row! Come back tomorrow."
	else if days > 1 then
		set save.streak to 1
		say "Welcome back! Your streak starts again."
	end
	if days >= 1 then
		set save.last_claim to real_time()
		save_data("rewards", save)
	end
	say "Today is " + format_date(real_time(), "Weekday, Month DD")
end
```

`utc_date(moment)` is the same as `date` but in UTC, so it gives the same answer on every computer (good for tests and leaderboards).

---

## 18. Finding bugs with the debugger

When a script does something you did not expect, the MageCap Bellows debugger lets you stop it at a line, look at every variable, and run it one line at a time while the game waits.

Take this script on a player entity, which should give the player a bonus every third coin but never does:

```forge
set coins to 0
set bonus to 0

when collided with other
	if other.name is "Coin" then
		add 1 to coins
		if coins is 3 then
			add 10 to bonus
			set coins to 0
		end
		other.destroy()
	end
end
```

**1. Set a breakpoint.** Open the script in the Script Editor and click just left of line 5 (`if other.name is "Coin" then`), or put the cursor on the line and press **F9**. A red dot shows the breakpoint. Click it again (or press F9 again) to remove it. Breakpoints move with their lines while you edit, and they stay set until you close the project or the editor.

**2. Play in the editor viewport.** Breakpoints stop the game only when it plays inside the editor, not in its own game window. Choose **Game > Play Scene in Editor Viewport** (or turn off **Game > Use a Game Window for Play**, so Play uses the viewport). In a game window the game runs without stopping, and the editor warns you that your breakpoints won't stop it. Then touch a coin. When a script reaches a line with a breakpoint, the game pauses before that line runs. The Script Editor shows the line with an arrow, and the **Debugger** panel (next to the Console; **View > Panels > Debugger** opens it) shows:

- **Stopped at** the script, the line and why it stopped (a breakpoint, a step, or a pause).
- **Call stack**: the function or `when` handler that is running, and the ones that called it, innermost first. Click a call to see its variables.
- **Variables**: the call's own values, such as `other` here, a function's values and its loop variables, and `self` in a kind's functions.
- **Script variables**: the script's own variables, such as `coins` and `bonus`.
- **Breakpoints**: every breakpoint, with buttons to remove them.

Here **Variables** shows `other` as `Entity('Gold Coin')`: the coins in this level are named "Gold Coin", so `other.name is "Coin"` is never true. Writing `other.name.ends_with("Coin")` fixes it.

**3. Step through the code.** While a script is stopped, these buttons in the Debugger panel move it on. The keys work in any panel:

| Button | Key | What it does |
|--------|-----|--------------|
| Continue | F5 | Runs on until the next breakpoint. The game plays again. |
| Step over | F10 | Runs this line, including any functions it calls, and stops at the next line. |
| Step in | F11 | Like step over, but stops inside a function this line calls. |
| Step out | Shift+F11 | Runs to the end of this function and stops in the code that called it. |
| Stop at the next line | | Stops the next ForgeScript line that runs, wherever it is. Use it while the game plays to find out what is running. |

After a `for` or `while` round, stepping goes back to the loop's own line, so you can watch each round. Stepping never stops on an `end` line.

**4. Stop only when it matters, and watch values.** A breakpoint inside `when update` stops 60 times a second, which is rarely what you want. Right-click its red dot (or put the cursor on the line and press **Shift+F9**) to open **Edit Breakpoint**:

- **Stop only when** takes a condition, like `coins is 3`. The game stops at the line only when it's true there. The dot shows `=`.
- **Log a message instead of stopping** prints a message to the Console each time the line runs, and the game goes on. Values go in `{ }`, as in text: `coins is {coins}, bonus is {bonus}`. The dot becomes a diamond. With a condition too, it prints only when the condition is true.
- In the Debugger panel, **Watch** works out the lines you add (`bonus + 10`, `other.name`) in the selected call, at every stop.

Conditions, messages and Watch lines read your variables, the script's variables and engine values such as `my position`. They never call a function, so they can't change the game while you look; a line that tries says so. If a condition has a mistake (a typo, or `nothing.size`), the game stops at that line and the Debugger panel says why, so a broken condition is never skipped without you knowing.

Things worth knowing:

- **A `wait` lets the game go on.** Stepping over `wait 2 seconds` plays the game for two seconds, then stops at the line after it.
- **Each `when` handler runs on its own.** Stepping over a `trigger` line runs its handlers without stopping in them. To look inside one, put a breakpoint in it.
- **Some functions can't stop.** A function given to `map`, `filter`, `sort_by` or a kind's `to_text` runs straight through, because built-in functions need its answer right away. Its breakpoints are passed over.
- **Values are shown as they are.** The debugger never runs your code to show a value, so a kind with a `to_text` function shows its fields instead. Very long values are shortened.
- **Saving while stopped:** changes to scripts reload when the script goes on.
- **Stopping play** while a script is stopped ends it there. `when destroyed` still runs, and it does not stop at breakpoints.

The debugger works in play mode in the MageCap Bellows editor. For checking a script without playing it, the editor (and `forgescript --check` outside it) underlines every mistake as you type. Once there are none, it also marks **warnings**: code that runs but probably doesn't do what you meant. For example, `set max to 10` in a script that also uses `max(a, b)` takes the name `max` away from the built-in, so `max(a, b)` would try to call 10. The warning says so and suggests another name, like `max_value`. A warning never stops a script. For a quick look at a value, `say` still works: it prints to the Console.

**For teams that want no warnings at all:** `forgescript --check --strict game.forge` prints the same as `--check`, but ends with exit code 1 when there is a warning, so a build or CI can stop on it. An editor using the language server can ask for the same with the `strict` setting (`initializationOptions: {"strict": true}`), which shows warnings as mistakes. Neither changes how a script runs.

### Tidying a script's layout

`forgescript --format game.forge` fixes the indentation of a whole script: a tab for each block it's in, with `else`, `otherwise`, `catch` and a `match`'s `is` lines one level out, the way this guide writes them. It also removes spaces at the ends of lines and extra blank lines. It never changes your text, your comments or what the script does. In an editor connected to `forgescript --lsp`, its Format Document command does the same.

### Testing your scripts with `test` and `expect`

A **test** is a small check that your code does what you meant. Write it in the same script, with
`test`, a name, and `expect` lines:

```forge
function damage_after_armor(damage, armor)
	return max(damage - armor, 1)
end

test "armor lowers damage"
	expect damage_after_armor(10, 3) is 7
end

test "a hit always does at least 1"
	expect damage_after_armor(2, 50) is 1
end
```

**Tests don't run when the game runs.** Run them from a terminal with:

```
forgescript --test Scripts/combat.forge
```

It runs the script first (so its functions and kinds exist), then each test, and prints:

```
PASS armor lowers damage
PASS a hit always does at least 1

2 tests: 2 passed, 0 failed.
```

When an `expect` isn't true, the test fails and says what it found:

```
FAIL armor lowers damage
    Error: Expected damage_after_armor(10, 3) is 8, but damage_after_armor(10, 3) is 7.
        in 'test "armor lowers damage"' (combat.forge line 6)
```

- Write tests at the top level of the script. `do` after the name is optional.
- A test stops at its first failed `expect` (or any other error); the next test still runs.
- A test can `wait`: `wait 3 seconds` takes no real time in `--test`, as without `--realtime`.
- Tests run in order and share the script's variables, like the rest of the script.
- `expect` works anywhere, not just in tests: outside a test, a failed `expect` is an error like
  `fail`, which makes it a handy check while you build.
- `forgescript --test` ends with exit code 1 when a test failed, so a build or CI can stop on it.

### Finding slow code

`forgescript --profile game.forge` runs the script, then lists the functions that ran the most
instructions, with their share and how many times each was called. Two more options:

- `--lines` adds the 20 busiest lines over every function, with **about** how many milliseconds
  each took. The time comes from the real clock, sampled every 256 instructions, so it includes
  engine calls (which count as one instruction each) and changes a little from run to run.
  Instructions are the exact measure: they're the same on every machine.
- `--json` gives the whole profile as JSON for tools, with every line of every function, and a
  `"format": 1` field that goes up if a field ever changes meaning. Add `--output profile.json` to
  keep it apart from what the script prints.
- `test` and `expect` aren't reserved: `set test to 3` and a function called `expect` still work.
  Because of that, an `expect` can't start with `(` or `[` (`expect (x)` is a call to `expect`):
  write `expect 2 is in [1, 2]`, not `expect [1, 2] contains 2`.

---

## 19. Rules worth remembering

1. **Positions start at 1.** `list[1]` is the first item, `list[-1]` the last.
2. **`set ... to` changes values. `is` compares them.** There is no `=`.
3. **Every block ends with `end`:** `if`, `for`, `while`, `until`, `repeat`, `match`, `function`, `phrase`, `kind`, `when`, `try`, timers (`every 3 seconds`, `after 2 seconds`) and `test`.
4. **`then` and `do` are optional** but make code easier to read.
5. **Only `false` and `nothing` are false.**
6. **Long loops need `wait`.** Otherwise the game freezes.
7. **Lists, maps, vectors and objects are shared** when you copy a variable. Use `copy(...)` for a separate one.
8. **`set` inside a function makes a local variable**, unless the script already has one with that name. Use `my` for object fields.
9. **Error messages tell you the file, line and what to try.** Read them; they were written to help.

---

## 20. Menus and HUDs

In MageCap Bellows, a menu or a HUD is made of entities: a **UICanvas** holds the screen's UI, and each panel, label and button under it is an entity with a **UIRect** (its place on screen). You build them in the editor (**Create > UI**) and give the menu one script.

### Reacting to buttons

A button raises `clicked` when it's pressed. The event goes to the button's own script first, then to its parent's script, and so on up. So one script on the menu can handle every button, by name:

```forge
# main_menu.forge, attached to the Main Menu canvas
when "Start" is clicked
	load_scene("Scenes/Level1.bscene")
end

when "Quit" is clicked
	quit()
end
```

- `when "Start" is clicked` runs only for the entity named Start (letter case doesn't matter).
- `when clicked with widget` runs for any button under the script's entity. The widget is the entity that was clicked, so `say widget.name` tells you which.
- A widget also raises `focused` and `unfocused` when keyboard or gamepad focus moves to it or away from it, and `cancelled` when the player presses Esc or the gamepad's B while it has focus.

### Changing what's on screen

Every UI entity has these everyday properties:

| Property | What it does |
|---|---|
| `text` | The text of its Text component: a label, or a button's caption. `set score_label.text to "Score: {score}"` |
| `visible` | Whether it shows. Hiding an entity hides everything under it. |
| `interactable` | Whether the pointer and focus can reach it (and everything under it). A button that isn't interactable shows as disabled. |

And these phrases say the same things in English:

```forge
set pause_menu to find_entity("Pause Menu")
set resume_button to find_entity("Resume")

show pause_menu         # set pause_menu.visible to true
hide pause_menu         # set pause_menu.visible to false
enable resume_button    # set resume_button.interactable to true
disable resume_button   # set resume_button.interactable to false
focus resume_button     # gives it keyboard and gamepad focus
```

- `focus` gives back whether the widget could take focus. It can't when it isn't focusable, is hidden, isn't interactable, or sits behind a modal canvas: `if not focus name_box then say "can't type yet" end`.
- Write the widget right after the word, without brackets: `show pause_menu`. A space doesn't change that: `show (pause_menu)` is still the function, so it prints the entity and the menu stays hidden. The editor warns about this one and suggests `show pause_menu`. With brackets, `show(value)` is still ForgeScript's function for seeing a value the way a programmer does (with quotes around text), as it always was. The same holds for any script that has its own function called `hide`, `enable`, `disable` or `focus`: `hide(x)` calls it.
- A phrase given something that isn't an entity says so: "'hide' needs a UI entity, like: hide find_entity("Pause Menu"). This is a number."

### When the UI takes the input

While the pointer is over a panel or button, mouse clicks go to the UI, not to the game. A **modal** canvas (a pause menu, a dialog) takes every key and button while it shows. Two functions tell your game scripts when to leave input alone:

```forge
when update with delta
	if ui_wants_pointer() then return end    # the pointer is over the UI
	if is_mouse_pressed("Left") then shoot() end
end
```

`ui_wants_keyboard()` does the same for keys: it's true while a text box has focus.


### Pausing the game

A pause menu stops the game behind it. Three phrases do that:

```forge
pause the game      # physics, time() and scripts stop
resume the game     # everything picks up where it was
if the game is paused then say "paused" end
```

They are the same as `set_paused(true)`, `set_paused(false)` and `is_paused()`.

While the game is paused:

- `when update` doesn't run, and every `wait` holds. When the game resumes, each wait carries on with the time it had left, so a `wait 3 seconds` that had waited 1 second waits 2 more.
- `time()` stands still. It counts game time, not real time.
- UI events still reach **every** script: `when "Resume" is clicked` works on any menu script.
- Scripts marked **Run While Paused** keep going. Tick it on the entity's Script component in the Inspector. Their `when update` runs and their waits count down in real time, so a pause menu can animate or fade.

Here is a pause menu that works. Its script is on the Pause Menu canvas, a **modal** canvas that starts hidden:

```forge
# pause_menu.forge, on the Pause Menu canvas
when "Resume" is clicked
	hide self
	resume the game
end

when cancelled     # Esc or the gamepad's B on the menu
	hide self
	resume the game
end
```

And one game script opens it:

```forge
# player.forge
when update
	if is_action_pressed("pause") then
		show find_entity("Pause Menu")
		pause the game
	end
end
```

`player.forge` doesn't need Run While Paused: once the game is paused, the menu's own events close it.

**The one trap: waiting in a menu that isn't marked.** A button handler that waits holds at its `wait` while the game is paused, because only marked scripts count time then:

```forge
when "Resume" is clicked
	play_sound("click")
	wait 0.2 seconds      # holds here while paused: the menu never closes
	hide self
	resume the game
end
```

Either tick **Run While Paused** on that script, or resume first and wait after. The Inspector warns about this: on a script that isn't marked, it lists the UI handlers that wait, including waits inside your own functions they call or `start`.

### Moving between scenes

`load_scene("Scenes/Level2.bscene")` changes the level at the end of the frame. Everything in the old scene goes, unless it's **kept**. Keep the things that should carry on, like the player, the music or a game manager:

```forge
# player.forge
when created
	keep self across scenes
end
```

You can also tick **Keep Across Scenes** in the Inspector, or write `set my keep_across_scenes to true`. A kept entity takes its children with it, and keeps:

- its script's values, with no second `when created`;
- its running `wait`s and timers, which finish on time in the new scene;
- its physics, so it keeps moving, and its playing sounds.

It's fine to place the player in every level. When the new scene has the same entity, the kept one stays and the scene's copy is left out, so there are never two players.

Every script hears `scene loaded` once the new scene is in. That's where a kept script finds the new level's things:

```forge
when scene loaded with name, path
	say "Welcome to {name}"
	set my position to find_entity("Spawn Point").position
end
```

A kept script that still holds an entity from the old scene gets a "no longer exists" error when it uses it. Look entities up again in `when scene loaded`.
