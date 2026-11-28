#include "ForgeScript/Tools/Help.h"

#include <algorithm>

namespace ForgeScript {

	namespace {

		using Kind = HelpEntry::Kind;

		HelpEntry Keyword(const char* name, const char* signature, const char* doc)
		{
			return { Kind::Keyword, "", name, signature, doc, false };
		}

		HelpEntry Function(const char* name, const char* signature, const char* doc)
		{
			return { Kind::Function, "", name, signature, doc, false };
		}

		HelpEntry Phrase(const char* name, const char* doc)
		{
			return { Kind::Phrase, "", name, name, doc, false };
		}

		HelpEntry Method(const char* owner, const char* name, const char* signature, const char* doc)
		{
			return { Kind::Method, owner, name, signature, doc, false };
		}

		HelpEntry Property(const char* owner, const char* name, const char* signature, const char* doc)
		{
			return { Kind::Method, owner, name, signature, doc, true };
		}

		std::vector<HelpEntry> BuildHelp()
		{
			return {
				// Reserved words
				Keyword("add", "add value to target", "Adds a number to a variable (add 5 to score) or an item to the end of a list (add \"key\" to items)."),
				Keyword("and", "a and b", "True when both sides are true. Also separates names after with or by."),
				Keyword("by", "multiply x by n, when hit by damage", "Part of multiply and divide, of for counting steps, and names a value an event brings."),
				Keyword("catch", "try ... catch problem ... end", "Runs when something inside try fails; the name after catch holds the error message."),
				Keyword("constant", "constant NAME is value", "A script value that never changes. Only at the top of a script."),
				Keyword("contains", "list contains x", "True when a list, map or text holds the value."),
				Keyword("divide", "divide target by n", "Divides a variable by a number and keeps the result in it."),
				Keyword("do", "while x do ... end", "Optional word that starts the body of a loop."),
				Keyword("each", "for each item in list ... end", "Goes through every item of a list (or every key and value of a map, or every field and value of an object)."),
				Keyword("else", "if ... else ... end", "The part of an if that runs when the condition is false. otherwise means the same."),
				Keyword("end", "... end", "Closes a block: if, loops, function, phrase, kind, when, try."),
				Keyword("extends", "kind Boss extends Enemy", "Makes a kind that starts with everything another kind has."),
				Keyword("fail", "fail \"message\"", "Stops with an error that try ... catch can handle."),
				Keyword("false", "false", "The value for no. Only false and nothing count as false."),
				Keyword("for", "for i from 1 to 10 ... end", "Counts through numbers, or goes through a list with for each."),
				Keyword("from", "for i from 1 to 10, take 5 from health", "Part of counting loops and of take ... from."),
				Keyword("function", "function name(a, b is 10) ... end", "Defines a function; 'is' gives a value a default for calls that leave it out, and calls can name values: attack(goblin, crit: 5). A last value written numbers... collects the rest as a list: function total(numbers...). 'as' gives a value a type for the checker: function heal(amount as number). Without a name it makes a function value: function(x) return x * 2 end."),
				Keyword("if", "if condition then ... else ... end", "Runs a block only when the condition is true. After a value it picks one of two: \"big\" if x > 3 otherwise \"small\"."),
				Keyword("in", "for each x in list, x is in list", "Names the list a loop goes through, or tests membership."),
				Keyword("is", "a is b, x is above 3, x is between 1 and 10, x is a number, x is even", "Compares values. Lists and maps are equal when they hold the same items. 'between' includes both ends, in either order. 'is even', 'is odd' and 'is a whole number' test numbers, unless the script has its own value named even or odd."),
				Keyword("kind", "kind Enemy ... end", "Defines a kind of object, with fields and functions; make one with new Enemy()."),
				Keyword("multiply", "multiply target by n", "Multiplies a variable by a number and keeps the result in it."),
				Keyword("my", "my health", "A field of the object or entity this script belongs to; the same as self.health."),
				Keyword("new", "new Enemy(\"Goblin\")", "Makes an object of a kind and runs its setup function."),
				Keyword("no", "no", "The same as false."),
				Keyword("not", "not x, x is not y", "Turns true into false and false into true."),
				Keyword("nothing", "nothing", "No value. Missing values and empty results are nothing."),
				Keyword("of", "length of list, first of list, item 2 of list", "Calls a function with one value: length of list is length(list). first of list and last of list are list.first and list.last, and item 2 of list is list[2], unless the script has its own first, last or item."),
				Keyword("or", "a or b", "True when either side is true."),
				Keyword("otherwise", "if ... otherwise ... end", "The same as else. Also the second choice in value if test otherwise other."),
				Keyword("parent", "parent.setup(name)", "Inside a kind that extends another, calls the other kind's version of a function."),
				Keyword("phrase", "phrase heal (target) by (amount) ... end", "Defines words that call code: heal hero by 5."),
				Keyword("repeat", "repeat 3 times ... end, repeat forever ... end", "Runs a block a number of times, or until stop."),
				Keyword("return", "return value", "Ends a function and gives back a value."),
				Keyword("self", "self.position", "The object or entity this code belongs to."),
				Keyword("set", "set name to value, set x, y to list, set score as number to 0", "Makes or changes a variable, property or list item. With several names it takes a list (or a vector) apart, one item per name. 'as' says what a variable holds, for the checker only: set names as list of text to []. A kind's field gets its type on its line at the top of the kind."),
				Keyword("skip", "skip", "Jumps to the next round of the loop."),
				Keyword("start", "start patrol()", "Runs a function as its own task, so it can wait while the rest of the game goes on. 'set p to start patrol()' keeps the task, to ask p.running, p.done or p.result, or to p.cancel() it."),
				Keyword("stop", "stop", "Leaves the loop right away."),
				Keyword("take", "take value from target", "Subtracts a number from a variable, or removes an item from a list."),
				Keyword("then", "if condition then ... end", "Optional word after the condition of an if."),
				Keyword("to", "set x to 5, add 1 to score", "Part of set, add and counting loops."),
				Keyword("trigger", "trigger event with values", "Sends an event: every matching when block runs right away."),
				Keyword("true", "true", "The value for yes."),
				Keyword("match", "match value / is \"a\", \"b\" then ... / is above 10 then ... / otherwise ... / end", "Runs the first choice whose 'is' fits the value, which is worked out once. Each 'is' works like an if's; commas mean any one of them. Not a reserved word: it only starts a match when the next line starts with is."),
				Keyword("every", "every 3 seconds ... end, every x in list is above 0", "A timer: runs the block again and again, waiting the time before each run (counted from when the last run finished). stop inside it ends it. Not a reserved word: it only starts a timer on a line that ends in a time word. In a value, every x in list is ... is true when it holds for each item (list.all), and any x in list is ... when it holds for one (list.any)."),
				Keyword("test", "test \"name\" ... end", "A test of the script, with expect lines. Tests run only with forgescript --test, never when the script runs. Write them at the top level. Not a reserved word."),
				Keyword("expect", "expect total(2, 3) is 5", "An error naming what was expected and what was found, unless the condition is true. In a test, the first failed expect fails the test. Not a reserved word."),
				Keyword("property", "property health_percent ... end", "Inside a kind: a value read like a field (hero.health_percent) and worked out by its block each time it's read. It can't be set, and it can't wait. Not a reserved word."),
				Keyword("keep", "keep my health between 0 and my max_health", "Inside a kind: every change to the field is kept between the two ends, which are worked out at each change. Not a reserved word."),
				Keyword("contract", "contract Damageable ... needs take_hit(amount) ... end", "What a kind promises to have, one 'needs' line each: 'needs name' for a field or property, 'needs name(values)' for a function. Kinds write 'kind Crate follows Damageable', and 'x is a Damageable' checks an object. At the top level of a script. Not a reserved word."),
				Keyword("follows", "kind Crate follows Damageable, Named", "On a kind line, after 'extends' if there is one: the contracts the kind promises to meet. A missing need is a mistake at the kind line. Not a reserved word."),
				Keyword("needs", "needs take_hit(amount)", "Inside a contract: something a kind that follows it must have. 'needs health' is a field or property; 'needs take_hit(amount)' is a function that takes that many values. Not a reserved word."),
				Keyword("shared", "shared count is 0", "Inside a kind: one value for the kind and every object made from it, read and changed as Enemy.count (also inside the kind's functions). Worked out once, when the kind is made. 'shared count as number is 0' gives it a type for the checker. Not a reserved word."),
				Keyword("choices", "choices State are idle, running, jumping", "Makes a set of choices (an enum): State.idle is the text \"idle\". A misspelled choice is a mistake the checker names, and a match over the choices warns about one it leaves out. Write it at the top level of a script. Not a reserved word."),
				Keyword("after", "after 2 seconds ... end", "A timer: runs the block once, after the time. The script goes on right away. Not a reserved word: it only starts a timer on a line that ends in a time word."),
				Keyword("try", "try ... catch problem ... finally ... end", "Runs a block and catches any error inside it. Code after finally always runs, however the try ends; without catch the error then goes on."),
				Keyword("until", "until done ... end, wait until ready", "Repeats until the condition is true, or waits until it is."),
				Keyword("use", "use \"helpers\", use \"helpers\" as h", "Brings in another script's functions, phrases and kinds."),
				Keyword("wait", "wait 2 seconds, wait 3 frames, wait until ready, wait for hit with damage", "Pauses only this task while the game keeps running. 'wait for' pauses it until this script gets the event, with its values in the names."),
				Keyword("when", "when event with values ... end", "Runs every time the event happens. Inside a kind, when my health changes with now, before ... end runs right after an object's field changes. when my is_open is set to wanted ... end runs instead of each change, and sets the field itself."),
				Keyword("while", "while condition ... end", "Repeats a block while the condition is true."),
				Keyword("with", "trigger hit with 25, when hit with damage", "Introduces the values that go with an event."),
				Keyword("yes", "yes", "The same as true."),

				// Built-in functions and values
				Function("print", "print(values...)", "Shows values on one line, separated by spaces."),
				Function("show", "show(value)", "Shows a value the way a programmer sees it, with quotes around text."),
				Function("text", "text(value)", "The value as text: text(42) is \"42\"."),
				Function("number", "number(value), number(text, base)", "The value as a number, or nothing when it is not one: number(\"3.5\") is 3.5. With a base from 2 to 36 it reads text written in that base: number(\"ff\", 16) is 255, number(\"101\", 2) is 5."),
				Function("type", "type(value)", "The name of the value's type: \"number\", \"text\", \"list\", or a kind's name."),
				Function("length", "length(value)", "How many items a list or map has, or characters a text has."),
				Function("copy", "copy(value)", "A separate copy of a list, map or vector, so changing one leaves the other alone."),
				Function("assert", "assert(condition, message)", "Fails with the message when the condition is false."),
				Function("abs", "abs(x)", "The number without its minus sign."),
				Function("floor", "floor(x)", "The whole number at or below x."),
				Function("ceil", "ceil(x)", "The whole number at or above x."),
				Function("round", "round(x, digits)", "x rounded to the nearest whole number, or to that many digits after the point."),
				Function("truncate", "truncate(x)", "x without its fraction, toward zero: truncate(-2.5) is -2 (floor(-2.5) is -3)."),
				Function("quotient", "quotient(a, b)", "Whole-number division that agrees with %: quotient(7, 2) is 3, quotient(-7, 2) is -4, so quotient(a, b) * b + a % b is a."),
				Function("to_base", "to_base(n, base)", "A whole number written in a base from 2 to 36, as text: to_base(255, 16) is \"ff\", to_base(5, 2) is \"101\". number(text, base) reads it back."),
				Function("bit_and", "bit_and(a, b)", "The bits that are on in both a and b: bit_and(12, 10) is 8. Bits work on whole numbers from 0 to 9,007,199,254,740,991."),
				Function("bit_or", "bit_or(a, b)", "The bits that are on in a, b or both: bit_or(5, 2) is 7."),
				Function("bit_xor", "bit_xor(a, b)", "The bits that are on in a or b but not both: bit_xor(12, 10) is 6."),
				Function("shift_left", "shift_left(x, n)", "x with its bits moved n places up, the same as x times 2^n: shift_left(1, 3) is 8."),
				Function("shift_right", "shift_right(x, n)", "x with its bits moved n places down, dropping the lowest: shift_right(256, 4) is 16."),
				Function("has_bit", "has_bit(x, n)", "Whether bit n of x is on, for flags. Bits count from 0: bit 0 is worth 1, bit 3 is worth 8. if has_bit(flags, 3) then"),
				Function("with_bit", "with_bit(x, n)", "x with bit n on (bits count from 0): set flags to with_bit(flags, 3)."),
				Function("without_bit", "without_bit(x, n)", "x with bit n off (bits count from 0): set flags to without_bit(flags, 3)."),
				Function("flip_bit", "flip_bit(x, n)", "x with bit n switched, on to off or off to on (bits count from 0)."),
				Function("count_bits", "count_bits(x)", "How many bits of x are on, like how many flags are set: count_bits(7) is 3."),
				Function("format_number", "format_number(x, decimals)", "x as text with its thousands grouped and that many decimals, for scores and money: format_number(1250.5, 2) is \"1,250.50\"."),
				Function("move_toward", "move_toward(from, to, step)", "from moved toward to by at most step, never past it (numbers or vectors): call it every frame for steady movement that stops on the target."),
				Function("inverse_lerp", "inverse_lerp(a, b, value)", "Where value sits between a and b: 0 at a, 1 at b. lerp backwards."),
				Function("remap", "remap(value, from_low, from_high, to_low, to_high)", "value moved from one range to another: remap(5, 0, 10, 0, 100) is 50."),
				Function("smoothstep", "smoothstep(a, b, x)", "0 before a, 1 after b, and a smooth S-curve between, for easing in and out."),
				Function("wrap", "wrap(value, low, high)", "value kept between low and high by going around, like an angle: wrap(370, 0, 360) is 10. high itself wraps to low."),
				Function("snapped", "snapped(value, step)", "The nearest multiple of step, for grids: snapped(17, 5) is 15."),
				Function("random_seed", "random_seed(number)", "Makes random numbers repeat the same way for the same seed, for levels made from a seed."),
				Function("range", "range(from, to, step)", "A list of the numbers from from to to, both included, like for i from 1 to 5: range(1, 5) is [1, 2, 3, 4, 5]. Counts down when to is smaller."),
				Function("zip", "zip(first, second)", "The items of two lists paired by position: zip([\"a\", \"b\"], [1, 2]) is [[\"a\", 1], [\"b\", 2]]."),
				Function("to_json", "to_json(value)", "The value as JSON text (nothing, true/false, numbers, text, lists and maps with text keys)."),
				Function("from_json", "from_json(text)", "The value JSON text describes, made of nothing, true/false, numbers, text, lists and maps."),
				Function("format_time", "format_time(seconds)", "Seconds as a clock, for timers: format_time(65) is \"1:05\", format_time(3723) is \"1:02:03\"."),
				Function("sqrt", "sqrt(x)", "The square root of x."),
				Function("sin", "sin(radians)", "The sine of an angle in radians (use radians(degrees) to convert)."),
				Function("cos", "cos(radians)", "The cosine of an angle in radians (use radians(degrees) to convert)."),
				Function("tan", "tan(radians)", "The tangent of an angle in radians (use radians(degrees) to convert)."),
				Function("asin", "asin(x)", "The angle in radians whose sine is x."),
				Function("acos", "acos(x)", "The angle in radians whose cosine is x."),
				Function("atan", "atan(x)", "The angle in radians whose tangent is x."),
				Function("atan2", "atan2(y, x)", "The angle in radians of the point (x, y), from -pi to pi."),
				Function("exp", "exp(x)", "e raised to the power x."),
				Function("log", "log(x, base)", "The logarithm of x: natural without a base, or in the base given."),
				Function("sign", "sign(x)", "-1, 0 or 1, depending on whether x is below, at or above zero."),
				Function("min", "min(values...)", "The smallest of the numbers, or of the numbers in a list."),
				Function("max", "max(values...)", "The largest of the numbers, or of the numbers in a list."),
				Function("clamp", "clamp(x, low, high)", "x kept between low and high."),
				Function("lerp", "lerp(a, b, t)", "The value t of the way from a to b: lerp(0, 10, 0.5) is 5."),
				Function("radians", "radians(degrees)", "Converts degrees to radians."),
				Function("degrees", "degrees(radians)", "Converts radians to degrees."),
				Function("pi", "pi", "The number pi, about 3.14159."),
				Function("infinity", "infinity", "A number larger than every other number."),
				Function("random", "random(), random(n), random(a, b)", "A random number: from 0 to 1, a whole number from 1 to n, or from a to b."),
				Function("real_time", "real_time()", "Seconds since the start of 1970 (UTC) by the computer's clock, with fractions. Unlike time(), it keeps counting while the game is paused or closed: for daily rewards, streaks and save stamps."),
				Function("date", "date() or date(moment)", "The local date and time, now or at a moment from real_time(), as a map: year, month (1 to 12), day, hour, minute, second, weekday (\"Monday\"), weekday_number (1 = Monday to 7), month_name and day_of_year."),
				Function("utc_date", "utc_date() or utc_date(moment)", "Like date(), in UTC instead of the computer's time zone, so it gives the same answer everywhere."),
				Function("format_date", "format_date(moment, pattern)", "A moment from real_time() as text in local time: \"2026-11-22 02:00\" by default, or by a pattern of YYYY, MM, DD, hh, mm, ss, Month, Mon, Weekday and Wkd, like format_date(saved_at, \"Weekday, Month DD\")."),
				Function("days_between", "days_between(from, to)", "Whole calendar days from one moment to another in local time: 1 from 11 PM to 1 AM the next day, so a daily reward knows a new day began. Negative when to is earlier."),
				Function("random_chance", "random_chance(chance)", "True with that chance, from 0 (never) to 1 (always): if random_chance(0.25) then drop a coin."),
				Function("random_direction", "random_direction()", "A random direction of length 1, any way round with the same chance."),
				Function("random_point_in_circle", "random_point_in_circle(radius)", "A random point on the ground (y is 0) within radius of (0, 0, 0), every spot with the same chance. Add it to a position to spawn around it."),
				Function("noise", "noise(x), noise(x, y), noise(x, y, z) or noise(position)", "Smooth noise from -1 to 1 that changes gently as the position moves, for terrain, wobble and camera shake. The same position always gives the same value."),
				Function("ease", "ease(t, style)", "t from 0 to 1 bent into a curve, for smooth animation: ease(t, \"out\"). Styles: \"linear\", \"in\", \"out\", \"in out\", \"sine in\", \"sine out\", \"sine in out\", \"back in\", \"back out\", \"bounce out\", \"elastic out\"."),
				Function("color", "color(r, g, b, a) or color(\"#ff8800\")", "A color as the [r, g, b, a] list engine colors take, from 0 to 1 each (alpha is 1 when left out), or from hex text like \"#ff8800\" or \"#ff880080\"."),
				Function("hsv", "hsv(hue, saturation, value, alpha)", "A color from hue, saturation and value, each from 0 to 1, as an [r, g, b, a] list. The hue goes round the color wheel: 0 red, 1/3 green, 2/3 blue."),
				Function("time", "time()", "Seconds since the game started."),
				Function("frame", "frame()", "Frames since the game started."),
				Function("vector", "vector(x, y, z)", "A position or direction with x, y and z (z is 0 when left out)."),
				Function("distance", "distance(a, b)", "How far apart two vectors are."),

				// Built-in phrases
				Phrase("say (message)", "Shows a value on its own line."),
				Phrase("random number from (low) to (high)", "A random whole number from low to high."),
				Phrase("random item from (items)", "A random item of a list."),
				Phrase("distance from (a) to (b)", "How far apart two vectors are."),

				// Text
				Property("text", "length", "text.length", "How many characters the text has (also size and count)."),
				Method("text", "upper", "text.upper()", "The text in capital letters."),
				Method("text", "lower", "text.lower()", "The text in small letters."),
				Method("text", "trim", "text.trim()", "The text without spaces at the start and end."),
				Method("text", "split", "text.split(separator)", "A list of the parts between separators (spaces when left out)."),
				Method("text", "contains", "text.contains(part)", "True when the text holds the part."),
				Method("text", "starts_with", "text.starts_with(part)", "True when the text begins with the part."),
				Method("text", "ends_with", "text.ends_with(part)", "True when the text ends with the part."),
				Method("text", "find", "text.find(part)", "The position where the part starts, or nothing."),
				Method("text", "replace", "text.replace(old, new)", "The text with every old part replaced by new."),
				Method("text", "slice", "text.slice(start, end)", "The characters from start to end (to the end when left out)."),
				Method("text", "repeat", "text.repeat(n)", "The text n times in a row."),
				Method("text", "reverse", "text.reverse()", "The text backwards, letter by letter. As a sentence, reverse name changes the variable."),
				Method("text", "pad_start", "text.pad_start(width, fill)", "The text made at least width letters long by adding fill (a space if left out) in front: \"7\".pad_start(3, \"0\") is \"007\"."),
				Method("text", "pad_end", "text.pad_end(width, fill)", "The text made at least width letters long by adding fill (a space if left out) at the end."),
				Method("text", "characters", "text.characters()", "A list of the text's characters."),
				Method("text", "is_number", "text.is_number()", "True when the text is a number, like \"3.5\"."),
				Method("text", "is_empty", "text.is_empty()", "True when the text has no characters."),
				Method("text", "capitalize", "text.capitalize()", "The text with its first letter in capitals (a to z only): \"sword of fire\".capitalize() is \"Sword of fire\"."),
				Method("text", "title", "text.title()", "The text with the first letter of each word in capitals (a to z only): \"sword of fire\".title() is \"Sword Of Fire\"."),
				Method("text", "count", "text.count(part)", "How many times part is in the text, without overlapping: \"banana\".count(\"an\") is 2."),

				// Lists
				Property("list", "length", "list.length", "How many items the list has (also size and count)."),
				Property("list", "first", "list.first", "The first item, or nothing when the list is empty."),
				Property("list", "last", "list.last", "The last item, or nothing when the list is empty."),
				Method("list", "add", "list.add(item)", "Puts the item at the end of the list."),
				Method("list", "insert", "list.insert(position, item)", "Puts the item at a position, moving later items back."),
				Method("list", "remove", "list.remove(item)", "Removes the first equal item; true when one was found."),
				Method("list", "remove_at", "list.remove_at(position)", "Removes the item at a position and gives it back."),
				Method("list", "pop", "list.pop()", "Removes the last item and gives it back."),
				Method("list", "clear", "list.clear()", "Removes every item."),
				Method("list", "contains", "list.contains(item)", "True when the list holds an equal item."),
				Method("list", "index_of", "list.index_of(item)", "The position of the first equal item, or nothing."),
				Method("list", "is_empty", "list.is_empty()", "True when the list has no items."),
				Method("list", "copy", "list.copy()", "A separate copy of the list."),
				Method("list", "reverse", "list.reverse(), or: reverse list", "Turns the list around, last item first."),
				Method("list", "shuffle", "list.shuffle(), or: shuffle list", "Puts the items in a random order."),
				Method("list", "pick", "list.pick()", "A random item."),
				Method("list", "sort", "list.sort(), or: sort list", "Sorts the list from smallest to largest."),
				Method("list", "sort_by", "list.sort_by(function), or: sort list by field, sort list by function(x) ... end", "Sorts the list by what the function gives back for each item (or by each item's field)."),
				Method("list", "join", "list.join(separator)", "The items as one text, with the separator between them (\", \" when left out)."),
				Method("list", "slice", "list.slice(start, end)", "A new list of the items from start to end."),
				Method("list", "sum", "list.sum()", "All the numbers added together."),
				Method("list", "average", "list.average()", "The numbers' average, or nothing for an empty list."),
				Method("list", "min", "list.min()", "The smallest number, or nothing for an empty list."),
				Method("list", "max", "list.max()", "The largest number, or nothing for an empty list."),
				Method("list", "count", "list.count(function or value)", "How many items the function gives back true for, or how many items equal the value."),
				Method("list", "remove_where", "list.remove_where(function)", "Removes every item the function gives back true for, and gives back how many went."),
				Method("list", "any", "list.any(function)", "True when the function gives back true for at least one item."),
				Method("list", "all", "list.all(function)", "True when the function gives back true for every item (and for an empty list)."),
				Method("list", "map", "list.map(function)", "A new list of what the function gives back for each item."),
				Method("list", "filter", "list.filter(function)", "A new list of the items for which the function gives back true."),
				Method("list", "find", "list.find(function)", "The first item for which the function gives back true, or nothing."),
				Method("list", "each", "list.each(function)", "Calls the function with every item."),
				Method("list", "unique", "list.unique()", "A new list with each item once, where it first comes: [3, 1, 3, 2].unique() is [3, 1, 2]."),
				Method("list", "flatten", "list.flatten()", "A new list with the items of the lists inside, one level deep: [[1, 2], [3], 4].flatten() is [1, 2, 3, 4]."),
				Method("list", "chunk", "list.chunk(size)", "The list cut into lists of size items (the last may be shorter): [1, 2, 3, 4, 5].chunk(2) is [[1, 2], [3, 4], [5]]."),
				Method("list", "max_by", "list.max_by(function)", "The item for which the function gives the biggest number (the first on a tie), or nothing for an empty list."),
				Method("list", "min_by", "list.min_by(function)", "The item for which the function gives the smallest number (the first on a tie), or nothing for an empty list: enemies.min_by(function(e) return e.position.distance_to(my position) end)."),

				// Maps
				Property("map", "length", "map.length", "How many keys the map has (also size and count)."),
				Method("map", "keys", "map.keys()", "A list of the keys, in the order they were added."),
				Method("map", "values", "map.values()", "A list of the values, in the order they were added."),
				Method("map", "has", "map.has(key)", "True when the map has the key."),
				Method("map", "get", "map.get(key, default)", "The value for the key, or the default (nothing when left out)."),
				Method("map", "remove", "map.remove(key)", "Removes the key; true when it was there."),
				Method("map", "merge", "map.merge(other)", "Copies every entry of other into this map, replacing values with the same keys, and gives back this map."),
				Method("map", "clear", "map.clear()", "Removes every key."),
				Method("map", "copy", "map.copy()", "A separate copy of the map."),
				Method("map", "is_empty", "map.is_empty()", "True when the map has no keys."),

				// Vectors
				Property("vector", "x", "vector.x", "The x part (left and right)."),
				Property("vector", "y", "vector.y", "The y part (up and down)."),
				Property("vector", "z", "vector.z", "The z part (forward and back)."),
				Property("vector", "length", "vector.length", "How long the vector is."),
				Method("vector", "normalized", "vector.normalized()", "The same direction with a length of 1."),
				Method("vector", "dot", "vector.dot(other)", "The dot product: how much two directions point the same way."),
				Method("vector", "cross", "vector.cross(other)", "A vector at right angles to both."),
				Method("vector", "distance_to", "vector.distance_to(other)", "How far it is to the other vector."),
				Method("vector", "lerp", "vector.lerp(other, t)", "The point t of the way to the other vector."),
				Method("vector", "copy", "vector.copy()", "A separate copy of the vector."),
				Property("task", "running", "task.running", "Yes until the task has ended: set p to start patrol(), then if p.running then ... end."),
				Property("task", "done", "task.done", "Yes once the task has ended in any way (returned, failed or cancelled), so 'wait until p.done' never waits forever."),
				Property("task", "failed", "task.failed", "Yes when an error ended the task; task.problem says what went wrong."),
				Property("task", "cancelled", "task.cancelled", "Yes when task.cancel() stopped it."),
				Property("task", "result", "task.result", "What the task's function returned, or nothing until it has."),
				Property("task", "problem", "task.problem", "The message of the error that ended the task, like 'catch problem' gets; nothing otherwise."),
				Method("task", "cancel", "task.cancel()", "Stops the task where it is: its waits end and it never runs again. A task that has ended stays as it is."),
			};
		}

		const HelpEntry* Find(Kind kind, const std::string& owner, const std::string& name)
		{
			const std::vector<HelpEntry>& help = GetLanguageHelp();
			auto it = std::find_if(help.begin(), help.end(), [&](const HelpEntry& entry)
			{
				return entry.EntryKind == kind && entry.Owner == owner && entry.Name == name;
			});
			return it == help.end() ? nullptr : &*it;
		}

	}

	const std::vector<HelpEntry>& GetLanguageHelp()
	{
		static const std::vector<HelpEntry> s_Help = BuildHelp();
		return s_Help;
	}

	const HelpEntry* FindKeywordHelp(const std::string& word)
	{
		return Find(Kind::Keyword, "", word);
	}

	const HelpEntry* FindFunctionHelp(const std::string& name)
	{
		if (const HelpEntry* function = Find(Kind::Function, "", name))
			return function;
		return Find(Kind::Phrase, "", name);
	}

	const HelpEntry* FindMethodHelp(const std::string& owner, const std::string& name)
	{
		return Find(Kind::Method, owner, name);
	}

}
