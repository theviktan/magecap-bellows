# ForgeScript Language Guide

ForgeScript is a scripting language for games. It reads like plain English, it is short to type, and it was built to drive a game engine: moving entities, reacting to events, waiting for time to pass, and organizing game objects.

This guide teaches the whole language step by step. Every example is real ForgeScript code you can run.

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
18. [Rules worth remembering](#18-rules-worth-remembering)

For a one-page list of everything, see [Reference.md](Reference.md).

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
say "42".is_number()          # true
say number("42") + 1          # 43
say text(42) + "!"            # 42!
```

Text works with every language: `"café".length` is 4.

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
| `a is equal to b` | equal |
| `a is empty` | text, list or map with nothing in it (or nothing itself) |
| `a is in list` / `list contains a` | membership |
| `a is a number` | type check (`number`, `text`, `boolean`, `list`, `map`, `function`, `vector`, `object`, `kind`) |
| `a is a Enemy` | is this object made from the kind `Enemy` (or a kind that extends it)? |

If you prefer symbols, `==`, `!=`, `<`, `<=`, `>`, `>=` also work. A single `=` is **not** used in ForgeScript; the compiler will tell you to use `is` or `to` instead.

### Combining conditions

```forge
if health is above 0 and has_key then say "The door opens." end
if is_night or is_raining then say "Stay inside." end
if not is_alive then say "Respawning..." end
```

`and` and `or` stop early: in `a and b`, if `a` is false, `b` is never checked.

`or` is also handy for default values: `set name to saved_name or "Player"` gives "Player" when `saved_name` is nothing.

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
| `list.map(function)` | New list with the function applied to every item. |
| `list.filter(function)` | New list with only items where the function says true. |
| `list.find(function)` | First item where the function says true. |
| `list.each(function)` | Call the function for every item. |
| `list.copy()` / `copy(list)` | A separate copy. |

```forge
set scores to [40, 95, 70]
say scores.filter(function(s) return s is above 50 end)    # [95, 70]

set monsters to ["dragon", "rat", "wolf"]
monsters.sort_by(function(m) return m.length end)
say monsters                                               # ["rat", "wolf", "dragon"]
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
| `map.length` | Number of keys. |
| `map.clear()`, `map.copy()`, `map.is_empty()` | As for lists. |

Maps can hold functions, which makes them handy as simple objects:

```forge
set actions to { greet: function(who) return "Hi, " + who end }
say actions.greet("Ron")
```

Two maps are equal (`is`) when they have the same keys with equal values, in any order: `{ a: 1, b: 2 } is { b: 2, a: 1 }` is `true`. A list or map used as a **key** means that one particular list or map, not any list with the same items.

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
- A function must be given exactly the number of values it asks for.
- A function with no `return` gives back `nothing`.
- `length of items` means `length(items)`. Any function that takes one value can be called with `of`: `sqrt of 16`, `abs of -3`.

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

Every matching `when` block runs. Missing values arrive as `nothing`; extra values are ignored.

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

**Important:** a loop that never waits freezes the game, because nothing else gets a turn. Always put a `wait` inside `repeat forever` and other long-running loops. The engine can stop runaway scripts with an error.

Code the engine calls directly and needs an answer from right away (for example, a sorting function you pass to `sort_by`) cannot `wait`. ForgeScript tells you clearly if this happens; use `start` for code that needs to wait.

---

## 14. Handling errors

When something goes wrong (dividing by zero, reading outside a list, calling something that does not exist), ForgeScript stops that task and reports a clear error with the file and line number:

```
Error: Position 5 is outside the list, which has 2 items.
    in 'pick_reward' (loot.forge line 12)
    in loot.forge line 30
```

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

---

## 18. Rules worth remembering

1. **Positions start at 1.** `list[1]` is the first item, `list[-1]` the last.
2. **`set ... to` changes values. `is` compares them.** There is no `=`.
3. **Every block ends with `end`:** `if`, `for`, `while`, `until`, `repeat`, `function`, `phrase`, `kind`, `when`, `try`.
4. **`then` and `do` are optional** but make code easier to read.
5. **Only `false` and `nothing` are false.**
6. **Long loops need `wait`.** Otherwise the game freezes.
7. **Lists, maps, vectors and objects are shared** when you copy a variable. Use `copy(...)` for a separate one.
8. **`set` inside a function makes a local variable**, unless the script already has one with that name. Use `my` for object fields.
9. **Error messages tell you the file, line and what to try.** Read them; they were written to help.
