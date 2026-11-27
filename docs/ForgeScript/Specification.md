# MageCap ForgeScript Language Specification

This is the exact description of ForgeScript **1.6.0**: what a script may contain and what it means. Use it when you need a precise answer, when you build a tool that reads ForgeScript, or when you change the language.

- To **learn** ForgeScript, read the [Language Guide](LanguageGuide.md), which explains everything with examples.
- To **look something up** quickly, use the one-page [Quick Reference](Reference.md).
- To **put ForgeScript in a program**, see [Embedding](Embedding.md).

Where this document and the implementation disagree, that is a bug in one of them. Please report it.

**The compatibility rule.** A new version of ForgeScript never changes what a working script does. New words are contextual (see [Words](#24-words)), new forms use lines that were mistakes before, and anything that changes meaning waits for a major version. Every release is checked by running every script in the repository with the old and the new `forgescript` and comparing their output.

## Contents

1. [Notation](#1-notation)
2. [Source text](#2-source-text)
3. [Values](#3-values)
4. [Expressions](#4-expressions)
5. [Statements](#5-statements)
6. [Names and scope](#6-names-and-scope)
7. [Functions](#7-functions)
8. [Phrases](#8-phrases)
9. [Kinds and objects](#9-kinds-and-objects)
10. [Events and tasks](#10-events-and-tasks)
11. [Errors](#11-errors)
12. [Using other scripts](#12-using-other-scripts)
13. [Mistakes and warnings](#13-mistakes-and-warnings)
14. [Limits](#14-limits)

---

## 1. Notation

Grammar rules use this notation:

| Notation | Means |
|---|---|
| `set` | the word itself, typed exactly |
| *expression* | a part described elsewhere in this document |
| [ *x* ] | *x* may be left out |
| { *x* } | *x* any number of times, including none |
| *a* \| *b* | either *a* or *b* |
| NEWLINE | the end of a line |
| *block* | any number of statements, each on its own line |

---

## 2. Source text

### 2.1 Characters and lines

A script is UTF-8 text. Text in quotes that holds bytes that aren't UTF-8 (a file saved as Windows-1252, say) still runs as it always has, but the checker warns, because it shows up garbled. Letter case matters everywhere: `Set` is not `set`, and `score` and `Score` are two different names.

A script is a sequence of **lines**. A line break ends a statement, except:

- inside brackets: `( )`, `[ ]` and `{ }`, where line breaks are ignored;
- after a backslash at the very end of a line, which joins the line with the next one;
- after an operator or `and` / `or` at the end of a line, which continues on the next line (`say 1 +` followed by `2` on the next line is `say 1 + 2`). A line that *starts* with an operator does not continue the previous one.

Spaces and tabs separate words and are otherwise ignored. Indentation has no meaning; blocks end with `end`, not with less indentation.

There is no statement separator: `;` is not allowed, and two statements may not share a line (`set x to 1 set y to 2` is a mistake). The one exception is a block written on one line, like `if ready then say "Go!" end`.

### 2.2 Comments

- `#` starts a comment that runs to the end of the line.
- `#[` starts a block comment that runs to the next `]#`, across any number of lines. Block comments do not nest.

### 2.3 Names

A **name** starts with a letter (`a` to `z`, `A` to `Z`) or `_`, followed by letters, digits (`0` to `9`) and `_`. Names are used for variables, functions, phrases' slots, kinds, fields and events.

### 2.4 Words

**Reserved words** cannot be used as names of variables or functions. They can be property names (`map.end`) and map keys (`{if: 1}`):

`add` `and` `by` `catch` `constant` `contains` `divide` `do` `each` `else` `end` `extends` `fail` `false` `for` `from` `function` `if` `in` `is` `kind` `multiply` `my` `new` `no` `not` `nothing` `of` `or` `otherwise` `parent` `phrase` `repeat` `return` `self` `set` `skip` `start` `stop` `take` `then` `to` `trigger` `true` `try` `until` `use` `wait` `when` `while` `with` `yes`

**Contextual words** have a meaning only in one spot and are ordinary names everywhere else:

| Word | Its spot |
|---|---|
| `times`, `forever` | `repeat 3 times`, `repeat forever` |
| `seconds`, `second`, `ms`, `milliseconds`, `minute`, `minutes`, `frames`, `frame` | after the amount in `wait` |
| `above`, `below`, `greater`, `less`, `than`, `least`, `most`, `equal`, `between`, `empty` | after `is` (comparisons, [4.4](#44-comparisons)) |
| `a`, `an` | type tests: `x is a number` |
| `even`, `odd`, `whole` | number tests after `is`: `x is even`, `x is a whole number` |
| `the`, `first`, `last`, `item` | list words in a value: `the length of items`, `first of items`, `item 2 of items` |
| `every`, `any` | `every x in list is ...`, `any x in list is ...`; `every` also starts a timer |
| `choices`, `are` | `choices State are idle, running` at the start of a line |
| `shared` | `shared count is 0` at the start of a line inside a kind |
| `test`, `expect` | `test "name"` at the start of a line; `expect` followed by a value |
| `as` | `use "helpers" as h` |
| `match` | the start of a match block ([5.6](#56-match)) |

Because these words are contextual, `set times to 3` and `set match to 1` are fine.

### 2.5 Numbers

- Decimal: `42`, `3.5`, `0.25`. A number starts with a digit, so write `0.5`, not `.5`.
- Exponent: `1e3`, `2.5e-4`, `1E+6`.
- Hexadecimal: `0xFF`, `0x1f` (any letter case after `0x`).
- `_` may separate digits for reading: `1_000_000`, `0xFF_FF`.

A minus sign is not part of a number: `-5` is the operator `-` applied to `5`.

### 2.6 Text

Text is written between double quotes: `"Hello"`. Single quotes are not used for text; `'s` is the possessive ([4.7](#47-properties-positions-and-calls)).

**Escapes:** `\n` new line, `\t` tab, `\r` carriage return, `\"` quote, `\\` backslash, `\{` and `\}` braces, `\0` the zero character.

**Placeholders:** `{` *expression* `}` inside text is replaced by the value of the expression, turned into text as `text()` would ([3.2](#32-text-forms-of-values)): `"Score: {score * 10}"`. Placeholders may hold any expression, including text with its own placeholders.

**Long text** starts with `"""` and ends with `"""`, over any number of lines:

- `"` and `""` need no escape inside it. Escapes and placeholders work as in other text.
- It ends at the last three quotes of a run, so `"""a "b""""` is `a "b"`.
- A line break right after the opening `"""` is not part of the text, and neither is a last line holding only the closing `"""`.
- The spaces and tabs that every line starts with (the indentation of the code around it) are removed.
- Every line ends with a plain new line (`\n`), even in a file with Windows line endings.

---

## 3. Values

Every value has one of these types. `type(x)` gives the type's name as text.

| Type | `type(x)` | Written as |
|---|---|---|
| nothing | `"nothing"` | `nothing` |
| boolean | `"boolean"` | `true` or `yes`, `false` or `no` |
| number | `"number"` | `42`, `3.5`, `0xFF` |
| text | `"text"` | `"Hello"` |
| list | `"list"` | `[1, 2, 3]`, `[]` |
| map | `"map"` | `{ name: "Bo", hp: 10 }`, `{}` |
| vector | `"vector"` | `vector(1, 2, 3)` |
| function | `"function"` | a function's name, or `function(x) ... end` |
| kind | `"kind"` | a kind's name |
| object | `"object"` | `new Enemy()` |

A host program can add its own types; MageCap Bellows adds entities and engine types such as transforms.

- **Numbers** are 64-bit floating-point numbers (IEEE 754 doubles). There is no separate integer type. Whole numbers are exact up to 2^53. Arithmetic follows IEEE 754, so `0.1 + 0.2 is 0.3` is `false`, even though both print as `0.3`.
- **Text** is a sequence of Unicode characters. `length` counts characters, not bytes (`"😀".length` is `1`). Text cannot be changed in place; operations make new text.
- **Lists** hold any values, in order. Positions start at **1**; `-1` is the last item, `-2` the one before it.
- **Maps** hold values under keys and remember the order keys were added. Keys may be text, numbers, booleans or vectors. In `{ name: 1 }` the key is the text `"name"`; use `m[1]` for a number key. Reading a missing key gives `nothing`.
- **Vectors** have three numbers, `x`, `y` and `z`. `vector(x, y)` makes `z` zero.
- **Lists, maps and objects are shared, not copied.** `set b to a` makes `b` the same list as `a`, so changing one changes the other. `copy(a)` or `a.copy()` makes a separate copy.

### 3.1 True and false

Only `false` and `nothing` count as false. Everything else counts as true, including `0`, `""` and `[]`. Use `is empty` to test for empty text, lists and maps.

### 3.2 Text forms of values

`say`, `text()`, placeholders and `+` with text turn values into text this way:

| Value | Text |
|---|---|
| nothing | `nothing` |
| boolean | `true`, `false` |
| number | up to 14 significant digits, without trailing zeros: `3`, `3.5`, `0.33333333333333`; very large or small numbers use an exponent: `1e+21`, `1.2345678901235e+17`; `infinity`, `-infinity` |
| text | itself (inside a list or map, in quotes: `["a"]`) |
| list | `[1, "a", nothing]` |
| map | `{"b": 1, "a": 2}`, in the order keys were added |
| vector | `(1, 2, 3)` |
| object | its kind's `to_text()` if it has one, otherwise `Enemy {name: "Goblin", health: 50}` |

`show(x)` prints a value the way a programmer reads it, with text in quotes.

A list, map or object that holds itself is written as `[...]`, `{...}` or `Node {...}` where it
comes back inside itself: after `set l to [1]` and `l.add(l)`, `say l` prints `[1, [...]]`. (An
object whose kind has `to_text()` is written by that function, as always.) Values
nested more than 16 deep are shortened to `...`. Text longer than 100 million bytes is a mistake:
"Turning this into text would make more than 100000000 bytes of text".

---

## 4. Expressions

### 4.1 Precedence

From the loosest to the tightest:

| Level | Operators | Groups |
|---|---|---|
| 1 | *value* `if` *test* `otherwise` *value* | right to left |
| 2 | `or` | left to right |
| 3 | `and` | left to right |
| 4 | `not` | — |
| 5 | comparisons: `is ...`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `contains` | do not chain |
| 6 | `+` `-` | left to right |
| 7 | `*` `/` `%` | left to right |
| 8 | unary `-` | — |
| 9 | `^` | right to left |
| 10 | `of` | — |
| 11 | `.` `'s` `[ ]` `( )` | left to right |

So `not 1 is 2` is `not (1 is 2)`, `-2 ^ 2` is `-(2 ^ 2)`, which is `-4`, and `2 ^ 3 ^ 2` is `2 ^ 9`, which is `512`. Brackets `( )` group as usual.

### 4.2 Arithmetic

| Expression | Numbers | Other values |
|---|---|---|
| `a + b` | sum | if either side is text, both are turned into text and joined: `"a" + 1` is `"a1"`; two lists join into a new list; two vectors add |
| `a - b` | difference | two vectors subtract |
| `a * b` | product | text times a number repeats it: `"ab" * 3` is `"ababab"`; a vector times a number scales it |
| `a / b` | quotient (`7 / 2` is `3.5`); dividing by zero is an error | a vector divided by a number |
| `a % b` | remainder, with the sign of `b`: `-7 % 3` is `2`, `7 % -3` is `-2`; `% 0` is an error | — |
| `a ^ b` | `a` to the power `b` | — |
| `-a` | negation | a vector reversed |

Any other combination is an error that names both types.

### 4.3 Equality

`a is b`, `a is equal to b` and `a == b` are the same test. `a is not b` and `a != b` are its opposite.

- Values of different types are never equal: `1 is "1"` is `false`. No conversion happens.
- Numbers are equal when they are the same double; text when it has the same characters.
- Lists are equal when they have equal items in the same order. Maps are equal when they have the same keys with equal values, in any order. Vectors are equal when their three numbers are.
- Objects, functions and kinds are equal only to themselves.

### 4.4 Comparisons

| Written | Same as |
|---|---|
| `a is above b`, `a is greater than b`, `a > b` | greater |
| `a is below b`, `a is less than b`, `a < b` | less |
| `a is at least b`, `a >= b` | greater or equal |
| `a is at most b`, `a <= b` | less or equal |
| `x is between a and b` | `x` is at least the smaller of `a` and `b` and at most the larger (both ends included, in either order) |
| `x is not between a and b` | its opposite |

Order comparisons work between two numbers, or between two texts (character by character, by Unicode code: `"B" < "a"`). Anything else is an error. Comparisons do not chain: `1 < x < 10` is an error that suggests `is between`.

### 4.5 Membership, emptiness and types

| Expression | True when |
|---|---|
| `list contains x`, `x is in list` | the list has an item equal to `x` |
| `map contains k`, `k is in map` | the map has the key `k` |
| `text contains t`, `t is in text` | `t` appears in the text |
| `x is not in y` | the opposite of `x is in y` |
| `x is empty` / `x is not empty` | text, list or map with no characters, items or keys (other values are never empty) |
| `x is even` / `x is odd` | `x` is a finite whole number and `x % 2` is `0` (even) or not (odd); a value that isn't a number is an error. Only when no local, parameter, enclosing local or script name `even` / `odd` exists, and the next token isn't `.`, `'s`, `(`, `[` or an arithmetic operator; otherwise it is the comparison `x is even` |
| `x is a whole number` | `x` is a finite number equal to its floor; false for other values |
| `x is a number` | `type(x)` is that type name: `number`, `text`, `boolean`, `list`, `map`, `function`, `vector`, `object`, `kind` |
| `x is a Enemy`, `x is an Enemy` | `x` is an object of that kind, or of a kind that extends it |

### 4.6 Logic

- `a and b`: if `a` counts as false, gives `a` without working out `b`; otherwise gives `b`.
- `a or b`: if `a` counts as true, gives `a` without working out `b`; otherwise gives `b`.
- `not a`: `true` if `a` counts as false, otherwise `false`.

So `nothing or "default"` is `"default"`, and `player and player.name` never reads the name of `nothing`.

**Picking a value:** *a* `if` *test* `otherwise` *b* works out *test*, then only *a* (when it counts as true) or only *b*. `else` may be written for `otherwise`. It chains to the right: `"A" if s > 90 otherwise "B" if s > 80 otherwise "C"`.

### 4.7 Properties, positions and calls

| Expression | Means |
|---|---|
| `a.b`, `a's b` | the property or field `b` of `a`; for a map, the value under the text key `"b"` |
| `my b` | `self.b` |
| `a[i]` | the item at position `i` of a list or text (from 1; negative from the end), or the value under key `i` of a map |
| `f(x, y)` | a call ([7](#7-functions)) |
| `f of x` | `f(x)`: `length of items` |
| `the x` | `x`, for any name `x` |
| `first of x`, `first item of x` | `x.first`; `last` likewise gives `x.last` |
| `every n in x <comparison>` | `x.all(function(n) return n <comparison> end)`: one comparison chain (`is ...`, `contains`, `==`, `!=`, `<`, `<=`, `>`, `>=`) about `n`, ending where a comparison ends (`and`, `or`, `then`, ...) |
| `any n in x <comparison>` | `x.any(...)` likewise |
| `every n in x is f`, `is not f` | with nothing after `f`, and no name `f` in reach (and `f` not `empty`, `even` or `odd`): `n.f` (or `not n.f`) |
| `item i of x` | `x[i]`. `i` is a number, a name (with `.`, `'s`, `[ ]` and calls after it) or an expression in brackets; anything else before the `of` is an error |
| `a.m(x)` | a method call on `a` |
| `parent.m(x)` | inside a kind, the method `m` of the kind it extends |

`every` and `any` take their forms only when followed by a name and `in`, and when nothing named `every` or `any` is in reach (by the same rule as below); a script phrase starting with the word wins. `every` with a time word at the end of the line is a timer ([10.2](#102-tasks)).

The four words `the`, `first`, `last` and `item` take the forms above only where nothing is named so: no local, parameter, enclosing local, name of the script or of a script it uses, or built-in function. Otherwise each is an ordinary name, and `first of x` is `first(x)`.

A position outside a list or text is an error that names the position and the length. Position `0` is an error that explains that positions start at 1.

### 4.8 Lists and maps

`[` *expression* { `,` *expression* } `]` makes a new list. `{` *key* `:` *expression* { `,` *key* `:` *expression* } `}` makes a new map, where *key* is a name (meaning that text), text, or a number.

### 4.9 Function values

`function (` [ *parameters* ] `)` *block* `end` is a function value that can be stored, passed and called. It sees the variables around it, and keeps seeing them after the code that made it has finished ([6.4](#64-closures)).

---

## 5. Statements

Each statement takes one line, except blocks, which run from their first line to `end`. In the forms below, `then` and `do` may always be left out.

### 5.1 Variables and constants

```
set name to expression
set target to expression           target: a.b, a's b, my b, a[i]
set name, name { , name } to expression
constant NAME is expression
choices NAME are name { , name } [ and name ]
```

- `set name to` gives a variable a value, making it if needed (see [6](#6-names-and-scope) for where).
- **Taking apart:** with two or more names, the value must be a list with exactly as many items as names, or a vector with 2 or 3 names (`x`, `y`, `z`). The value is worked out first, so `set a, b to [b, a]` swaps. A name may appear only once.
- `constant` works only at the top level of a script. A constant cannot be changed afterward.
- **Choices:** `choices NAME are a, b and c` is `constant NAME is {a: "a", b: "b", c: "c"}`, with the choices in order; each choice is its name as text. It works only at the top level of a script, starting a line, and the names must differ. A script phrase starting with `choices` takes the line instead. Where `NAME` isn't a local, `NAME.x` with `x` not a choice (and not `length`, `size`, `count` or a call) is a compile error, wherever the line is in the script. A `match` with no `otherwise` whose cases are all `NAME.choice` of one set gives a warning ([13](#13-mistakes-and-warnings)) for each choice it leaves out.

### 5.2 Changing values

```
add expression to target            number: adds; text: joins; list: adds an item at the end
take expression from target         number: subtracts; list: removes the first equal item
multiply target by expression
divide target by expression
sort target [ by name | by function-expression ]
shuffle target
reverse target
```

`sort`, `shuffle` and `reverse` are not reserved. A statement that starts with one of them followed by a name, `my` or `self` is `set target to target.sort()` (`.shuffle()`, `.reverse()`), so on a text `reverse` changes the variable. `sort target by name` is `target.sort_by(function(item) return item.name end)`: each item's field, or a map's key. `sort target by function(x) ... end` uses that function. A script whose phrases start with the word calls the phrase instead, and `sort(x)` stays a call.

### 5.3 Saying

`say` *expression* prints a value as text. `print(a, b, ...)` prints several, separated by spaces.

### 5.4 If

```
if expression [then]
    block
{ else if expression [then]
    block }
[ else
    block ]
end
```

`otherwise` may be written for `else`. A one-line form also works: `if hp is 0 then say "Game over" end`.

### 5.5 Loops

```
repeat expression times  block  end
repeat forever  block  end
for name from expression to expression [by expression] [do]  block  end
for each name [, name] in expression [do]  block  end
while expression [do]  block  end
until expression [do]  block  end
```

- `for ... from ... to ...` counts both ends included. Without `by`, it counts up by 1, or down by 1 when the end is smaller than the start.
- `for each` goes through a list (the second name, if given, is the position), a map (key and value), text (each character), or an object made from a kind (each field's name as text, and its value, in the order the fields were first set: a parent kind's first; methods aren't fields). Anything else is an error.
- `stop` leaves the innermost loop; `skip` goes on with its next round.

### 5.6 Match

```
match expression
is choice { , choice } [then]
    block
{ is choice { , choice } [then]
    block }
[ otherwise
    block ]
end
```

A *choice* is anything that may follow `is` in a comparison: a value (`is "sword"`), or `above 100`, `at least 90`, `between 1 and 10`, `in [1, 2, 3]`, `empty`, `not "idle"`, and so on.

- The value after `match` is worked out once, before the choices.
- The first choice that fits runs, and the rest are skipped. With several choices after one `is`, any one of them picks it.
- If no choice fits, the `otherwise` block runs, or nothing when there is none.
- `else` may be written for `otherwise`, which must come last.

`match` starts a match only at the start of a statement, with a value after it, when the next line starts with `is`. Anywhere else `match` is an ordinary name.

### 5.7 Functions, phrases and kinds

```
function name ( [parameters] )  block  end
phrase pattern  block  end
kind Name [extends Name]  { set field to expression | function ... end }  end
return [expression]
```

See [7](#7-functions), [8](#8-phrases) and [9](#9-kinds-and-objects).

### 5.8 Events and tasks

```
when event-name [ (with | by) name { (, | and) name } ] [do]  block  end
when "Entity Name" is event-name ...  block  end
trigger event-name [ with expression { , expression } ]
wait [ expression [unit] ]
wait until expression
wait for event-name [ (with | by) name { (, | and) name } ]
wait for "Entity Name" is event-name [ (with | by) name { (, | and) name } ]
every expression unit [do]  block  end
after expression unit [do]  block  end
start call
```

See [10](#10-events-and-tasks).

### 5.9 Errors

```
try
    block
[ catch [name]
    block ]
[ finally
    block ]
end
fail expression
```

See [11](#11-errors).

### 5.9.1 Tests

```
test text [do]  block  end
expect expression
```

- `test` *text* at the start of a line, with nothing else after the text but `do`, makes a **test**: a function the module keeps, never run when the script runs. A host runs it with `VM::RunTest`, which `forgescript --test` uses: the script runs first, then each test in order, each as a task of its own that may wait (game time steps by 1/60 second, up to 600 seconds a test). A test passes if it ends without an error. Tests can only be written at the top level of a script.
- `expect` *expression* works anywhere. When the expression is false or `nothing`, it is an error: "Expected *expression*, but *left* is *value*." when the expression is one comparison (no `and`, `or` or `not` at its top), with *left* the text and value of the comparison's left side, and otherwise "Expected *expression*, but it is *value*." The texts are the script's own.
- `test` and `expect` are contextual words. `expect` takes this form only when followed by something that can start a value but can't follow a name (a number, text, a name, `true`, `false`, `yes`, `no`, `nothing`, `not`, `my`, `self`, `new`), so `expect(x)`, `expect - 1` and `expect[1]` keep their meanings. A script phrase starting with either word takes the line instead.

### 5.10 Using other scripts

`use "path"` [ `as` *name* ]. See [12](#12-using-other-scripts).

### 5.11 Expression statements

A call (`f(x)`, `object.method()`) or a phrase (`heal hero by 5`) may stand alone as a statement. Any other expression standing alone is a mistake.

---

## 6. Names and scope

### 6.1 Script variables

At the top level of a script (outside functions and kinds), `set` makes a **script variable**. Every function in the script sees it, wherever the function is written.

### 6.2 Local variables

Inside a function, `set name to` makes a **local variable** of that function, unless the script or an enclosing function already has a variable of that name, in which case it changes that one. Parameters are local variables too.

### 6.3 Block names

The names a `for` or `for each` loop gives, and the name after `catch`, belong to their block and are gone after its `end`.

### 6.4 Closures

A function written inside another function sees the outer function's variables, shares them, and keeps them alive after the outer function returns. Two calls of the same inner function value see each other's changes.

### 6.5 Order

Top-level `function`, `phrase` and `kind` definitions can be used anywhere in the script, including above the line that defines them. A variable must have been given a value before it is read; reading one that has not is an error that suggests a similar name.

### 6.6 Built-in names

The built-in functions (`print`, `max`, `random`, `time`, ...) and the host's functions are names like any other: a script may give a value to `max`, and then `max(1, 2)` calls that value instead. The checker warns when this happens ([13](#13-mistakes-and-warnings)).

---

## 7. Functions

```
function name ( [ parameter { , parameter } ] [ , name ... ] ) [ as type ]  block  end
parameter:  name [ as type ] [ is expression ]
type:       number | text | boolean | list | map | function | vector | object | kind | name
```

- A call must give each parameter without a default exactly once. Too few or too many values is an error that says how many the function needs.
- **Defaults** (`damage is 10`) come after the parameters without one. A default is worked out at each call that leaves its value out, and may use the parameters before it.
- **Named values:** `attack(goblin, crit: 5)` gives values by name, after the values given in order. Built-in functions take values in order only.
- **Types:** `as` *type* after a parameter's name is for checkers only and never changes what runs. A checker warns ([13](#13-mistakes-and-warnings)) for each value of a call to one of the script's own top-level functions, by name, that is written out plainly (a number, `-` and a number, text without `{ }`, `true`/`false`/`yes`/`no`, `nothing`, a list or a map) and isn't of the parameter's type; values given by name are matched to their parameters. In 1.7.0 a value that is one of the calling function's own variables is checked too, against a parameter of type `number`, `text`, `boolean`, `list`, `map` or `vector`, when the variable's type is certain: every `set` of it in the function (inner functions included) gives a plain value of one type other than `nothing`, and nothing else gives it a value (`for`, `with`, `catch`, `set a, b to`); or it is a parameter with one of those types that the function never sets. A lowercase type that is one edit from a type word is a mistake. A `name...` parameter can't have a type. `function` and `kind` after `as` open no block.
- **What it gives back** (1.7.0): `as` *type* right after the `)`, on the same line, is for checkers only, like a parameter's type, for a script's functions and kinds' functions. A checker warns for a `return` whose value is written out plainly (as above) and isn't of that type; `return nothing` and a bare `return` are never warned about. It also warns, at the type, when the function has no `return` with a value and no `fail`. A lone `as` after the `)` with no type after it is read as before.
- **The rest:** a last parameter written `name...` gets the values given in order after the other parameters, as a new list (`[]` when there are none). The parameters before it work as above; a call needs at least as many values as they have without defaults, and too few is an error that says "at least". The `...` parameter can't have a default or be named.
- `return` *expression* gives back a value and leaves the function. A function that ends without `return`, or with a bare `return`, gives back `nothing`.
- Functions are values: they can be stored in variables, lists and maps, and passed to other functions.

---

## 8. Phrases

```
phrase pattern  block  end
pattern:  word { word | ( name ) }
```

A phrase is a function called by its words: `phrase heal (target) by (amount)` is called as `heal hero by 5`.

- A pattern starts with a word, and two slots may not follow each other.
- Each slot holds any expression; the value becomes the parameter of that name.
- Phrases can return values, so they work inside expressions: `say average of 4 and 8`.
- A phrase can be used anywhere in the script, even above its definition, and in scripts that `use` it.
- A local variable of the same name as a phrase's first word wins over the phrase, except at the start of a statement when the next word can't go on from a value: with a local `move`, `move box to p` is the phrase, while `move(1)`, `move.x`, `say move` and `repeat move times` use the variable.
- The host can register its own phrases. A host phrase may be marked to **give way to calls**: its first word followed by `(`, `[`, `.`, `'s`, an operator, `of`, `is`, `contains`, `and` or `or` keeps the meaning it had without the phrase. A space doesn't change that: `show (x)` is still a call, and the checker warns about it ([13](#13-mistakes-and-warnings)).

The built-in phrases are `say (value)`, `random number from (a) to (b)`, `random item from (list)` and `distance from (a) to (b)`.

---

## 9. Kinds and objects

```
kind Name [extends Name]
    { set field to expression }
    { shared name is expression }
    { when my field changes [ with name [ , name ] ]  block  end }
    { property name [ as type ]  block  end }
    { keep my field between term and term }
    { when my field is set to name  block  end }
    { function name ( [parameters] )  block  end }
end
```

- `set` lines in a kind make **fields**; every new object gets its own copy, worked out when the object is made. `function` blocks make **methods**.
- `new Name(values)` makes an object and passes the values to its `setup` method, if it has one. Without `setup`, write `new Name` or `new Name()`.
- Inside a method, `self` is the object and `my field` is `self.field`. A plain `set name to` inside a method makes a local variable, not a field.
- `extends` makes a kind start from another's fields and methods. `parent.method(...)` calls the version of the kind it extends.
- A `to_text()` method decides how the object prints.
- **Change handlers:** `when my` *field* `changes` inside a kind is a method kept by the kind. After a write to that field of an object made from the kind (or one that extends it) that leaves a value not equal (by `is`) to the one before, each handler for the field runs as its own task, right away, like `trigger`'s: the kind it extends first, then in the order written. Its `with` names (at most two) get the new value and the old one. A write is not a change when the field had no value before, when it is made by the kind's field lines (when `new` makes the object), or when it happens in a task that is running a handler for the same object and field. A change made from handlers nested 64 deep is an error ("'b' was changed from inside 64 other changes and events ..."). Writes to kinds without handlers, and to fields without them, run as before. At the top level of a script, `when my field changes` is still an event handler for the event named `my field changes`.
- **Shared values:** `shared name is expression` (or `to`) makes one value for the kind and all its objects, worked out once when the `kind` statement runs, in the scope around the kind. `Name.name2` reads it and `set Name.name2 to ...` (also `add`, `take` and the rest) changes it, inside the kind's methods too; a bare `name2` in a method is not the shared value. A kind that extends another reads and changes the parent's shared values through its own name (`Boss.count` is `Enemy.count`). Reading one through an object (`goblin.count`) is an error that says to write `Enemy.count`. A shared value can't be called `name` (a kind's `name` is its name), and a name can be shared once per kind. `shared` is a contextual word: only at the start of a line inside a kind, followed by a name and `is` or `to`.
- **Properties, keeps and set handlers** (1.8.0) are methods kept by the kind, found in the kind or the nearest kind it extends (a kind's own replaces its parent's). Each runs as a nested task that must finish right away: a `wait` or `wait for` in one is an error, and one run from inside 64 others is an error.
  - `property` *name* `[as` *type*`]` ... `end`: reading *name* on an object that has no field of that name runs the block, with `self` as the object, and gives its `return` value (`nothing` without one). Writing it is an error ("'*name*' is worked out by its property, so it can't be set. ..."). `parent.`*name* inside a kind that extends another runs the parent's property. A name can't be both a field (`set` *name* line) and a property of one kind, and has at most one property per kind.
  - `keep my` *field* `between` *low* `and` *high*: each write to the field of an object (by `set`, `add`, `take`, `multiply`, `divide`, `obj["field"]`, from any code) first works out *low* and *high* (arithmetic terms, as in `is between`, with `self` as the object), then stores the value clamped into the range, the ends in either order. Ends that aren't numbers, or a value that isn't a number, are errors. The kind's field lines store first values unclamped. One `keep` per field per kind.
  - `when my` *field* `is set to` *name* ... `end`: each write to the field, except the kind's field lines, runs the block instead of storing the value, with *name* holding the value written. Inside the task running it, a write to the same field of the same object stores the value (and runs `when ... changes` handlers as usual). The write's expression still gives the value written. A field can't have both `keep` and a set handler in one kind, nor either on a property's name.
  - `property`, `keep` and the set handler's words are contextual: only at the start of a line inside a kind, in these shapes. Everywhere else they mean what they meant in 1.7.0.
- Reading or calling a field or method an object doesn't have is an error that names the kind.

---

## 10. Events and tasks

### 10.1 Events

- `when` *event-name* runs its block every time that event happens. The event name is every word after `when` up to `with` or `by` (or the line's end). The names after `with` or `by`, separated by `,` or `and`, receive the event's values in order; missing values are `nothing`, extra ones are ignored.
- `when "Start" is clicked` runs only when the event came from the entity named `Start` (any letter case).
- `trigger` *event-name* [ `with` *values* ] runs every matching `when` block at once, before the line after `trigger`.
- The host sends its own events (MageCap Bellows: `created`, `update`, `physics update`, `clicked`, `script error` and the rest, listed in the [Scripting API](../ScriptingAPI.md#forgescript-entity-scripts)).

### 10.2 Tasks

- Every `when` block runs as its own **task**, and so does every `start` *call*. A task runs until it finishes, fails, or waits.
- `wait` pauses only the current task:

  | Form | Waits for |
  |---|---|
  | `wait` | one frame |
  | `wait 2`, `wait 2 seconds`, `wait 1 second` | seconds |
  | `wait 250 ms`, `wait 250 milliseconds` | thousandths of a second |
  | `wait 1 minute`, `wait 2 minutes` | minutes |
  | `wait 3 frames`, `wait 1 frame` | frames |
  | `wait until` *expression* | the first frame on which the expression counts as true, checked every frame |

- The host moves time forward, once per frame.
- A host whose game is paused calls `VM::UpdatePaused` instead of `Update`. Time and the frame count stand still, so `time()` stops and every wait holds, except the waits of tasks in modules the host names (MageCap Bellows: scripts marked Run While Paused). Those count down by the real frame time. When the host calls `Update` again, the held waits carry on with the time they had left.
- `wait for` *event-name* pauses the task until its module next gets the event, delivered as a `when` block would get it (to that module only, or to every module, as the host sends it); `wait for "Start" is clicked` needs the event's source to be that entity, ignoring letter case. The module's `when` blocks for the event run first, then its waiting tasks, in the order they began waiting. The waiting tasks are chosen before any block runs, so a wait that begins during the event waits for its next one. The names after `with` or `by` receive the event's values like a `when` block's names (missing ones are `nothing`); at the top of a script they are script variables, elsewhere variables of the function. An event wait is never ended by time, and ends when its event comes even while the host's game is paused. `VM::HasHandler` reports a module that waits for an event as one that has a handler for it.
- `every` *expression unit* ... `end` and `after` *expression unit* ... `end` are **timers**. The unit is one of `second`, `seconds`, `ms`, `millisecond`, `milliseconds`, `minute`, `minutes`, `frame`, `frames`, and is required. Each timer runs as a task of its module, made from its block as from an anonymous function (it sees the variables around it), started where it is written; the line after the timer runs right away. `after` waits once (as `wait` *expression unit* would) and runs its block. `every` repeats forever: it works out the expression, waits, and runs its block; `stop` and `skip` inside the block (outside any inner loop) end or restart it. Timers are cancelled with their module. `every` and `after` are not reserved: a line is a timer when it starts a statement with one of them, a value follows, and the line ends in a unit. A script whose phrases (its own, the host's, or a used script's) start with that word calls the phrase instead.
- Code that a host or a built-in function calls directly and must finish right away (a function passed to `sort_by`, an object's `to_text`) cannot `wait` or `wait for`. It can start a task or a timer, which runs on its own.

---

## 11. Errors

- A **runtime error** stops the current task. Without a `try`, it is reported with its message, the script's name, the line, and the calls that led there.
- `fail` *expression* raises an error with any value.
- `try` ... `catch` *name* ... `end`: an error inside the `try` block runs the `catch` block, with *name* holding the error: the message as text for errors ForgeScript raises, or the value given to `fail`.
- A `try` with neither `catch` nor `finally` ignores an error inside it, and the script carries on after its `end`.
- `finally` ... runs after the `try` (and `catch`) however they end: normally, with an error, or by `return`, `stop` or `skip`. Without a `catch`, the error goes on after the `finally` block, still reported at its own line.
- An error inside a `catch` or `finally` block goes on to the next enclosing `try`.

---

## 12. Using other scripts

`use "helpers"` loads another script once and makes its top-level functions, phrases, kinds and variables available. With `as h`, they are reached as `h.name` instead.

- `.forge` is added when the path has no extension.
- The path must be relative, must not contain a `..` step, and must end in `.forge` or have no extension. Anything else is a mistake, in every compile: "'use' only reaches scripts inside this script's place ...", or "'use' only loads ForgeScript scripts ...". A link that leads outside the folder is not followed (the script counts as not found).
- In a MageCap Bellows project (a folder with a `.bproject` file, found by walking up from the script), paths start at the project's asset folder: `use "Scripts/Helpers"` means `Assets/Scripts/Helpers.forge`. Anywhere else they start next to the script.
- A script that `use`s a script that cannot be found, or that has a mistake, stops at the `use` line.

---

## 13. Mistakes and warnings

A **mistake** (compile error) is text that is not ForgeScript. A script with a mistake does not run. Checkers (`forgescript --check`, the language server, the Bellows editor) list every mistake they can find, in order of line.

A **warning** points at code that runs but probably doesn't do what was meant. A warning never stops a script or changes what it does, and checkers show warnings only once a script has no mistakes. In 1.5.0:

- A value named like a built-in function that the script also calls: `set max to 10` in a script that calls `max(a, b)`.
- `show (menu)`, with a space before the bracket, for a host phrase that gives way to calls ([8](#8-phrases)): it is a call, not the phrase.
- In 1.6.0: a timer whose time is written as a negative number (`every -1 seconds`), which works like 0.
- In 1.6.0: a call to one of the script's functions with a plain value of another type than its parameter's `as` type: "'heal' takes a number for 'amount' (amount as number), but this is text."
- In 1.6.0: a `match` with no `otherwise` whose cases are all choices of one set ([5.1](#51-variables-and-constants)) and leave one out: "This match has no case for State.jumping, and no otherwise."
- In 1.7.0: a `repeat forever`, `while true` or `until false` loop that has no `wait`, `stop`, `return` or `fail` inside and calls nothing that might wait: "This 'repeat forever' loop never waits, so nothing else in the game can run while it does."
- In 1.7.0: a script's own function, kind or choices that takes the name of a built-in or engine function: "Your function 'tween' takes the name of the built-in 'tween', so 'tween' in this script now means yours." The script's own name still wins, so the script works as before; the warning is there because a new engine release can add a name a game already uses.
- In 1.7.0: text in quotes with a byte that isn't UTF-8: "This text has a byte that isn't readable text, so it will show up garbled in the game. Save the script as UTF-8 (in most editors: Save As, then Encoding: UTF-8)." It points at the first such byte, once per piece of text. The script runs as before.
- In 1.7.0: a name read as a value that nothing sets: not this script (anywhere in it), not a script it uses, not the language and not the engine. "'helth' isn't set anywhere, so this line will stop with a mistake when it runs. Did you mean 'health'?" A name is suggested only when it is the one close name. Calls (`helth()`) are left alone.
- In 1.7.0: a `return` with a plain value of another type than the function's return type: "'status' says it gives back text (as text), but this gives back a number." And a function with a return type but no `return` with a value: "'status' says it gives back text (as text), but it never returns a value, so it always gives back nothing. Add 'return' with the text it should give back." (No quick fix: `return nothing` would hide the mistake.)
- In 1.7.0: a call that gives one of the function's own variables whose type is certain (see [7](#7-functions)) and isn't the parameter's type: "'heal' takes a number for 'amount' (amount as number), but 'name' is text (it is only ever set to text)." For a typed parameter the reason reads "(who as text)".

`forgescript --check --strict` (1.7.0) counts warnings too: the output is the same, and the exit code is 1 when there is a warning (65 when there is a mistake). The language server's `strict` initialization option sends warnings as errors. Neither changes what runs.

A mistake or warning whose message says exactly what to write instead also carries that change, so an editor can offer it as a one-click fix (1.7.0; see the language server's quick fixes).

---

## 14. Limits

| Limit | Value | When it's reached |
|---|---|---|
| Nesting of brackets, lists, maps, calls, blocks, and chains of `-`, `not` and `^` | 256 levels (or fewer, if reading it has used 512 KB of the program's stack) | a mistake: "This is nested too deeply" |
| Calls inside calls | 512 | an error: "Too many functions calling each other" |
| Built-in functions calling back into scripts, and `trigger` inside `when` | 64 levels | an error on the deepest line |
| Variables in one function | 255 | a mistake |
| Variables from outside that one function uses | 255 | a mistake |
| Values in one call, parameters of one function, values of one event | 255 | a mistake |
| Names taken apart at once | 255 | a mistake |
| Instructions without a `wait` | set by the host (MageCap Bellows: 50,000,000 per frame; the `forgescript` tool: none) | an error that stops the task |
