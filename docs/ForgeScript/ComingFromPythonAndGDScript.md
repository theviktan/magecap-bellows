# Coming from Python or GDScript

If you already write Python or GDScript (Godot 4), most of ForgeScript will feel familiar: it has variables, functions, lists, dictionaries, classes and events. This page shows the same code in all three languages, so you can read ForgeScript in terms you know. Every ForgeScript example here is run by ForgeScript's tests (`ForgeScript/Tests/Scripts/33_coming_from_python_and_gdscript.forge`), so it works as written.

For the full language, read the [Language Guide](LanguageGuide.md). For the exact rules, see the [Specification](Specification.md).

## The big differences first

These are the ones that catch people out. Read them before anything else.

| | Python | GDScript | ForgeScript |
|---|---|---|---|
| First item of a list | `items[0]` | `items[0]` | `items[1]` (positions start at **1**; `items[-1]` is the last) |
| Blocks | indentation and `:` | indentation and `:` | `end` closes every block; indentation is only for reading |
| Counts as false | `False`, `None`, `0`, `""`, `[]`, `{}` | `false`, `null`, `0`, `""`, `[]`, `{}` | only `false` and `nothing`; `0`, `""` and `[]` count as **true** (test with `is empty`) |
| `7 / 2` | `3.5` | `3` (both are whole numbers) | `3.5` (every number is a decimal number; use `floor(7 / 2)` for `3`) |
| `-7 % 3` | `2` | `-1` | `2` (the sign of the right side, like Python) |
| Power | `2 ** 10` | `2 ** 10` | `2 ^ 10` |
| Change a variable | `score += 10` | `score += 10` | `add 10 to score` (also `take`, `multiply`, `divide`) |
| Equal | `a == b` | `a == b` | `a is b` (`==` works too); lists and maps compare by what they hold |
| Nothing | `None` | `null` | `nothing` |
| Comments | `# ...` | `# ...` | `# ...`, and `#[ ... ]#` across lines |

A few more habits to drop:

- **No `:` at the end of a line, and no semicolons.** One statement per line.
- **Words, not symbols.** `and`, `or` and `not` are the only logic operators (no `&&`, `||` or `!`).
- **Letter case matters** in names, and words like `set`, `if` and `end` are lowercase.

## Variables and text

```python
# Python
score = 0
score += 10
name = "Ada"
print(f"Hello, {name}! Score: {score}")
MAX_HP = 100        # a constant by convention only
```

```gdscript
# GDScript
var score = 0
score += 10
var name = "Ada"
print("Hello, %s! Score: %d" % [name, score])
const MAX_HP = 100
```

```forge
# ForgeScript
set score to 0
add 10 to score
set name to "Ada"
say "Hello, {name}! Score: {score}"      # Hello, Ada! Score: 10
constant MAX_HP is 100
```

- `set ... to` both makes a variable and changes it; there is no `var`.
- Every text can hold `{ }` placeholders, like a Python f-string, without a prefix.
- `constant` works only at the top of a script, and nothing can change it afterward.
- For an `enum` (Python's `class State(Enum)`, GDScript's `enum State { IDLE, RUN }`), write
  `choices State are idle, running`. `State.idle` is the text `"idle"`, not a number, so it prints
  and saves as its name.

## Decisions

```python
# Python
if hp < 25:
    print("low")
elif hp < 50:
    print("hurt")
else:
    print("fine")
size = "big" if hp > 20 else "small"
```

```gdscript
# GDScript
if hp < 25:
    print("low")
elif hp < 50:
    print("hurt")
else:
    print("fine")
var size = "big" if hp > 20 else "small"
```

```forge
# ForgeScript
if hp is below 25 then
	say "low"
else if hp is below 50 then
	say "hurt"
else
	say "fine"
end
set size to "big" if hp > 20 otherwise "small"
```

`<`, `>`, `<=` and `>=` work too, and so do the word forms: `is below`, `is above`, `is at least`, `is at most`, and `is between 1 and 10`. `then` is optional.

### `match`

```python
# Python 3.10
match weapon:
    case "sword":
        print("Slash!")
    case "bow" | "crossbow":
        print("Twang!")
    case _:
        print("Thud.")
```

```gdscript
# GDScript
match weapon:
    "sword":
        print("Slash!")
    "bow", "crossbow":
        print("Twang!")
    _:
        print("Thud.")
```

```forge
# ForgeScript
match weapon
is "sword" then say "Slash!"
is "bow", "crossbow" then say "Twang!"
otherwise say "Thud."
end
```

A choice can also be a comparison: `is above 100`, `is between 1 and 10`, `is in [1, 2, 3]`.

## Loops

