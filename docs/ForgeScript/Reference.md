# ForgeScript Quick Reference

Everything in the language on one page. For explanations and examples, see the [Language Guide](LanguageGuide.md).

## Statements

| Statement | Example |
|---|---|
| Make / change a variable | `set health to 100` |
| Change a property or item | `set enemy.health to 0`, `set list[2] to "x"`, `set my speed to 3` |
| Constant (top level only) | `constant MAX_HP is 100` |
| Add / subtract | `add 5 to score`, `take 10 from health` |
| Add to / remove from a list | `add "key" to items`, `take "key" from items` |
| Multiply / divide | `multiply speed by 2`, `divide damage by 4` |
| Show a value | `say "Hello"`, `print(a, b, c)` |
| If | `if x is 1 then ... else if ... else ... end` (`otherwise` = `else`) |
| Repeat | `repeat 3 times ... end`, `repeat forever ... end` |
| Count | `for i from 1 to 10 [by 2] do ... end` |
| Each | `for each item [, position] in list do ... end` (maps: `key, value`) |
| While / until | `while x is below 5 do ... end`, `until done do ... end` |
| Leave / skip a loop | `stop`, `skip` |
| Function | `function name(a, b) ... return x ... end` |
| Anonymous function | `function(x) return x * 2 end` |
| Phrase | `phrase heal (target) by (amount) ... end` → `heal hero by 5` |
| Kind | `kind Enemy [extends Base] set field to 1  function method() ... end end` |
| New object | `new Enemy("Goblin")`, `new Point` |
| Event handler | `when hit by damage ... end`, `when key pressed with key and shift ... end` |
| Event from one entity | `when "Start" is clicked ... end`: runs only when the entity named Start (any letter case) raised the event, like a UI widget's events on their way up to its parents' scripts |
| Send an event | `trigger hit with 25`, `trigger "game over"` |
| Wait | `wait 2 seconds`, `wait 250 ms`, `wait 3 frames`, `wait`, `wait until ready` |
| Run as a task | `start patrol()`, `start guard.patrol(3)` |
| Errors | `try ... catch problem ... end`, `fail "message"` |
| Use another script | `use "helpers"`, `use "helpers" as h` |
| Comments | `# line`, `#[ block ]#` |

## Expressions

