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
- `SetErrorHandler` receives every uncaught error with the message, file, line and a call trace.
- The VM is **not** thread safe. Use it from the main (game) thread only.

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
- Missing values arrive as `nothing`; extra values are ignored, so scripts can take only what they need.
- Route the script-side `trigger` statement with `SetTriggerHandler`. The default sends the event back to the same script; many games want it to reach every script:

```cpp
m_VM.SetTriggerHandler([this](ForgeScript::Module*, const std::string& event, const std::vector<ForgeScript::Value>& values)
{
	m_VM.EmitAll(event, values);
});
```

To call a script function from C++ and get a result (for example an AI scoring function):

```cpp
ForgeScript::Value score = m_VM.GetGlobal(module, "score_target");
ForgeScript::Value result;
if (m_VM.Call(score, { Wrap(target) }, &result) && result.IsNumber())
	best = result.AsNumber();
```

Functions called this way must finish right away; they cannot `wait`.

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

- **Every mistake at once** (for an editor's problem list): `std::vector<ForgeScript::CompileError> mistakes = m_VM.FindMistakes(source, "enemy.forge");` returns all mistakes it can find, sorted by line, and includes the one `Check` reports. It also reports a `use` whose script your module loader cannot find, so set the loader first.
- **Settings for an inspector:** `ForgeScript::FindScriptVariables(source)` (`ForgeScript/Compiler/ScriptVariables.h`) lists the top-level `set name to <value>` lines whose value is a plain number, text, true/false, `vector(...)` or color list, with their line numbers, without running the script. Bellows shows these as Inspector fields and, when the entity starts, applies edited values with `m_VM.SetGlobal(module, name, value)` after `RunInModule` and before sending `created`.

```cpp
for (const ForgeScript::ScriptVariable& variable : ForgeScript::FindScriptVariables(source))
	AddField(variable.Name, variable.Kind, variable.Number, variable.Text);
```

- **Hot reload:** call `m_VM.ClearModuleCache()`, unload the entity's module, then attach the script again. (Bellows simply makes a new VM each time play starts, which reads every script again.)
- **Interactive console:** run typed lines in one module with `CompileOptions::Repl = true`, which prints the value of a line that is only an expression (see `ForgeScript/CLI/Source/Main.cpp`).
- **Repeatable random numbers** for replays and tests: `m_VM.SetRandomSeed(1234);`

---

## Step 10: Scripts for AI agents and automated builds

Everything a script can do goes through the functions, phrases and types you register, so an AI agent driving the editor can:

1. write `.forge` files,
2. call `VM::Check` to catch mistakes with exact line and column numbers,
3. run them in play mode and read the print and error handlers.

Register the same engine API for the editor's automation tools, and agents can build game logic (Tetris rules, enemy AI, UI flows) as ForgeScript scripts without touching C++.