```python
# Python
for i in range(3):            # 0, 1, 2
    print(i)
for position, enemy in enumerate(enemies, start=1):
    print(position, enemy)
while n > 0:
    n -= 1
for _ in range(2):
    print("again")
```

```gdscript
# GDScript
for i in range(3):            # 0, 1, 2
    print(i)
for i in enemies.size():
    print(i + 1, enemies[i])
while n > 0:
    n -= 1
for _i in 2:
    print("again")
```

```forge
# ForgeScript
for i from 0 to 2             # 0, 1, 2: both ends are included
	say i
end
for each enemy, position in enemies
	say "{position}: {enemy}"
end
while n is above 0
	take 1 from n
end
repeat 2 times
	say "again"
end
```

- `for i from 1 to 10` **includes** 10, unlike `range`. Add `by 2` to change the step; it counts down when the end is smaller.
- `for each` takes the item first, then (optionally) its position, which starts at 1.
- `stop` and `skip` are `break` and `continue`. `repeat forever` is `while True:`.

## Lists

```python
# Python
party = ["Ada", "Bo"]
party.append("Cy")
first, last = party[0], party[-1]
count = len(party)
has_bo = "Bo" in party
doubled = [x * 2 for x in [1, 2, 3]]
a, b = b, a
```

```gdscript
# GDScript
var party = ["Ada", "Bo"]
party.append("Cy")
var first = party[0]
var last = party[-1]
var count = party.size()
var has_bo = "Bo" in party
var doubled = [1, 2, 3].map(func(x): return x * 2)
var t = a; a = b; b = t
```

```forge
# ForgeScript
set party to ["Ada", "Bo"]
add "Cy" to party
set first to party[1]
set last to party[-1]
set count to party.length
set has_bo to party contains "Bo"
set doubled to [1, 2, 3].map(function(x) return x * 2 end)
set a, b to [b, a]
```

Lists are shared, not copied, as in both other languages: `set b to a` gives you the same list. Use `copy(a)` for a separate one.

## Dictionaries (maps)

```python
# Python
hero = {"name": "Ada", "hp": 50}
hero["hp"] = 40
mana = hero.get("mana", 0)
for key, value in hero.items():
    print(key, value)
```

```gdscript
# GDScript
var hero = {"name": "Ada", "hp": 50}
hero["hp"] = 40
var mana = hero.get("mana", 0)
for key in hero:
    print(key, hero[key])
```

```forge
# ForgeScript
set hero to { name: "Ada", hp: 50 }
set hero.hp to 40
set mana to hero.get("mana", 0)
for each key, value in hero
	say "{key} = {value}"
end
```

- A name before `:` is a text key, so `{ name: "Ada" }` means `{"name": "Ada"}`. `hero.hp` and `hero["hp"]` are the same.
- Reading a key that isn't there gives `nothing` instead of an error.
- Maps remember the order keys were added, like Python's dicts.

## Functions

```python
# Python
def attack(target, damage=10, crit=1):
    return f"{target} takes {damage * crit}"

attack("goblin", crit=2)
double = lambda x: x * 2
```

```gdscript
# GDScript
func attack(target, damage = 10, crit = 1):
    return "%s takes %d" % [target, damage * crit]

attack("goblin", 10, 2)        # no named arguments
var double = func(x): return x * 2
```

```forge
# ForgeScript
function attack(target, damage is 10, crit is 1)
	return "{target} takes {damage * crit}"
end

attack("goblin", crit: 2)
set double to function(x) return x * 2 end
```

- A default is worked out at each call that leaves its value out, so `function add_to(list is [])` gives a new list every time. (In Python, that same default would be shared between calls.)
- Named values use `:`, as in `crit: 2`, after the values given in order.

### Phrases: your own commands

ForgeScript has one thing neither of the others has: a **phrase** is a function you call with words.

```forge
phrase heal (target) by (amount)
	add amount to target.hp
end

heal hero by 5
```

## Classes (kinds)

```python
# Python
class Enemy:
    def __init__(self, name):
        self.name = name
        self.health = 50

    def hurt(self, amount):
        self.health -= amount

class Boss(Enemy):
    def __init__(self, name):
        super().__init__(name)
        self.health = 300

    def hurt(self, amount):
        super().hurt(amount / 2)

dragon = Boss("Dragon")
isinstance(dragon, Enemy)
```

```gdscript
# GDScript
class Enemy:
    var name = "enemy"
    var health = 50
    func _init(new_name):
        name = new_name
    func hurt(amount):
        health -= amount

class Boss extends Enemy:
    func _init(new_name):
        super(new_name)
        health = 300
    func hurt(amount):
        super.hurt(amount / 2)

var dragon = Boss.new("Dragon")
dragon is Enemy
```