| | |
|---|---|
| Math | `+ - * / % ^` and `-x`. `%` is the remainder, `^` is power. |
| Join text | `"a" + "b"`, `"Score: " + 10`, `"Hi {name}!"` |
| Equal | `a is b`, `a is equal to b`, `a == b` (lists and maps: same items; kinds' objects: the same object) |
| Not equal | `a is not b`, `a != b` |
| Greater | `a is above b`, `a is greater than b`, `a > b` |
| Less | `a is below b`, `a is less than b`, `a < b` |
| At least / most | `a is at least b` (`>=`), `a is at most b` (`<=`) |
| Membership | `list contains x`, `x is in list`, `x is not in list` (also maps and text) |
| Empty | `x is empty`, `x is not empty` |
| Type | `x is a number` / `text` / `boolean` / `list` / `map` / `function` / `vector` / `object` / `kind`, `x is a Enemy`, `x is an Enemy` |
| Logic | `and`, `or`, `not` |
| Property | `player.health`, `player's health`, `my health` (= `self.health`) |
| Index | `list[1]`, `list[-1]`, `map["key"]`, `text[1]` |
| Call | `f(1, 2)`, `object.method()`, `length of list` (= `length(list)`) |
| Parent method | `parent.setup(name)` inside a kind |

Precedence from loosest to tightest: `or` → `and` → `not` → comparisons → `+ -` → `* / %` → unary `-` → `^` → `of` → `.` `[ ]` `( )`.

## Values

`nothing` · `true` / `yes` · `false` / `no` · numbers (`42`, `3.5`, `1_000`, `0xFF`, `1e3`) · text (`"..."`) · lists (`[1, 2]`) · maps (`{ key: value }`) · vectors (`vector(x, y, z)`) · functions · kinds and objects.

Only `false` and `nothing` are false. Positions start at **1**; negative positions count from the end.

Text escapes: `\n` `\t` `\r` `\"` `\\` `\{` `\}` `\0`.

## Built-in functions

| Group | Functions |
|---|---|
| Output | `print(...)`, `say (value)`, `show(value)` |
| Types | `text(x)`, `number(x)` (nothing if not a number), `type(x)`, `length(x)`, `copy(x)`, `assert(condition, message)` |
| Formatting | `format_number(x [, decimals])` (`"1,250.50"`), `format_time(seconds)` (`"1:05"`, `"1:02:03"`) |
| Math | `abs`, `floor`, `ceil`, `round(x [, digits])`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2(y, x)`, `exp`, `log(x [, base])`, `sign`, `min(...)`, `max(...)`, `clamp(x, low, high)`, `lerp(a, b, t)`, `radians`, `degrees`, `pi`, `infinity` |
| Random | `random()` (0 to 1), `random(n)` (1 to n), `random(a, b)`, `random number from (a) to (b)`, `random item from (list)` |
| Time | `time()` (seconds since start), `frame()` |
| Vectors | `vector(x, y [, z])`, `distance(a, b)`, `distance from (a) to (b)` |

## Methods

**Text:** `length` · `upper()` · `lower()` · `trim()` · `split([separator])` · `contains(x)` · `starts_with(x)` · `ends_with(x)` · `find(x)` · `replace(a, b)` · `slice(start [, end])` · `repeat(n)` · `pad_start(width [, fill])` · `pad_end(width [, fill])` · `characters()` · `is_number()` · `is_empty()`

**Lists:** `length` · `first` · `last` · `add(x)` · `insert(position, x)` · `remove(x)` · `remove_at(position)` · `pop()` · `clear()` · `contains(x)` · `index_of(x)` · `is_empty()` · `copy()` · `reverse()` · `shuffle()` · `pick()` · `sort()` · `sort_by(f)` · `join([separator])` · `slice(start [, end])` · `sum()` · `average()` · `min()` · `max()` · `count(f or value)` · `any(f)` · `all(f)` · `map(f)` · `filter(f)` · `find(f)` · `each(f)`

**Maps:** `length` · `keys()` · `values()` · `has(key)` · `get(key [, default])` · `remove(key)` · `clear()` · `copy()` · `is_empty()`

**Vectors:** `x` · `y` · `z` · `length` · `normalized()` · `dot(v)` · `cross(v)` · `distance_to(v)` · `lerp(v, t)` · `copy()`

**Kinds:** `Kind.name`. Objects can define `setup(...)` (runs on `new`) and `to_text()` (how they print).

## Reserved words

These cannot be used as variable or function names (they can be used as property names, like `map.end`):

`add` `and` `by` `catch` `constant` `contains` `divide` `do` `each` `else` `end` `extends` `fail` `false` `for` `from` `function` `if` `in` `is` `kind` `multiply` `my` `new` `no` `not` `nothing` `of` `or` `otherwise` `parent` `phrase` `repeat` `return` `self` `set` `skip` `start` `stop` `take` `then` `to` `trigger` `true` `try` `until` `use` `wait` `when` `while` `with` `yes`

Words such as `times`, `forever`, `seconds`, `frames`, `above`, `below`, `least`, `most`, `than`, `empty`, `a`, `an` and `as` only have a special meaning in their own spot, so you can still use them as names.

## Scope rules

- At the top of a script, `set` makes a script variable that every function in the script can see.
- Inside a function, `set` makes a local variable, unless the script (or an outer function) already has a variable with that name.
- Loop variables (`for each x`, `for i from`) and `catch` names belong to their block.
- Top-level `function` and `phrase` definitions can be used anywhere in the file, even above the line that defines them.
- Inside a kind's functions, use `my field` (or `self.field`) for the object's fields.

## Tasks and time

- Every `when` handler and every `start` runs as its own task.
- `wait` pauses only the current task. The engine moves time forward once per frame.
- Code called directly by the engine or by a built-in function (such as the function given to `sort_by`) must finish right away and cannot `wait`.
- A task that runs too long without waiting is stopped with an error when the engine sets an instruction limit.
