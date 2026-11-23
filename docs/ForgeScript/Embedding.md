# The ForgeScript C++ API

This guide explains, step by step, how C++ code hosts ForgeScript: creating the virtual machine, exposing types, functions and phrases, running scripts and sending events. Bellows' own scripting layer is built on exactly this API. A small working host lives in [`ForgeScript/Examples/EngineIntegration/Main.cpp`](../../ForgeScript/Examples/EngineIntegration/Main.cpp); it builds and runs as the `ForgeScriptEngineExample` test, so it always matches the code.

**Requirements:** a C++17 compiler (Visual Studio 2022 on Windows, GCC 11+ or Clang 14+ on Linux) and CMake 3.20+. ForgeScript has no other dependencies.

---

## Step 1: Link the library

ForgeScript is built as part of MageCap Bellows from the `ForgeScript/` folder. Any target that hosts scripts links the `ForgeScript` CMake target:

```cmake
target_link_libraries(MyTarget PRIVATE ForgeScript)
```

Then include the one public header: `#include "ForgeScript/ForgeScript.h"`.

The library only uses the C++ standard library, so it can also be copied into another C++20 project on its own.

---

## Step 2: Create one VM for the game

A `ForgeScript::VM` holds every script, every value and the garbage collector. Create **one** per running game (the editor's play mode and an exported game each create their own).

```cpp
#include "ForgeScript/ForgeScript.h"

class ScriptEngine
{
public:
	ScriptEngine()
		: m_VM(MakeConfig())
	{
		m_VM.SetPrintHandler([](const std::string& text) { EngineLog::Info("[script] {}", text); });
		m_VM.SetErrorHandler([](const ForgeScript::ScriptError& error) { EngineLog::Error("{}", error.ToString()); });
	}

private:
	static ForgeScript::VMConfig MakeConfig()
	{
		ForgeScript::VMConfig config;
		config.InstructionLimit = 10'000'000; // A loop that never waits becomes an error, not a frozen game.
		config.MaxCallDepth = 512;            // Runaway recursion becomes an error, not a crash.
		return config;
	}

	ForgeScript::VM m_VM;
};
```

- `SetPrintHandler` receives everything scripts `say` or `print`. Send it to your console panel.
- `SetErrorHandler` receives every uncaught error with the message, file, line and a call trace. `Trace` is readable text; `Frames` holds the same steps as data, innermost first: each `StackFrame` has `File`, `Line`, `Function` (empty for code at the top of a script) and `Repeats` (a function calling itself shows once, with the count). Use `Frames` to send errors to other tools instead of reading the text.
- The VM is **not** thread safe. Use it from the main (game) thread only.
- `StartProfile()` and `TakeProfile()` tell where scripts spend their instructions: each `ProfileEntry` has the `Function` (empty for a script's top level), its `File` and `Line`, the `Instructions` executed in its own code and its `Calls`, most instructions first. Show them in a debug overlay to find what to optimize; it costs a little time per instruction while on.
- `config.CountInstructions = true` makes the VM count every instruction it executes; `GetInstructionsRun()` gives the total. The count is the same on every machine, so it suits a performance test of your own scripts, as `forgescript --benchmark` uses it. It's off by default because counting costs a little time on every instruction (a set `InstructionLimit` counts too).

---

## Step 3: Expose your entity type

A `ForgeScript::NativeType` tells ForgeScript how scripts may read, change and call a C++ type.

Do not hand scripts raw pointers into an ECS registry, because entities can move or be destroyed. Hand them a small reference object instead and look the entity up each time:

```cpp
struct EntityRef
{
	Scene* Owner = nullptr;
	UUID ID;
};

static Entity Resolve(void* data)
{
	EntityRef* ref = static_cast<EntityRef*>(data);
	return ref->Owner->TryGetEntityByUUID(ref->ID); // An invalid Entity if it was destroyed.
}

void ScriptEngine::RegisterEntityType()
{
	m_EntityType.Name = "Entity";

	// Called when the script wrapper is garbage collected.
	m_EntityType.Finalizer = [](void* data) { delete static_cast<EntityRef*>(data); };
	// Lets scripts get a clear "This Entity no longer exists" error.
	m_EntityType.IsAlive = [](void* data) { return (bool)Resolve(data); };
	m_EntityType.ToText = [](void* data) { return "Entity(" + Resolve(data).GetName() + ")"; };

	// Read-only property:  say my name
	m_EntityType.Getters["name"] = [](ForgeScript::VM& vm, void* data)
	{
		return vm.MakeText(Resolve(data).GetName());
	};

	// Read and write:  set my position to vector(0, 1, 0)   and   add 1 to my position.y
	m_EntityType.Getters["position"] = [](ForgeScript::VM& vm, void* data)
	{
		glm::vec3 p = Resolve(data).GetComponent<TransformComponent>().Translation;
		return vm.MakeVector(p.x, p.y, p.z);
	};
	m_EntityType.Setters["position"] = [](ForgeScript::VM&, void* data, const ForgeScript::Value& value)
	{
		if (!ForgeScript::IsObjectType(value, ForgeScript::ObjectType::Vector))
			throw ForgeScript::RuntimeError("position must be a vector");
		ForgeScript::Vector* v = ForgeScript::As<ForgeScript::Vector>(value);
		Resolve(data).GetComponent<TransformComponent>().Translation = { v->X, v->Y, v->Z };
	};

	// Method:  self.destroy()
	m_EntityType.Methods["destroy"] = { 0, [this](ForgeScript::CallContext& context)
	{
		EntityRef* ref = context.SelfAs<EntityRef>(m_EntityType);
		m_Scene->DestroyEntity(Resolve(ref));
		return ForgeScript::Value();
	} };
}

ForgeScript::Value ScriptEngine::Wrap(Entity entity)
{
	return m_VM.MakeNative(m_EntityType, new EntityRef{ m_Scene, entity.GetUUID() });
}
```

**Vectors and write back.** Getters that return vectors hand the script a copy. When a script writes `set my position.x to 5` or `add 1 to my position.y`, ForgeScript reads `position`, changes the copy, and then calls the `position` setter with the result automatically. Properties with only a getter are read-only, and scripts get a clear error if they try to change them.

**Errors from C++.** Throw `ForgeScript::RuntimeError("message")` from any getter, setter, method or function. Scripts can catch it with `try ... catch`, and if they do not, it is reported with the script's file and line.

**Identity.** Each `MakeNative` call makes a new wrapper, so by default two wrappers of the same C++ object are not equal. Set `Equals` (and `Hash`, to use them as map keys) so `if other is player` works:

```cpp
m_EntityType.Equals = [](void* a, void* b) { return static_cast<EntityRef*>(a)->ID == static_cast<EntityRef*>(b)->ID; };
m_EntityType.Hash = [](void* data) { return std::hash<uint64_t>()(static_cast<EntityRef*>(data)->ID); };
```

The `NativeType` must stay alive as long as the VM, so store it as a member next to the VM.

---

## Step 4: Add engine functions and phrases

**Functions** are called with brackets:

```cpp
m_VM.RegisterFunction("spawn", 2, [this](ForgeScript::CallContext& context)
{
	const std::string& prefab = context.Text(0);   // Errors clearly if it is not text.
	ForgeScript::Vector* at = context.VectorArg(1);
	Entity entity = m_Scene->Instantiate(prefab, { at->X, at->Y, at->Z });
	return Wrap(entity);
});
// Script:  set bullet to spawn("Bullet", my position)
```

**Phrases** are called with words, which is what makes scripts read like English:

```cpp
m_VM.RegisterPhrase("move (thing) toward (target) by (amount)", [this](ForgeScript::CallContext& context)
{
	Entity thing = Resolve(context.NativeArg<EntityRef>(0, m_EntityType));
	Entity target = Resolve(context.NativeArg<EntityRef>(1, m_EntityType));
	float amount = (float)context.Number(2);
	// ... move thing toward target ...
	return ForgeScript::Value();
});
// Script:  move self toward player by speed * delta
```

**Functions with members.** A native function can carry values that scripts reach with a dot, which is how a type offers both a constructor and helpers: `Material("Gold")` and `Material.load("Materials/Gold.bmat")`.

```cpp
ForgeScript::Value material = m_VM.MakeFunction("Material", ForgeScript::AnyArity, makeMaterial);
m_VM.SetFunctionMember(material, "load", m_VM.MakeFunction("Material.load", 1, loadMaterial));
m_VM.SetSharedGlobal("Material", material);
```

Use `AnyArity` as the arity for functions that accept any number of values, and `context.Count()` to see how many were given. Values the host wants every script to see go in shared globals: `m_VM.SetSharedGlobal("gravity", ForgeScript::Value::FromNumber(9.81));`.

**CallContext helpers:** `Arg(i)`, `Count()`, `Number(i)`, `Integer(i)`, `Bool(i)`, `Text(i)`, `ListArg(i)`, `MapArg(i)`, `VectorArg(i)`, `FunctionArg(i)`, `NativeArg<T>(i, type)`, `Self()`, `SelfAs<T>(type)`, `Error(message)`, `GetVM()`.

**Making values:** `Value::MakeNothing()`, `Value::FromBool(b)`, `Value::FromNumber(n)`, `vm.MakeText(s)`, `vm.MakeList({...})`, `vm.MakeMap()`, `vm.MakeVector(x, y, z)`, `vm.MakeNative(type, data)`, `vm.MakeFunction(name, arity, fn)`.

---

## Step 5: Give each scripted entity its own module

A `ForgeScript::Module` holds one script's variables and `when` handlers. Create one per entity that has a `ScriptComponent`, so every enemy keeps its own `health`, `target` and so on:

```cpp
bool ScriptEngine::Attach(Entity entity, const std::string& source, const std::string& fileName)
{
	ForgeScript::Module* module = m_VM.CreateModule(fileName);
	m_VM.SetGlobal(module, "self", Wrap(entity));       // 'self' and 'my' now mean this entity.
	entity.GetComponent<ScriptComponent>().Module = module;

	ForgeScript::LoadResult result = m_VM.RunInModule(module, source, fileName);
	return result.Success;  // Errors were already sent to the error handler.
}
```

`RunInModule` compiles the script and runs its top level right away (stopping at the first `wait`, if any).

To let scripts write `use "helpers"`, tell the VM how to find script files:

```cpp
m_VM.SetModuleLoader([](const std::string& path, std::string& outName, std::string& outSource)
{
	std::filesystem::path file = Project::GetScriptsFolder() / (path + ".forge");
	outName = file.filename().string();
	return FileSystem::ReadText(file, outSource);
});
```

---

## Step 6: Drive scripts from the game loop

Once per frame, send the per-frame event and then move script time forward:

```cpp
void ScriptEngine::OnUpdate(Timestep ts)
{
	m_VM.EmitAll("update", { ForgeScript::Value::FromNumber(ts.GetSeconds()) });
	m_VM.Update(ts.GetSeconds());   // Resumes every task whose wait has finished.
}
```

Send other events whenever they happen:

```cpp
// On scene start:        when game starts
m_VM.EmitAll("game starts");

// From physics:          when collided with other
m_VM.Emit(module, "collided", { Wrap(otherEntity) });

// From input:            when key pressed with key
m_VM.EmitAll("key pressed", { m_VM.MakeText("space") });
```

- `Emit` runs every matching `when` in one module. `EmitAll` sends the event to every module.
- `Emit(module, event, values, source)` names the entity that raised the event (`EmitAll(event, values, source)` too, for a broadcast about one entity). Handlers written `when "Start" is clicked` run only when `source` is `"Start"` (ignoring letter case); without a source (and with `EmitAll`) they never run. MageCap Bellows passes the widget's name for UI events.
- `Emit` also wakes the module's tasks that `wait for` the event (after its `when` blocks), and `HasHandler(module, event)` is true while a task waits for it, so a host that sends an event only to modules with a handler reaches those scripts too.
- Missing values arrive as `nothing`; extra values are ignored, so scripts can take only what they need.
- Route the script-side `trigger` statement with `SetTriggerHandler`. The default sends the event back to the same script; many games want it to reach every script:

```cpp
m_VM.SetTriggerHandler([this](ForgeScript::Module*, const std::string& event, const std::vector<ForgeScript::Value>& values)
{
	m_VM.EmitAll(event, values);
});
```

  The handler may throw `ForgeScript::RuntimeError` (for example when your own event system refuses the event): it becomes an error on the `trigger` line, which the script can catch. A `trigger` nested more than `VMConfig::MaxNestedCalls` (64) deep inside `when` handlers fails the same way, so a handler that triggers itself can never use up the C++ stack.

To call a script function from C++ and get a result (for example an AI scoring function):

```cpp
ForgeScript::Value score = m_VM.GetGlobal(module, "score_target");
ForgeScript::Value result;
if (m_VM.Call(score, { Wrap(target) }, &result) && result.IsNumber())
	best = result.AsNumber();
```

Functions called this way must finish right away; they cannot `wait`.

To hear when a script's variables change (for example to send an "a field changed" event to a HUD), watch them after the script's first run, so its starting values aren't reported:

```cpp
m_VM.SetScriptVariableChangedHandler([this](ForgeScript::Module* module, const std::string& name,
	const ForgeScript::Value& now, const ForgeScript::Value& before)
{
	m_Pending.push_back({ EntityOf(module), name, now, before }); // queue it; send it after the script's step
});
m_VM.WatchScriptVariables(module, { "health", "speed" });
```

- **What reports:** every write to a watched variable: `set`, `add ... to`, `take ... from`, `set a, b to ...`, writes from the script's functions and handlers, from another script (`set player.health to 5`) and from `SetGlobal`.
- **What doesn't:** a write of the same value (`==`: numbers, text and vectors by their contents, lists and maps by identity, so `items.add(x)` reports nothing), hot reload (`CarryOverVariables`) and `use`.
- **Queue, don't run:** the handler runs in the middle of the script's instruction. Keep the values (copy them into your queue) and run other scripts from the queue afterwards.
- **Cost:** a module with no watched names pays one test per variable write; a watched module one lookup.
- `WatchScriptVariables(module, {})` stops it; a new list replaces the old one.

---

## Step 7: Destroying entities and stopping play mode

When an entity with a script is destroyed:

```cpp
m_VM.UnloadModule(module);   // Cancels its waiting tasks; its memory is collected later.
```

Any other script still holding that entity gets the error "This Entity no longer exists (it was destroyed)." the next time it uses it, thanks to `IsAlive`.

When play mode stops, destroy the whole `ScriptEngine` (and so the VM). Everything scripts created is freed.

---

## Step 8: Holding script values in C++

Script values are cleaned up by a garbage collector. A `Value` that only lives in a C++ variable between frames is **not** protected. Keep it in a `ForgeScript::Handle`:

```cpp
ForgeScript::Handle m_OnClick;  // member

m_OnClick = ForgeScript::Handle(m_VM, m_VM.GetGlobal(module, "on_click"));
// later, in another frame:
m_VM.Call(m_OnClick.Get(), {});
```

Values passed straight into `Emit`, `Call`, `SetGlobal` and `SetSharedGlobal` are safe without a handle.

---

## Step 9: Editor features

- **Check a script for mistakes without running it** (for an error underline in the editor):

```cpp
ForgeScript::CompileError error;
if (!m_VM.Check(source, "enemy.forge", error))
	ShowError(error.Line, error.Column, error.Message);
```

  `Check` also reports a `use` whose script your module loader cannot find (`Could not find the script "Scripts/Missing" to use.`), so set the same loader the game uses before checking. Call `m_VM.ClearModuleCache()` first if scripts may have changed on disk since the last check.

- **Every mistake at once** (for an editor's problem list): `std::vector<ForgeScript::CompileError> mistakes = m_VM.FindMistakes(source, "enemy.forge");` returns all mistakes it can find, sorted by line, and includes the one `Check` reports. It also reports a `use` whose script your module loader cannot find, so set the loader first.
- **Settings for an inspector:** `ForgeScript::FindScriptVariables(source)` (`ForgeScript/Compiler/ScriptVariables.h`) lists the top-level `set name to <value>` lines whose value is a plain number, text, true/false, `vector(...)` or color list, with their line numbers, without running the script. Bellows shows these as Inspector fields and, when the entity starts, applies edited values with `m_VM.SetGlobal(module, name, value)` after `RunInModule` and before sending `created`.

```cpp
for (const ForgeScript::ScriptVariable& variable : ForgeScript::FindScriptVariables(source))
	AddField(variable.Name, variable.Kind, variable.Number, variable.Text);
```

- **Hot reload** while the game runs: check the new text first (`FindMistakes`) and keep the old module if it has mistakes. Otherwise call `m_VM.ClearModuleCache()`, run the new source in a fresh module for the entity, then `m_VM.CarryOverVariables(oldModule, newModule, {"self"})` so script variables keep their values (constants, functions, phrases, kinds and values whose type changed take the new version), and unload the old module, which stops its waiting tasks. `m_VM.GetGlobalNames(module)` lists a module's names. Bellows' `ForgeScriptRuntime::Reload` does exactly this and then sends `reloaded`.
- **Debugger** (breakpoints, stepping, call stack and variables). A stop holds one task where it is, like a `wait` with no end, and then `Update` runs nothing and time does not move until you go on:

```cpp
m_VM.SetDebugHandler([this](const ForgeScript::DebugStop& stop)
{
	// Called at the moment of the stop. Do not wait for the user here: note it, pause the game, return.
	m_Paused = true;
	ShowStop(stop.File, stop.Line); // stop.Reason: Breakpoint, Step or Pause
});
m_VM.SetBreakpoints("Scripts/Enemy.forge", { 12, 30 }); // the name the script was loaded with; {} removes them
m_VM.Pause();                                            // stop the next line any task runs

// Later, from the editor's buttons (only while m_VM.IsStopped()):
for (const ForgeScript::DebugFrame& frame : m_VM.GetCallStack())          // innermost first
	AddCallRow(frame.Function.empty() ? frame.File : frame.Function, frame.Line);
for (const ForgeScript::DebugVariable& variable : m_VM.GetVariables(0))   // frame 0 = innermost
	AddVariableRow(variable.Name, variable.Type, variable.Text);
for (const ForgeScript::DebugVariable& variable : m_VM.GetGlobals(m_VM.GetCallStack()[0].Owner))
	AddScriptVariableRow(variable.Name, variable.Type, variable.Text);
m_VM.StepOver();   // or StepIn, StepOut, Continue
m_Paused = m_VM.IsStopped();
```

Breakpoints can have a condition or a log message (1.7.0):

```cpp
m_VM.SetDetailedBreakpoints("Scripts/Enemy.forge", {
	{ 12, "hp < 10", "" },               // stops only when hp < 10 there
	{ 30, "", "hp is {hp}, at {me.position}" } // prints, never stops
});
ForgeScript::DebugVariable value;
std::string error;
if (m_VM.Evaluate(0, "enemy.health * 2", value, error))    // while stopped: frame 0 = innermost
	ShowValue(value.Type, value.Text);
else
	ShowError(error);
```

- **Conditions, log messages and Evaluate** are one line of ForgeScript, worked out in that frame: its variables, the script's variables and the engine's values. Engine properties can be read (`me.position`, `enemy.health`), but no function or method is called (that fails with a message), so working something out never changes the game. A kind's `to_text` isn't called either.
- A condition or log message that can't be worked out (a typo, `nothing.hp`) **stops** there, with the reason in `DebugStop::Message`, so it is never skipped silently.
- A log message is ForgeScript text: `{...}` is worked out, everything else is printed as written, through the print handler.
- `SetBreakpoints(file, lines)` still sets plain breakpoints, and replaces a script's conditions and log messages too.

  A step that reaches a `wait` or the end of its handler returns with nothing stopped; the task stops on its next line when it runs again. Only code that may wait can stop: functions that built-in functions call back (`sort_by`, `map`, `filter`, a kind's `to_text`) and `VM::Call` pass over their breakpoints. Event handlers run as their own tasks, so step into one with a breakpoint in it. A task that reaches a breakpoint while another is stopped waits its turn; the handler is called for it when the first goes on. `UnloadModule` and `CancelTasks` end the stops of the tasks they cancel. `StopDebugging()` turns the debugger off before a game shuts down: it removes every breakpoint, pause and step and cancels the held tasks without calling the handler. Variables are shown without running script code (a kind's `to_text` is not called), and long values are shortened. Lines are checked only while there are breakpoints, a pause request or a step, so a game without them runs at full speed. In Bellows, `ForgeScriptRuntime::SetBreakpoints` and `SetDebugHandler` keep both across play sessions, and `GetMachine()` gives the session's VM for the buttons above. Its `Stop()` turns the debugger off before sending `destroyed`, and `Reload` refuses (returning false with a message) while a script is stopped.
- **Interactive console:** run typed lines in one module with `CompileOptions::Repl = true`, which prints the value of a line that is only an expression (see `ForgeScript/CLI/Source/Main.cpp`).
- **Repeatable random numbers** for replays and tests: `m_VM.SetRandomSeed(1234);`
- **The same easing curves as `ease()`** for engine tweens and animation (`ForgeScript/Core/Easing.h`, included by `ForgeScript.h`). `TryEase` matches a style name the way `ease()` does (spaces around it and letter case don't matter) and clamps `t` to 0 to 1; `EaseStyleNames()` lists the 11 names for your own error message:

```cpp
double eased = 0.0;
if (!ForgeScript::TryEase(style, elapsed / duration, eased))
	return Error("'" + style + "' isn't an easing style.");
position = start + (target - start) * eased;
```

---

## Step 10: Scripts for AI agents and automated builds

Everything a script can do goes through the functions, phrases and types you register, so an AI agent driving the editor can:

1. write `.forge` files,
2. call `VM::Check` to catch mistakes with exact line and column numbers,
3. run them in play mode and read the print and error handlers.

Scripts can carry their own tests (`test "name" ... expect ... end`). They never run when a script runs; to run them, load the script and call `VM::RunTest` for each name `VM::GetTestNames` gives:

```cpp
ForgeScript::LoadResult loaded = vm.Load(source, "combat.forge");
const std::vector<std::string> tests = vm.GetTestNames(loaded.LoadedModule);
for (size_t i = 0; i < tests.size(); i++)
{
	std::string failure;
	if (!vm.RunTest(loaded.LoadedModule, i, failure))
		std::cout << "FAIL " << tests[i] << "\n" << failure << "\n";
}
```

`RunTest` runs the test as a task and calls `Update` itself (1/60 second steps by default) until it ends, so a test may `wait`. The error handler still sees a failing test's error; `forgescript --test` quiets it and prints the failure itself.

Register the same engine API for the editor's automation tools, and agents can build game logic (Tetris rules, enemy AI, UI flows) as ForgeScript scripts without touching C++.
