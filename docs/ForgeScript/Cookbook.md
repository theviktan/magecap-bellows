# ForgeScript Cookbook

Thirty-one short recipes for things almost every game needs: moving, fighting, inventories, saving,
timers, menus, getting around a level and the math in between. Each recipe is a complete script you can copy, followed by
**how it works** for people new to ForgeScript and **going further** for people who know it.

Every recipe here is checked with `forgescript --check` against MageCap Bellows' API each time
ForgeScript is tested. The seventeen recipes that don't need the engine also carry their own `test`
blocks, and those run too, so what a recipe's tests say is what its code does. Run them yourself
with `forgescript --test recipe.forge`. The others need a scene, so copy them into a script attached
to an entity in MageCap Bellows.

New to the language? Start with the [Language Guide](LanguageGuide.md); every word used here is in
the [Quick Reference](Reference.md).

## Contents

**Moving things**
1. [Walk with the keyboard](#1-walk-with-the-keyboard)
2. [Jump only when on the ground](#2-jump-only-when-on-the-ground)
3. [A camera that follows smoothly](#3-a-camera-that-follows-smoothly)
4. [Patrol between points](#4-patrol-between-points)
5. [Turn to face the player](#5-turn-to-face-the-player)

**Health and combat**
6. [Health that can't go below zero](#6-health-that-cant-go-below-zero)
7. [Damage with armor and critical hits](#7-damage-with-armor-and-critical-hits)
8. [A cooldown](#8-a-cooldown)
9. [A moment of safety after a hit](#9-a-moment-of-safety-after-a-hit)
10. [Waves of enemies](#10-waves-of-enemies)
11. [The nearest enemy](#11-the-nearest-enemy)

**Items and progress**
12. [An inventory with stacks](#12-an-inventory-with-stacks)
13. [A shop](#13-a-shop)
14. [A loot table with weights](#14-a-loot-table-with-weights)
15. [Experience and levels](#15-experience-and-levels)
16. [A high-score table](#16-a-high-score-table)
17. [Saving and loading](#17-saving-and-loading)
18. [Achievements from events](#18-achievements-from-events)

**Flow and logic**
19. [A state machine](#19-a-state-machine)
20. [A quest with several goals](#20-a-quest-with-several-goals)
21. [A dialogue tree](#21-a-dialogue-tree)
22. [A shuffle bag](#22-a-shuffle-bag)
23. [A short cutscene](#23-a-short-cutscene)

**Screens and menus**
24. [A HUD that shows the score](#24-a-hud-that-shows-the-score)
25. [A countdown](#25-a-countdown)
26. [A pause menu](#26-a-pause-menu)
27. [A main menu](#27-a-main-menu)

**Math helpers**
28. [Snapping to a grid](#28-snapping-to-a-grid)
29. [Easing a value](#29-easing-a-value)
30. [Money, time and plurals](#30-money-time-and-plurals)

**Getting around a level**
31. [Chase the player around walls](#31-chase-the-player-around-walls)

---

## Moving things

### 1. Walk with the keyboard

Attach to the player. WASD or the arrow keys move it, at the same speed in every direction.

```forge
set speed to 5.0

when update with delta
	set direction to vector()
	if is_key_down("W") or is_key_down("Up") then add my forward to direction end
	if is_key_down("S") or is_key_down("Down") then take my forward from direction end
	if is_key_down("D") or is_key_down("Right") then add my right to direction end
	if is_key_down("A") or is_key_down("Left") then take my right from direction end
	if direction.length is above 0 then
		my translate(direction.normalized() * speed * delta)
	end
end
```

**How it works.** `when update with delta` runs once every frame, and `delta` is how many seconds the
frame took. Each held key adds a direction; `normalized()` makes the total one unit long, so moving
diagonally isn't faster. Multiplying by `delta` makes the speed the same on fast and slow computers:
`speed` is in units per second. `set speed to 5.0` also shows in the Inspector as a decimal field.

**Going further.** Use actions instead of keys (`is_action_down("move_forward")`) so players can
remap them. For physics bodies, set the RigidBody's `velocity` instead of moving the entity, or
collisions will fight the movement.

### 2. Jump only when on the ground

Attach to a player with a RigidBody. A ray straight down tells whether there's ground under it.

```forge
set jump_strength to 6.0
set foot_reach to 1.1

function on_ground()
	set hit to raycast(my position, vector(0, -1, 0), foot_reach)
	return hit is not nothing and hit.entity is not self
end

when update with delta
	if is_key_pressed("Space") and on_ground() then
		set body to my get_component("RigidBody")
		body.add_impulse(vector(0, jump_strength, 0))
	end
end
```

**How it works.** `raycast(origin, direction, distance)` gives back what the ray hits first, or
`nothing`. `foot_reach` is a little more than half the player's height, so the ray just reaches the
floor. `is_key_pressed` is true only in the frame the key went down, so holding Space jumps once.

**Going further.** Allow a jump for a few frames after leaving a ledge ("coyote time") by keeping
`set _last_grounded to time()` whenever `on_ground()` is true and jumping while
`time() - _last_grounded is below 0.1`.

### 3. A camera that follows smoothly

Attach to the camera. Name the player entity "Player" in the scene.

```forge
set offset to vector(0, 4, 8)
set smoothing to 5.0
set _target to nothing

when created
	set _target to find_entity("Player")
end

when update with delta
	if _target is nothing then return end
	set wanted to _target.position + offset
	set my position to my position.lerp(wanted, min(1, smoothing * delta))
	my look_at(_target.position)
end
```

**How it works.** Each frame the camera moves part of the way toward where it wants to be, so it
eases in instead of snapping. A bigger `smoothing` follows more tightly. `min(1, ...)` keeps a very
long frame from overshooting. `_target` is set at the top of the script so both `when` blocks share
it: a `set` only inside `when created` would make a variable that `when update` can't see. Names
starting with `_` stay out of the Inspector.

**Going further.** `lerp` with `smoothing * delta` slows slightly on low frame rates. For a result
that's exactly the same at any frame rate, use `1 - exp(-smoothing * delta)` as the blend amount.

### 4. Patrol between points

Attach to a guard. It walks from point to point, waits at each, and starts over.

```forge
set speed to 2.0
set pause_time to 1.5
set points to [vector(0, 0, 0), vector(6, 0, 0), vector(6, 0, 6)]

function patrol()
	repeat forever
		for each point in points do
			until my position.distance_to(point) is below 0.01 do
				set my position to move_toward(my position, point, speed * delta_time())
				wait 1 frame
			end
			wait pause_time seconds
		end
	end
end

when created
	start patrol()
end

when reloaded
	start patrol()
end
```

**How it works.** `start patrol()` runs the function as its own task, so it can `wait` without
holding up anything else. `move_toward` steps toward the point but never past it. `wait 1 frame`
lets the game draw before the next step. `when reloaded` starts the patrol again after you save the
script while the game runs.

**Going further.** Read the points from child entities (`my find_child("Point 1")`) so level
designers can drag them around in the editor.

### 5. Turn to face the player

Attach to a turret. It turns toward the player when the player comes close.

```forge
set sight to 12.0
set _player to nothing

when created
	set _player to find_entity("Player")
end

when update with delta
	if _player is nothing then return end
	if distance from my position to _player.position is at most sight then
		my look_at(_player.position)
	end
end
```

**How it works.** `distance from a to b` is the same as `distance(a, b)`. `look_at` turns the entity
so its front (its -Z side) points at the position.

**Going further.** To turn gradually, keep the angle in a variable and move it with
`move_toward(angle, wanted, turn_speed * delta)`, then set `my rotation_degrees`.

---

## Health and combat

### 6. Health that can't go below zero

A kind for anything that can be hurt. It never drops below 0 or above its maximum, and it announces
when it's defeated.

```forge
kind Fighter
	set name to "fighter"
	set health to 100
	set max_health to 100

	function setup(name, max_health is 100)
		set my name to name
		set my max_health to max_health
		set my health to max_health
	end

	function hurt(amount)
		set my health to clamp(my health - amount, 0, my max_health)
	end

	function heal(amount)
		set my health to clamp(my health + amount, 0, my max_health)
	end

	when my health changes with now
		if now is 0 then trigger defeated with my name end
	end
end

set defeated_names to []
when defeated with name
	add name to defeated_names
end

test "health stays between 0 and the maximum"
	set orc to new Fighter("Orc", 50)
	orc.hurt(80)
	expect orc.health is 0
	orc.heal(500)
	expect orc.health is 50
end

test "being defeated is announced once"
	set slime to new Fighter("Slime", 10)
	slime.hurt(4)
	slime.hurt(20)
	slime.hurt(20)
	expect defeated_names contains "Slime"
	expect defeated_names.count("Slime") is 1
end
```

**How it works.** `clamp(value, low, high)` keeps a number inside a range. `when my health changes`
runs right after the field gets a different value, so hitting a fighter already at 0 doesn't
announce it again. `trigger defeated with my name` reaches every `when defeated` block.

**Going further.** The change handler runs as its own task, so it may `wait`: play a death
animation, then `wait 1 second`, then remove the entity.

### 7. Damage with armor and critical hits

```forge
constant CRIT_CHANCE is 0.1
constant CRIT_MULTIPLIER is 2

function damage_after_armor(damage, armor)
	# Each point of armor takes away 1, but every hit does at least 1.
	return max(1, damage - armor)
end

function roll_damage(damage, armor)
	set result to damage_after_armor(damage, armor)
	if random() is below CRIT_CHANCE then
		multiply result by CRIT_MULTIPLIER
	end
	return result
end

test "armor lowers damage, but never to zero"
	expect damage_after_armor(10, 3) is 7
	expect damage_after_armor(2, 10) is 1
end

test "hits are normal or critical"
	random_seed(7)
	repeat 100 times
		set hit to roll_damage(10, 3)
		expect hit is 7 or hit is 14
	end
end
```

**How it works.** `constant` names a value that never changes, written in capitals by habit.
`random()` gives a number from 0 up to 1, so it's below 0.1 about one time in ten.
`random_seed(7)` makes the random numbers the same on every run, which is what a test needs.

**Going further.** Keep the rules in small functions like `damage_after_armor` that don't touch the
scene. They're easy to test, and the entity script just calls them.

### 8. A cooldown

An ability that can be used once every few seconds.

```forge
kind Cooldown
	set length to 1
	set ready_at to 0

	function setup(length)
		set my length to length
	end

	function is_ready(now)
		return now is at least my ready_at
	end

	function use(now)
		if not my is_ready(now) then return false end
		set my ready_at to now + my length
		return true
	end

	function left(now)
		return max(0, my ready_at - now)
	end
end

test "a cooldown can be used again only after its time"
	set dash to new Cooldown(2)
	expect dash.use(10)
	expect not dash.use(11)
	expect dash.left(11) is 1
	expect dash.use(12)
end
```

In a game, pass `time()`:

```forge
set dash_cooldown to 3.0
set _dash to nothing

when created
	set _dash to new Cooldown(dash_cooldown)
end

when update with delta
	if is_key_pressed("LeftShift") and _dash.use(time()) then
		my translate(my forward * 4)
	end
end

kind Cooldown
	set length to 1
	set ready_at to 0
	function setup(length)
		set my length to length
	end
	function use(now)
		if now is below my ready_at then return false end
		set my ready_at to now + my length
		return true
	end
end
```

**How it works.** Instead of counting down every frame, the cooldown remembers *when* it will be
ready. Giving it the time as `now` keeps the kind free of the engine, so the test can use made-up
times.

**Going further.** `time()` stops while the game is paused, so cooldowns pause too. Use
`real_time()` for something that should keep counting, like a daily reward.

### 9. A moment of safety after a hit

Attach to the player. After being hit, it can't be hurt again for a second, and it blinks.

```forge
set health to 5
set safe_time to 1.0
set _safe to false

when hit by damage
	if _safe then return end
	take damage from health
	set _safe to true
	start blink()
	after safe_time seconds
		set _safe to false
	end
end

function blink()
	repeat 10 times
		set my visible to not my visible
		wait safe_time / 10 seconds
	end
	set my visible to true
end
```

**How it works.** `after ... seconds ... end` runs its block once, later, as its own task, and the
lines after it carry on right away. `start blink()` runs the blinking at the same time.

**Going further.** Send the hit with the attacker (`trigger hit with 1, self`) and receive it as
`when hit with damage, attacker` to knock the player away from it.

### 10. Waves of enemies

Attach to a spawner. Every wave has more enemies, and the next wave starts when one is cleared.

```forge
set prefab to "Prefabs/Enemy.bprefab"
set first_wave to 3
set spread to 8.0
set _alive to 0
set _wave to 0

function spawn_wave()
	add 1 to _wave
	set count to first_wave + (_wave - 1) * 2
	repeat count times
		set place to my position + vector(lerp(-spread, spread, random()), 0, lerp(-spread, spread, random()))
		instantiate(prefab, place)
		add 1 to _alive
		wait 0.3 seconds
	end
	say "Wave {_wave}: {count} enemies"
end

when created
	start spawn_wave()
end

when enemy died
	take 1 from _alive
	if _alive is 0 then
		after 3 seconds
			start spawn_wave()
		end
	end
end
```

The enemy script sends `trigger enemy died` from its own `when destroyed` block.

**How it works.** `instantiate(path, position)` makes a copy of a prefab in the scene.
`lerp(-spread, spread, random())` is a random decimal between `-spread` and `spread`
(`random(a, b)` is for whole numbers, like a dice roll). Counting the living enemies tells when a
wave is over.

**Going further.** Give each wave its own list of prefabs, like `[["Bat"], ["Bat", "Orc"]]`, and
use `random item from (list)` to mix them.

### 11. The nearest enemy

```forge
function nearest(origin, positions)
	if positions is empty then return nothing end
	set best to positions[1]
	for each position in positions do
		if position.distance_to(origin) is below best.distance_to(origin) then
			set best to position
		end
	end
	return best
end

function within(origin, positions, reach)
	return positions.filter(function(p) return p.distance_to(origin) is at most reach end)
end

test "the nearest point wins"
	set enemies to [vector(10, 0, 0), vector(2, 0, 1), vector(-5, 0, 0)]
	expect nearest(vector(), enemies) is vector(2, 0, 1)
	expect nearest(vector(), []) is nothing
	expect within(vector(), enemies, 6).length is 2
end
```

With entities, compare their positions: `get_entities("Enemy")` lists the entities with that tag.

**How it works.** The loop keeps the best one seen so far. `filter` keeps the items for which the
function gives back true.

**Going further.** `positions.min_by(function(p) return p.distance_to(origin) end)` does the same in
one line. Compare squared distances (`(a - b).dot(a - b)`) to skip a square root when there are
hundreds of enemies.

---

## Items and progress

### 12. An inventory with stacks

```forge
constant STACK_LIMIT is 99

kind Inventory
	set items to {}

	function add_item(name, count is 1)
		set have to my items.get(name, 0)
		set my items[name] to min(STACK_LIMIT, have + count)
	end

	function remove_item(name, count is 1)
		set have to my items.get(name, 0)
		if have is below count then return false end
		if have is count then
			my items.remove(name)
		else
			set my items[name] to have - count
		end
		return true
	end

	function count_of(name)
		return my items.get(name, 0)
	end
end

test "stacks add up, up to a limit"
	set bag to new Inventory()
	bag.add_item("potion", 3)
	bag.add_item("potion")
	expect bag.count_of("potion") is 4
	bag.add_item("arrow", 500)
	expect bag.count_of("arrow") is 99
end

test "taking more than you have does nothing"
	set bag to new Inventory()
	bag.add_item("key")
	expect not bag.remove_item("key", 2)
	expect bag.remove_item("key")
	expect bag.count_of("key") is 0
	expect bag.items is empty
end
```

**How it works.** A map holds a count for each item name. `items.get(name, 0)` gives 0 for an item
you don't have yet. Each `new Inventory()` gets its own map.

**Going further.** A map's keys can be saved as they are with `save_data` (recipe 17). For items
with more than a count (durability, enchantments), keep a list of maps instead.

### 13. A shop

```forge
set prices to { sword: 120, shield: 80, potion: 15 }

function buy(wallet, bag, item)
	if not prices.has(item) then return "We don't sell {item}." end
	set price to prices[item]
	if wallet.gold is below price then
		return "You need {price - wallet.gold} more gold."
	end
	take price from wallet.gold
	add item to bag
	return "You bought a {item}."
end

test "buying takes the gold and gives the item"
	set wallet to { gold: 100 }
	set bag to []
	expect buy(wallet, bag, "shield") is "You bought a shield."
	expect wallet.gold is 20
	expect bag is ["shield"]
	expect buy(wallet, bag, "sword") is "You need 100 more gold."
	expect buy(wallet, bag, "boat") is "We don't sell boat."
end
```

**How it works.** Maps and lists are shared, not copied, when you pass them to a function, so
`take price from wallet.gold` changes the player's own wallet. Each answer is text you can show.

**Going further.** Return a map like `{ ok: false, reason: "..." }` when the caller needs to know
what happened, not only what to say.

### 14. A loot table with weights

```forge
set loot to [
	{ item: "coin", weight: 70 },
	{ item: "potion", weight: 25 },
	{ item: "crown", weight: 5 }
]

function roll_loot(table)
	set total to table.map(function(entry) return entry.weight end).sum()
	set roll to random() * total
	for each entry in table do
		if roll is below entry.weight then return entry.item end
		take entry.weight from roll
	end
	return table.last.item
end

test "common things drop more often"
	random_seed(42)
	set counts to { coin: 0, potion: 0, crown: 0 }
	repeat 1000 times
		add 1 to counts[roll_loot(loot)]
	end
	expect counts.coin is above counts.potion
	expect counts.potion is above counts.crown
	expect counts.crown is above 0
end
```

**How it works.** The weights add up to 100 here, so a coin has a 70 in 100 chance. The roll walks
down the table, taking away each weight, until it falls inside one.

**Going further.** Weights don't have to add up to 100. To raise the chance of rare things with a
luck stat, multiply their weight before rolling.

### 15. Experience and levels

```forge
function xp_for_level(level)
	# 100 for level 2, then each level needs 50% more than the last.
	return round(100 * 1.5 ^ (level - 2))
end

kind Hero
	set level to 1
	set xp to 0

	function gain(amount)
		add amount to my xp
		set levels to 0
		while my xp is at least xp_for_level(my level + 1) do
			take xp_for_level(my level + 1) from my xp
			add 1 to my level
			add 1 to levels
		end
		return levels
	end
end

test "experience carries over into the next level"
	set hero to new Hero()
	expect hero.gain(60) is 0
	expect hero.gain(60) is 1
	expect hero.level is 2
	expect hero.xp is 20
	expect hero.gain(1000) is 3
	expect hero.level is 5
end
```

**How it works.** `^` is "to the power of". The `while` loop handles a big reward that's worth more
than one level. `gain` gives back how many levels were gained, so the game can show "Level up!".

**Going further.** A table of numbers, like `[0, 100, 250, 500]`, is easier to balance by hand than
a formula; read it with `item (level + 1) of table`.

### 16. A high-score table

```forge
constant KEEP is 5

function add_score(table, name, score)
	add { name: name, score: score } to table
	sort table by function(entry) return -entry.score end
	while table.length is above KEEP do
		table.pop()
	end
	return table.index_of(table.find(function(entry) return entry.name is name and entry.score is score end))
end

test "the best scores stay, best first"
	set scores to []
	for each score in [50, 300, 120, 80, 999, 10] do
		add_score(scores, "player", score)
	end
	expect scores.length is 5
	expect first of scores is { name: "player", score: 999 }
	expect scores.last.score is 50
	expect add_score(scores, "Ava", 500) is 2
end
```

**How it works.** `sort table by` sorts by what the function gives back for each item; the minus
sign puts the biggest first. `pop()` drops the last item. Positions start at 1, so a new best score
gives 1.

**Going further.** Keep it between sessions with `save_data("scores", scores)` (recipe 17).

### 17. Saving and loading

Attach to a game manager.

```forge
set save_name to "slot1"
set _save to nothing

when created
	set _save to load_data(save_name, { level: 1, coins: 0, items: [] })
	say "Welcome back! You have {_save.coins} coins."
end

function save_game()
	save_data(save_name, _save)
end

when coin collected
	add 1 to _save.coins
	save_game()
end
```

**How it works.** `load_data(name, default)` gives back what was saved, or the default the first
time. Keeping everything in one map means one line saves it all. Saves go in the game's own folder,
and a half-written save never replaces a good one.

**Going further.** A New Game button starts over with `delete_data(save_name)` (recipe 27). Add a
`version: 1` field to the save. When the save's shape changes later,
`if _save.get("version", 0) is below 2 then ...` can upgrade old saves.

### 18. Achievements from events

```forge
set unlocked to []
set kills to 0

function unlock(name)
	if name is in unlocked then return end
	add name to unlocked
	trigger achievement with name
end

when enemy died
	add 1 to kills
	if kills is 1 then unlock("First blood") end
	if kills is 100 then unlock("Hundred") end
end

when level finished with seconds
	if seconds is below 60 then unlock("Speedrunner") end
end

test "achievements unlock once, from the game's events"
	repeat 3 times
		trigger enemy died
	end
	trigger level finished with 45
	trigger level finished with 30
	expect unlocked is ["First blood", "Speedrunner"]
end
```

**How it works.** The game sends events it already has (`trigger enemy died`), and this script
listens. Nothing else needs to know achievements exist. `trigger` runs every matching `when` block
right away, which is why the test can check the result on the next line.

**Going further.** Show a popup with a `when achievement with name` block in the HUD's script.

---

## Flow and logic

### 19. A state machine

```forge
choices Mood are idle, chasing and fleeing

kind Monster
	set mood to Mood.idle
	set health to 10

	function think(sees_player)
		match my mood
		is Mood.idle then
			if sees_player then set my mood to Mood.chasing end
		is Mood.chasing then
			if my health is below 3 then set my mood to Mood.fleeing end
			if not sees_player then set my mood to Mood.idle end
		is Mood.fleeing then
			if my health is at least 3 then set my mood to Mood.idle end
		end
	end
end

test "a hurt monster runs away"
	set wolf to new Monster()
	wolf.think(true)
	expect wolf.mood is Mood.chasing
	set wolf.health to 2
	wolf.think(true)
	expect wolf.mood is Mood.fleeing
end
```

**How it works.** `choices` names the states once, and `Mood.chasng` (a typo) is a mistake before
the game runs instead of a monster that never moves. A `match` over the choices with no `otherwise`
warns if one is left out.

**Going further.** Add `when my mood changes with now, before` to the kind to play the right
animation each time the state changes.

### 20. A quest with several goals

```forge
kind Goal
	set text to ""
	set needed to 1
	set progress to 0
	set done to false

	function setup(text, needed is 1)
		set my text to text
		set my needed to needed
	end

	function advance(amount is 1)
		set my progress to min(my needed, my progress + amount)
		set my done to my progress is my needed
	end
end

function quest_done(goals)
	return every goal in goals is done
end

function quest_text(goals)
	set lines to goals.map(function(g) return ("[x] " if g.done otherwise "[ ] ") + "{g.text} ({g.progress}/{g.needed})" end)
	return lines.join("\n")
end

test "the quest is done when every goal is"
	set goals to [new Goal("Find the key"), new Goal("Light the torches", 3)]
	goals[1].advance()
	goals[2].advance(2)
	expect not quest_done(goals)
	expect quest_text(goals) is "[x] Find the key (1/1)\n[ ] Light the torches (2/3)"
	goals[2].advance()
	expect quest_done(goals)
end
```

**How it works.** `every goal in goals is done` is true when each goal's `done` field is true.
`... if ... otherwise ...` picks one of two values in the middle of an expression.

**Going further.** `any goal in goals is done` tells whether the player has started at all.

### 21. A dialogue tree

```forge
set dialogue to {
	start: { line: "Halt! Who goes there?", choices: [["A friend.", "friend"], ["None of your business.", "rude"]] },
	friend: { line: "Welcome, friend.", choices: [] },
	rude: { line: "Then you shall not pass.", choices: [] }
}

function lines_for(path)
	set node to "start"
	set heard to [dialogue[node].line]
	for each pick in path do
		set answer, next to dialogue[node].choices[pick]
		add "> " + answer to heard
		set node to next
		add dialogue[node].line to heard
	end
	return heard
end

test "each answer leads to its own line"
	expect lines_for([1]) is ["Halt! Who goes there?", "> A friend.", "Welcome, friend."]
	expect lines_for([2]).last is "Then you shall not pass."
end
```

**How it works.** Each node has a line and a list of choices; each choice is the answer's text and
the node it leads to. `set answer, next to ...` takes a two-item list apart into two names. The
test "plays" the conversation by choosing answers by number.

**Going further.** Keep the dialogue in a `.json` file and read it with `from_json`, so writers can
edit it without touching the script.

### 22. A shuffle bag

Random, but fair: every item comes up once before any repeats, like dealing from a deck.

```forge
kind ShuffleBag
	set items to []
	set _left to []

	function setup(items)
		set my items to items
	end

	function next()
		if my _left is empty then
			set my _left to my items.copy()
			shuffle my _left
		end
		return my _left.pop()
	end
end

test "every item once before any repeats"
	random_seed(3)
	set bag to new ShuffleBag(["red", "green", "blue"])
	set dealt to [bag.next(), bag.next(), bag.next()]
	sort dealt
	expect dealt is ["blue", "green", "red"]
	expect bag.next() is in ["red", "green", "blue"]
end
```

**How it works.** The bag keeps a shuffled copy and hands items out from the end. When it's empty,
it shuffles a new copy. `copy()` matters: without it, shuffling would scramble the original list.

**Going further.** Use it for music tracks, enemy spawn spots or hints, anywhere plain `random`
repeating the same thing three times in a row would feel broken.

### 23. A short cutscene

Attach to a cutscene manager. Each line waits for the one before.

```forge
set camera_spot to vector(0, 3, -6)

when cutscene started
	set label to find_entity("Subtitle")
	set camera to primary_camera()
	show label
	set label.text to "The gate is opening..."
	trigger gate open
	wait for gate opened
	set camera.position to camera_spot
	set label.text to "Now go."
	wait 2 seconds
	hide label
	trigger cutscene finished
end
```

**How it works.** `wait for gate opened` pauses this cutscene until another script sends
`trigger gate opened`, however long the gate takes. `wait 2 seconds` holds the last line on screen.
Every `when` block runs as its own task, so the rest of the game keeps going.

**Going further.** Let the player skip it: keep a `_skipped` flag that a key sets, and check it
after each step with `if _skipped then return end`.

---

## Screens and menus

### 24. A HUD that shows the score

Attach to the HUD canvas, with a Text entity named "Score".

```forge
set score to 0

when created
	set _label to find_entity("Score")
	show_score()
end

function show_score()
	set _label.text to "Score: {format_number(score)}"
end

when points scored with amount
	add amount to score
	show_score()
end
```

**How it works.** Setting an entity's `text` changes what its Text component shows.
`format_number(1250)` gives `"1,250"`. Other scripts send `trigger points scored with 100`.

**Going further.** Only update the label when the score changes, as here, rather than every frame;
it keeps the HUD cheap.

### 25. A countdown

Attach to the HUD, with a Text entity named "Timer". It counts down and ends the round at zero.

```forge
set round_seconds to 90

when created
	set _left to round_seconds
	set _label to find_entity("Timer")
	set _label.text to format_time(_left)
	every 1 second
		take 1 from _left
		set _label.text to format_time(_left)
		if _left is at most 0 then
			trigger round over
			stop
		end
	end
end
```

**How it works.** `every 1 second ... end` runs its block once a second, and `stop` inside it ends
the timer. `format_time(75)` gives `"1:15"`. The timer holds while the game is paused.

**Going further.** Turn the label red for the last ten seconds with the Text component's `color`
(`_label.get_component("Text").color`).

### 26. A pause menu

Attach to the HUD canvas, with a panel named "Pause Menu" under it and a button named "Resume" in
the panel. Tick **Run While Paused** on the HUD's Script component: a paused game stops
`when update` in every other script, and this one has to keep hearing Esc.

```forge
set _menu to nothing

when created
	set _menu to find_entity("Pause Menu")
	hide _menu
end

function toggle_pause()
	if the game is paused then
		resume the game
		hide _menu
	else
		pause the game
		show _menu
		focus find_entity("Resume")
	end
end

when update with delta
	if is_key_pressed("Escape") then toggle_pause() end
end

when "Resume" is clicked
	toggle_pause()
end
```

**How it works.** `pause the game` stops physics, `time()` and scripts, except those marked Run
While Paused. A click goes to the button's script and then up through its parents' scripts, so the
HUD's `when "Resume" is clicked` hears the button inside it. `focus` lets keyboard and gamepad
players press Resume right away.

**Going further.** Give the pause menu a canvas of its own and make it modal in the Inspector, so
the game behind it gets no clicks or keys while it shows. Then move this script onto that canvas.

### 27. A main menu

Attach to the menu's canvas.

```forge
when created
	set continue_button to find_entity("Continue")
	if not has_data("slot1") then disable continue_button end
	focus find_entity("New Game")
end

when "New Game" is clicked
	delete_data("slot1")
	load_scene("Scenes/Level1.bscene")
end

when "Continue" is clicked
	load_scene("Scenes/Level1.bscene")
end

when "Quit" is clicked
	quit()
end
```

**How it works.** A button's click goes to its own script and then up to its parents' scripts, so
one script on the canvas handles every button by name. `disable` greys out Continue when there's no
save yet.

**Going further.** `when clicked with widget` catches every button, with `widget.name` telling which.

---

## Math helpers

### 28. Snapping to a grid

```forge
constant TILE is 2

function to_tile(position)
	return vector(floor(position.x / TILE), 0, floor(position.z / TILE))
end

function tile_center(tile)
	return vector(tile.x * TILE + TILE / 2, 0, tile.z * TILE + TILE / 2)
end

test "positions land in the right tile"
	expect to_tile(vector(3.9, 0, 0.5)) is vector(1, 0, 0)
	expect to_tile(vector(-0.5, 0, 4)) is vector(-1, 0, 2)
	expect tile_center(vector(1, 0, 0)) is vector(3, 0, 1)
	expect snapped(17, 5) is 15
end
```

**How it works.** Dividing by the tile size and rounding down with `floor` gives the tile number,
also for negative positions. `snapped(value, step)` rounds to the nearest step, for placing things
on grid lines.

**Going further.** Keep a map from `"{tile.x},{tile.z}"` to what's built there to know which tiles
are free.

### 29. Easing a value

```forge
function ease_in_out(t)
	return smoothstep(0, 1, t)
end

function ease_out_back(t)
	# Overshoots a little, then settles: good for popping up menus.
	set c to 1.70158
	return 1 + (c + 1) * (t - 1) ^ 3 + c * (t - 1) ^ 2
end

function eased_between(start_value, end_value, t, curve)
	return lerp(start_value, end_value, curve(clamp(t, 0, 1)))
end

test "eases start and end where they should"
	expect ease_in_out(0) is 0
	expect ease_in_out(1) is 1
	expect ease_in_out(0.5) is 0.5
	expect round(ease_out_back(1), 6) is 1
	expect ease_out_back(0.8) is above 1
	expect eased_between(10, 20, 2, ease_in_out) is 20
end
```

In a game, move `t` from 0 to 1 over the length of the animation:

```forge
set _start to 0

when created
	set _start to time()
end

when update with delta
	set t to (time() - _start) / 0.4
	set my scale to vector(1, 1, 1) * lerp(0, 1, smoothstep(0, 1, min(t, 1)))
end
```

**How it works.** An easing function turns a straight 0-to-1 into a curve: slow at the ends, or
overshooting. `eased_between` takes the easing as a value (`curve`), so any function can be passed
in. Its names stay clear of the built-in `ease` and of Bellows' `tween`, so neither is hidden.

**Going further.** ForgeScript has the common curves built in: `ease(t, "back out")` is the same as
`ease_out_back(t)`, and `"in"`, `"out"`, `"in out"`, `"bounce out"` and `"elastic out"` are there
too. Write your own, as here, when you want a curve of your own. In Bellows, `tween(self,
"position", vector(0, 5, 0), 2, "out")` moves a property over time with the same curves.

### 30. Money, time and plurals

```forge
function plural(count, word)
	return "{count} {word}" if count is 1 otherwise "{count} {word}s"
end

function price_text(cents)
	return "$" + format_number(cents / 100, 2)
end

test "numbers read well"
	expect plural(1, "coin") is "1 coin"
	expect plural(3, "coin") is "3 coins"
	expect price_text(123456) is "$1,234.56"
	expect format_time(3725) is "1:02:05"
end
```

**How it works.** `format_number(x, 2)` adds thousands separators and two decimals.
`format_time(seconds)` gives minutes and seconds, with hours when there are any.

**Going further.** Keep money in whole cents, as here, so adding prices never gives 0.30000000000000004.

## Getting around a level

### 31. Chase the player around walls

Attach to an enemy that has a **Nav Agent** component (**Add Component > Behavior > Nav Agent**), in
a scene with a baked navmesh (the **Settings** panel, **Scene** tab, **Navigation**, then **Bake
Navigation**). It walks around walls to the player when the player comes
close, and stops when the player gets away.

```forge
set chase_range to 12
set check_every to 0.5

function chase()
	repeat forever
		set player to find_entity("Player")
		if player is not nothing and my position.distance_to(player.position) is below chase_range then
			walk self to player.position
		else
			halt self
		end
		wait check_every seconds
	end
end

when created
	start chase()
end

when reloaded
	start chase()
end

when destination reached with who, reached
	if who is self and not reached then
		say "{self.name} can't get to the player from here"
	end
end
```

**How it works.** `walk self to player.position` gives the Nav Agent a destination, and the agent
finds a way around walls and steps around other agents on its own. Doing it again every half second
keeps up with a player who moves, without working out a new path every frame. `halt self` stops it
where it stands. `destination reached` comes when an agent arrives, with `reached` false when the
place couldn't be reached and the agent stopped as close as it could get. Every agent's arrival
reaches every script, so `who is self` picks this one.

**Going further.** `the path from my position to player.position` gives the corners of the way
there, as a list of vectors: add up the distances between them to know how far the walk really is,
not just how far away the player stands. `nearest_navigable_point(point)` moves a spawn point or a
clicked spot onto the navmesh, and `is_navigable(point)` says whether an agent could stand there.
Set the agent's `speed`, `acceleration` and `stopping_distance` in the Inspector or from a script
(`set my_agent.speed to 6` after `set my_agent to self.get_component("NavAgent")`).