```forge
# ForgeScript
kind Enemy
	set name to "enemy"
	set health to 50
	function setup(name)
		set my name to name
	end
	function hurt(amount)
		take amount from my health
	end
end

kind Boss extends Enemy
	set health to 300
	function hurt(amount)
		parent.hurt(amount / 2)
	end
end

set dragon to new Boss("Dragon")
say dragon is a Enemy                # true
```

| Python | GDScript | ForgeScript |
|---|---|---|
| `class Name:` | `class Name:` / `class_name Name` | `kind Name ... end` |
| `__init__` | `_init` | `setup` |
| `self.health` | `health` / `self.health` | `my health` / `self.health` |
| `super().hurt(x)` | `super.hurt(x)` | `parent.hurt(x)` |
| `Boss("Dragon")` | `Boss.new("Dragon")` | `new Boss("Dragon")` |
| `__str__` | `_to_string` | `to_text` |
| class variable `count = 0` | `static var count = 0` | `shared count is 0`, read as `Enemy.count` |

- `set` lines at the top of a kind are fields: every object gets its own copy, worked out when it is made. A kind that extends another starts from its parent's fields, so `Boss` doesn't need its own `setup`.
- Inside a method, a plain `set health to 5` makes a local variable. Write `set my health to 5` to change the field (like Python's `self.`).

## Errors

```python
# Python
try:
    risky()
except Exception as problem:
    print(f"Caught: {problem}")
finally:
    print("always")
raise ValueError("out of arrows")
```

```gdscript
# GDScript has no exceptions: functions return an error code instead.
var result = risky()
if result != OK:
    print("Caught: ", error_string(result))
```

```forge
# ForgeScript
try
	risky()
catch problem
	say "Caught: {problem}"
finally
	say "always"
end
fail "out of arrows"
```

`catch problem` gets the message as text for ForgeScript's own errors, or whatever value was given to `fail`.

When a habit from Python or GDScript slips in (`x += 1`, `elif`, `True`, `len(items)`, `items.append(x)`), ForgeScript's error ends with the ForgeScript for it, so you learn the new way as you go.

## Events (signals)

```python
# Python in MageCap Bellows
def on_event(self, name, *values):
    if name == "hit":
        print(f"Ouch, {values[0]}!")

bellows.emit("hit", 25)
```

```gdscript
# GDScript
signal hit(damage)

func _ready():
    hit.connect(_on_hit)

func _on_hit(damage):
    print("Ouch, %d!" % damage)

# elsewhere
hit.emit(25)
```

```forge
# ForgeScript
when hit by damage
	say "Ouch, {damage}!"
end

trigger hit with 25
```

- Event names can be several words: `when key pressed with key and shift`.
- Every matching `when` block, in every script, runs right away, before the line after `trigger`.
- The engine's own events are `when` blocks too: `when created`, `when update with delta`, `when collided with other`, `when "Start" is clicked`. They are listed in the [Scripting API](../ScriptingAPI.md#forgescript-entity-scripts).

## Waiting

```gdscript
# GDScript
await get_tree().create_timer(2.0).timeout
print("two seconds later")
```

```forge
# ForgeScript
wait 2 seconds
say "two seconds later"
```

`wait` pauses only the code that's waiting; the rest of the game keeps running. There is no `await` or `async` to write. Every `when` block already runs on its own, and `start patrol()` runs a function on its own without waiting for it.

Waiting for a signal, and timers:

```gdscript
# GDScript
await door_opened                       # wait for a signal
var timer = Timer.new()                 # repeat every 3 seconds
timer.wait_time = 3.0
timer.timeout.connect(_spawn)
add_child(timer)
timer.start()
```

```forge
# ForgeScript
wait for door opened

every 3 seconds
	spawn()
end
```

`wait for` takes the event's values too (`wait for hit with damage`). `after 2 seconds ... end` runs a block once, later. `stop` inside `every` ends the timer.

## Other scripts

```python
# Python
import helpers
from helpers import double
```

```gdscript
# GDScript
const Helpers = preload("res://Scripts/helpers.gd")
```

```forge
# ForgeScript
use "Scripts/helpers" as helpers     # helpers.double(2)
use "Scripts/helpers"                # double(2) directly
```

In a MageCap Bellows project, paths start at the Assets folder, as `res://` does in Godot.

## Where to go next

- The [Language Guide](LanguageGuide.md) teaches everything in order, with examples.
- The [Quick Reference](Reference.md) has every statement, operator, function and method on one page.
- [Scripting API](../ScriptingAPI.md) lists what the engine gives ForgeScript and Python alike: entities, components, input, UI, audio and more.
