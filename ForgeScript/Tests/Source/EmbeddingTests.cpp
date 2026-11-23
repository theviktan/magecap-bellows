#include "TestFramework.h"

#include <cmath>

using namespace ForgeScript;

// These tests use ForgeScript the way a game engine does: C++ objects exposed to scripts,
// one module per entity, events sent from the game loop, and time moving forward.

namespace {

	struct FakeEntity
	{
		std::string Name;
		double X = 0, Y = 0, Z = 0;
		double Health = 100;
		bool Alive = true;
	};

	NativeType MakeEntityType()
	{
		NativeType type;
		type.Name = "Entity";
		type.Getters["name"] = [](VM& vm, void* data) { return vm.MakeText(static_cast<FakeEntity*>(data)->Name); };
		type.Getters["health"] = [](VM&, void* data) { return Value::FromNumber(static_cast<FakeEntity*>(data)->Health); };
		type.Getters["position"] = [](VM& vm, void* data)
		{
			FakeEntity* e = static_cast<FakeEntity*>(data);
			return vm.MakeVector(e->X, e->Y, e->Z);
		};
		type.Setters["health"] = [](VM&, void* data, const Value& value)
		{
			if (!value.IsNumber())
				throw RuntimeError("health must be a number");
			static_cast<FakeEntity*>(data)->Health = value.AsNumber();
		};
		type.Setters["position"] = [](VM&, void* data, const Value& value)
		{
			if (!IsObjectType(value, ObjectType::Vector))
				throw RuntimeError("position must be a vector");
			FakeEntity* e = static_cast<FakeEntity*>(data);
			Vector* v = As<Vector>(value);
			e->X = v->X;
			e->Y = v->Y;
			e->Z = v->Z;
		};
		type.Methods["destroy"] = { 0, [](CallContext& c)
		{
			static_cast<FakeEntity*>(As<NativeObject>(c.Self())->Data)->Alive = false;
			return Value();
		} };
		type.Methods["move_by"] = { 3, [](CallContext& c)
		{
			FakeEntity* e = static_cast<FakeEntity*>(As<NativeObject>(c.Self())->Data);
			e->X += c.Number(0);
			e->Y += c.Number(1);
			e->Z += c.Number(2);
			return c.Self();
		} };
		type.IsAlive = [](void* data) { return static_cast<FakeEntity*>(data)->Alive; };
		type.ToText = [](void* data) { return "Entity(" + static_cast<FakeEntity*>(data)->Name + ")"; };
		return type;
	}

	struct Harness
	{
		VM Machine;
		std::string Output;
		std::vector<std::string> Errors;

		explicit Harness(const VMConfig& config = VMConfig())
			: Machine(config)
		{
			Machine.SetPrintHandler([this](const std::string& text) { Output += text + "\n"; });
			Machine.SetErrorHandler([this](const ScriptError& error) { Errors.push_back(error.ToString()); });
		}
	};

}

TEST_CASE("NativeFunctionsAreCallable")
{
	Harness h;
	h.Machine.RegisterFunction("double_it", 1, [](CallContext& c) { return Value::FromNumber(c.Number(0) * 2); });
	h.Machine.RegisterFunction("join_all", AnyArity, [](CallContext& c)
	{
		std::string text;
		for (size_t i = 0; i < c.Count(); i++)
			text += c.GetVM().ToText(c.Arg(i));
		return c.GetVM().MakeText(text);
	});
	h.Machine.Load("say double_it(21)\nsay join_all(1, \"a\", true)\nsay double_it of 4\n", "t.forge");
	CHECK_EQ(h.Output, std::string("42\n1atrue\n8\n"));
	CHECK(h.Errors.empty());
}

TEST_CASE("NativeErrorsAreCatchable")
{
	Harness h;
	h.Machine.RegisterFunction("needs_number", 1, [](CallContext& c) { return Value::FromNumber(c.Number(0)); });
	h.Machine.Load("try\n\tneeds_number(\"x\")\ncatch problem\n\tsay problem\nend\n", "t.forge");
	CHECK_EQ(h.Output, std::string("needs_number needs a number for value 1, but got text.\n"));
}

TEST_CASE("NativePhrases")
{
	NativeType entityType = MakeEntityType();
	FakeEntity hero{ "Hero" };
	FakeEntity goal{ "Goal", 10, 0, 0 };
	std::string error;

	Harness h2;
	CHECK(h2.Machine.RegisterPhrase("step (thing) toward (target) by (amount)", [&entityType](CallContext& c)
	{
		FakeEntity* thing = c.NativeArg<FakeEntity>(0, entityType);
		FakeEntity* target = c.NativeArg<FakeEntity>(1, entityType);
		double amount = c.Number(2);
		double direction = target->X > thing->X ? 1.0 : -1.0;
		thing->X += direction * amount;
		return Value();
	}));
	h2.Machine.SetSharedGlobal("hero", h2.Machine.MakeNative(entityType, &hero));
	h2.Machine.SetSharedGlobal("goal", h2.Machine.MakeNative(entityType, &goal));
	h2.Machine.Load("step hero toward goal by 2\nstep hero toward goal by 1 + 1\nsay hero.position\n", "t.forge");
	CHECK_EQ(h2.Output, std::string("(4, 0, 0)\n"));
	CHECK(h2.Errors.empty());
	CHECK_EQ(hero.X, 4.0);

	CHECK(!h2.Machine.RegisterPhrase("(x) first", [](CallContext&) { return Value(); }, &error));
	CHECK(!h2.Machine.RegisterPhrase("start moving (x)", [](CallContext&) { return Value(); }, &error));
	CHECK_CONTAINS(error, "keyword");
	CHECK(!h2.Machine.RegisterPhrase("run (x) if (y)", [](CallContext&) { return Value(); }, &error));
	CHECK_CONTAINS(error, "'if'");
}

TEST_CASE("NativeTypesExposeProperties")
{
	Harness h;
	NativeType entityType = MakeEntityType();
	FakeEntity player{ "Player", 1, 2, 3 };
	Module* module = h.Machine.CreateModule("player.forge");
	h.Machine.SetGlobal(module, "self", h.Machine.MakeNative(entityType, &player));

	LoadResult result = h.Machine.RunInModule(module,
		"say my name\n"
		"say self\n"
		"take 30 from my health\n"
		"set my position.x to 50\n"
		"add 5 to my position.y\n"
		"set p to my position\n"
		"set p.z to 999\n"
		"self.move_by(1, 1, 1)\n"
		"say my position\n"
		"say self is an object\n",
		"player.forge");

	CHECK(result.Success);
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string("Player\nEntity(Player)\n(51, 8, 4)\ntrue\n"));
	CHECK_EQ(player.Health, 70.0);
	CHECK_EQ(player.X, 51.0);
	CHECK_EQ(player.Y, 8.0);
	CHECK_EQ(player.Z, 4.0);
}

TEST_CASE("NativeTypeErrors")
{
	Harness h;
	NativeType entityType = MakeEntityType();
	FakeEntity enemy{ "Enemy" };
	h.Machine.SetSharedGlobal("enemy", h.Machine.MakeNative(entityType, &enemy));
	h.Machine.Load(
		"try\n\tset enemy.name to \"x\"\ncatch p\n\tsay p\nend\n"
		"try\n\tset enemy.health to \"lots\"\ncatch p\n\tsay p\nend\n"
		"try\n\tsay enemy.speed\ncatch p\n\tsay p\nend\n"
		"enemy.destroy()\n"
		"try\n\tsay enemy.health\ncatch p\n\tsay p\nend\n",
		"t.forge");
	CHECK_EQ(h.Output, std::string(
		"'name' of a Entity can be read but not changed.\n"
		"health must be a number\n"
		"A Entity has no property called 'speed'.\n"
		"This Entity no longer exists (it was destroyed).\n"));
	CHECK(!enemy.Alive);
}

TEST_CASE("OneModulePerEntityWithEvents")
{
	Harness h;
	NativeType entityType = MakeEntityType();
	FakeEntity a{ "A" };
	FakeEntity b{ "B" };

	const char* script =
		"set speed to 2\n"
		"when update with delta\n"
		"\tadd speed * delta to my position.x\n"
		"end\n"
		"when hit by damage\n"
		"\ttake damage from my health\n"
		"\tif my health is at most 0 then\n"
		"\t\tsay \"{my name} was defeated\"\n"
		"\tend\n"
		"end\n";

	Module* moduleA = h.Machine.CreateModule("a.forge");
	Module* moduleB = h.Machine.CreateModule("b.forge");
	h.Machine.SetGlobal(moduleA, "self", h.Machine.MakeNative(entityType, &a));
	h.Machine.SetGlobal(moduleB, "self", h.Machine.MakeNative(entityType, &b));
	CHECK(h.Machine.RunInModule(moduleA, script, "enemy.forge").Success);
	CHECK(h.Machine.RunInModule(moduleB, script, "enemy.forge").Success);
	h.Machine.SetGlobal(moduleB, "speed", Value::FromNumber(10));

	for (int frame = 0; frame < 10; frame++)
		h.Machine.EmitAll("update", { Value::FromNumber(0.5) });

	CHECK_EQ(a.X, 10.0);
	CHECK_EQ(b.X, 50.0);
	CHECK(h.Machine.HasHandler(moduleA, "hit"));
	CHECK(!h.Machine.HasHandler(moduleA, "jump"));

	CHECK(h.Machine.Emit(moduleB, "hit", { Value::FromNumber(150) }));
	CHECK_EQ(h.Output, std::string("B was defeated\n"));
	CHECK_EQ(a.Health, 100.0);
	CHECK(h.Errors.empty());
}

TEST_CASE("WaitUsesGameTime")
{
	Harness h;
	h.Machine.Load(
		"when game starts\n"
		"\tsay \"go\"\n"
		"\twait 2 seconds\n"
		"\tsay \"two seconds\"\n"
		"\twait 2 frames\n"
		"\tsay \"two frames\"\n"
		"end\n",
		"t.forge");
	Module* module = h.Machine.CreateModule("unused");
	(void)module;

	// The first loaded module is the script; emit to every module.
	h.Machine.EmitAll("game starts");
	CHECK_EQ(h.Output, std::string("go\n"));
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(1));

	h.Machine.Update(1.0);
	CHECK_EQ(h.Output, std::string("go\n"));
	h.Machine.Update(1.0);
	CHECK_EQ(h.Output, std::string("go\ntwo seconds\n"));
	h.Machine.Update(0.0);
	CHECK_EQ(h.Output, std::string("go\ntwo seconds\n"));
	h.Machine.Update(0.0);
	CHECK_EQ(h.Output, std::string("go\ntwo seconds\ntwo frames\n"));
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(0));
}

TEST_CASE("PausedUpdateRunsOnlyTheModulesThatKeepRunning")
{
	Harness h;
	const char* script =
		"when game starts\n"
		"\twait 2 seconds\n"
		"\tsay \"{name} after two seconds at {time()}\"\n"
		"\twait 1 frame\n"
		"\tsay \"{name} after a frame\"\n"
		"end\n";
	Module* menu = h.Machine.CreateModule("menu.forge");
	Module* enemy = h.Machine.CreateModule("enemy.forge");
	h.Machine.SetGlobal(menu, "name", h.Machine.MakeText("menu"));
	h.Machine.SetGlobal(enemy, "name", h.Machine.MakeText("enemy"));
	CHECK(h.Machine.RunInModule(menu, script, "pause.forge").Success);
	CHECK(h.Machine.RunInModule(enemy, script, "pause.forge").Success);
	h.Machine.EmitAll("game starts");
	const auto onlyMenu = [menu](const Module* module) { return module == menu; };

	// Paused: the menu's wait counts real time, the enemy's holds, and time() stands still.
	h.Machine.Update(0.5);
	h.Machine.UpdatePaused(1.0, onlyMenu);
	CHECK(h.Output.empty());
	h.Machine.UpdatePaused(0.5, onlyMenu);
	CHECK_EQ(h.Output, std::string("menu after two seconds at 0.5\n"));
	CHECK_EQ(h.Machine.GetTime(), 0.5);
	h.Machine.UpdatePaused(5.0, [](const Module*) { return false; });
	CHECK_EQ(h.Output, std::string("menu after two seconds at 0.5\n"));
	h.Machine.UpdatePaused(0.1, onlyMenu);
	CHECK_EQ(h.Output, std::string("menu after two seconds at 0.5\nmenu after a frame\n"));

	// Unpaused: the enemy carries on from the 0.5 seconds it had already waited.
	h.Machine.Update(1.0);
	CHECK_EQ(h.Output, std::string("menu after two seconds at 0.5\nmenu after a frame\n"));
	h.Machine.Update(0.5);
	CHECK_EQ(h.Output, std::string("menu after two seconds at 0.5\nmenu after a frame\nenemy after two seconds at 2\n"));
	h.Machine.Update(0.0);
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(0));
	CHECK(h.Errors.empty());
}

TEST_CASE("WaitForEventFollowsDeliveryAndSource")
{
	Harness h;
	Module* menu = h.Machine.CreateModule("menu.forge");
	Module* other = h.Machine.CreateModule("other.forge");
	CHECK(h.Machine.RunInModule(menu,
		"start function()\n\twait for \"Start\" is clicked\n\tsay \"start clicked\"\nend()\n"
		"start function()\n\twait for clicked with widget\n\tsay \"any click: {widget}\"\nend()\n", "menu.forge").Success);
	CHECK(h.Machine.RunInModule(other, "start function()\n\twait for clicked\n\tsay \"other module\"\nend()\n", "other.forge").Success);

	// A host sees the wait, even with no 'when' for the event.
	CHECK(h.Machine.HasHandler(menu, "clicked"));
	CHECK_FALSE(h.Machine.HasHandler(menu, "submitted"));

	// An event for one module wakes only its tasks; 'wait for "Start" is clicked' needs that source.
	h.Machine.Emit(menu, "clicked", { h.Machine.MakeText("Quit") }, "Quit");
	CHECK_EQ(h.Output, std::string("any click: Quit\n"));
	h.Machine.Emit(menu, "clicked", { h.Machine.MakeText("Start") }, "start");
	CHECK_EQ(h.Output, std::string("any click: Quit\nstart clicked\n"));
	CHECK_FALSE(h.Machine.HasHandler(menu, "clicked"));
	CHECK(h.Machine.HasHandler(other, "clicked"));

	// Unloading a module ends its waits.
	h.Machine.UnloadModule(other);
	CHECK_FALSE(h.Machine.HasHandler(other, "clicked"));
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(0));
	CHECK(h.Errors.empty());
}

TEST_CASE("TimersAndEventWaitsWhilePaused")
{
	Harness h;
	const char* script =
		"every 1 second\n"
		"\tsay \"{name} tick\"\n"
		"end\n"
		"start function()\n"
		"\twait for resume pressed\n"
		"\tsay \"{name} resumed\"\n"
		"end()\n";
	Module* menu = h.Machine.CreateModule("menu.forge");
	Module* enemy = h.Machine.CreateModule("enemy.forge");
	h.Machine.SetGlobal(menu, "name", h.Machine.MakeText("menu"));
	h.Machine.SetGlobal(enemy, "name", h.Machine.MakeText("enemy"));
	CHECK(h.Machine.RunInModule(menu, script, "timers.forge").Success);
	CHECK(h.Machine.RunInModule(enemy, script, "timers.forge").Success);
	const auto onlyMenu = [menu](const Module* module) { return module == menu; };

	// Paused, only the marked script's timer counts; an event still ends an event wait, as it runs handlers.
	h.Machine.UpdatePaused(1.0, onlyMenu);
	CHECK_EQ(h.Output, std::string("menu tick\n"));
	h.Machine.EmitAll("resume pressed");
	CHECK_EQ(h.Output, std::string("menu tick\nmenu resumed\nenemy resumed\n"));
	h.Machine.UpdatePaused(5.0, [](const Module*) { return false; });
	CHECK_EQ(h.Output, std::string("menu tick\nmenu resumed\nenemy resumed\n"));
	// Unpaused, both timers go on.
	h.Output.clear();
	h.Machine.Update(1.0);
	CHECK_CONTAINS(h.Output, "menu tick\n");
	CHECK_CONTAINS(h.Output, "enemy tick\n");
	CHECK_EQ(h.Output.size(), std::string("menu tick\nenemy tick\n").size());
	CHECK(h.Errors.empty());
}

TEST_CASE("UnloadCancelsWaitingTasks")
{
	Harness h;
	LoadResult result = h.Machine.Load("start (function()\n\twait 1 second\n\tsay \"should not print\"\nend)()\n", "t.forge");
	CHECK(result.Success);
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(1));
	h.Machine.UnloadModule(result.LoadedModule);
	CHECK_EQ(h.Machine.GetTaskCount(), static_cast<size_t>(0));
	h.Machine.Update(5.0);
	h.Machine.CollectGarbage();
	CHECK(h.Output.empty());
}

TEST_CASE("HostCallsScriptFunctions")
{
	Harness h;
	LoadResult result = h.Machine.Load("function score(a, b)\n\treturn a * 10 + b\nend\nfunction slow()\n\twait 1 second\nend\n", "t.forge");
	CHECK(result.Success);

	Value fn = h.Machine.GetGlobal(result.LoadedModule, "score");
	Value out;
	CHECK(h.Machine.Call(fn, { Value::FromNumber(4), Value::FromNumber(2) }, &out));
	REQUIRE(out.IsNumber());
	CHECK_EQ(out.AsNumber(), 42.0);

	CHECK(!h.Machine.Call(fn, { Value::FromNumber(1) }, &out));
	CHECK(!h.Machine.Call(h.Machine.GetGlobal(result.LoadedModule, "slow"), {}, &out));
	CHECK_EQ(h.Errors.size(), static_cast<size_t>(2));
	if (h.Errors.size() == 2)
	{
		CHECK_CONTAINS(h.Errors[0], "needs 2 values");
		CHECK_CONTAINS(h.Errors[1], "'wait' cannot be used here");
	}
}

TEST_CASE("ScriptsShareGlobalsSetByTheHost")
{
	Harness h;
	h.Machine.SetSharedGlobal("gravity", Value::FromNumber(9.8));
	h.Machine.Load("say gravity\n", "a.forge");
	h.Machine.Load("set gravity to 1\nsay gravity\n", "b.forge");
	h.Machine.Load("say gravity\n", "c.forge");
	CHECK_EQ(h.Output, std::string("9.8\n1\n9.8\n"));
}

TEST_CASE("TriggerHandlerRoutesEvents")
{
	Harness h;
	h.Machine.SetTriggerHandler([&h](Module*, const std::string& event, const std::vector<Value>& values)
	{
		h.Machine.EmitAll(event, values);
	});
	h.Machine.Load("when door opened with who\n\tsay \"listener A heard {who}\"\nend\n", "a.forge");
	h.Machine.Load("when door opened with who\n\tsay \"listener B heard {who}\"\nend\n", "b.forge");
	h.Machine.Load("trigger door opened with \"Ron\"\n", "c.forge");
	CHECK_EQ(h.Output, std::string("listener A heard Ron\nlistener B heard Ron\n"));
}

TEST_CASE("HandlesKeepValuesAlive")
{
	Harness h;
	LoadResult result = h.Machine.Load("set data to [\"keep me\", { nested: [1, 2, 3] }]\n", "t.forge");
	Handle handle(h.Machine, h.Machine.GetGlobal(result.LoadedModule, "data"));
	h.Machine.UnloadModule(result.LoadedModule);

	for (int i = 0; i < 5; i++)
		h.Machine.CollectGarbage();
	CHECK_EQ(h.Machine.ToText(handle.Get()), std::string("[\"keep me\", {\"nested\": [1, 2, 3]}]"));

	Handle copy = handle;
	handle.Reset();
	h.Machine.CollectGarbage();
	CHECK_EQ(h.Machine.ToText(copy.Get()), std::string("[\"keep me\", {\"nested\": [1, 2, 3]}]"));
}

TEST_CASE("GarbageIsCollected")
{
	Harness h;
	h.Machine.CollectGarbage();
	size_t before = h.Machine.GetObjectCount();
	LoadResult result = h.Machine.Load(
		"set keep to []\n"
		"for i from 1 to 20000 do\n"
		"\tset junk to [i, \"text {i}\", { value: i }, vector(i, i, i)]\n"
		"\tif i % 1000 is 0 then add junk to keep end\n"
		"end\n"
		"say keep.length\n",
		"t.forge");
	CHECK(result.Success);
	CHECK_EQ(h.Output, std::string("20\n"));
	h.Machine.CollectGarbage();
	size_t after = h.Machine.GetObjectCount();
	CHECK(after < before + 1000);

	h.Machine.UnloadModule(result.LoadedModule);
	h.Machine.CollectGarbage();
	CHECK(h.Machine.GetObjectCount() <= after);
}

TEST_CASE("CallbacksSurviveGCStress")
{
	VMConfig config;
	config.InitialGCThreshold = 1;
	Harness h(config);
	h.Machine.Load(
		"set words to [\"pear\", \"fig\", \"banana\", \"kiwi\"]\n"
		"say words.map(function(w) return w.upper() + \"!\" end)\n"
		"words.sort_by(function(w) return \"key \" + w end)\n"
		"say words\n"
		"say words.filter(function(w) return w.length > 3 end).join(\"+\")\n",
		"t.forge");
	CHECK_EQ(h.Output, std::string("[\"PEAR!\", \"FIG!\", \"BANANA!\", \"KIWI!\"]\n[\"banana\", \"fig\", \"kiwi\", \"pear\"]\nbanana+kiwi+pear\n"));
	CHECK(h.Errors.empty());
}

TEST_CASE("ModuleLoaderAndCaching")
{
	Harness h;
	int loads = 0;
	h.Machine.SetModuleLoader([&loads](const std::string& path, std::string& outName, std::string& outSource)
	{
		if (path != "shared")
			return false;
		loads++;
		outName = "shared.forge";
		outSource = "set counter to 0\nfunction bump()\n\tadd 1 to counter\n\treturn counter\nend\n";
		return true;
	});
	h.Machine.Load("use \"shared\" as s\nsay s.bump()\n", "a.forge");
	h.Machine.Load("use \"shared\" as s\nsay s.bump()\n", "b.forge");
	h.Machine.Load("use \"missing\"\n", "c.forge");
	CHECK_EQ(h.Output, std::string("1\n2\n"));
	CHECK_EQ(loads, 1);
	CHECK_EQ(h.Errors.size(), static_cast<size_t>(1));
	if (!h.Errors.empty())
		CHECK_CONTAINS(h.Errors[0], "Could not find the script \"missing\"");
}

TEST_CASE("CompileErrorsAreReportedToTheHost")
{
	Harness h;
	LoadResult result = h.Machine.Load("say 1\nset x to\n", "broken.forge");
	CHECK(!result.Success);
	CHECK_CONTAINS(result.ErrorMessage, "broken.forge:2");
	CHECK_EQ(h.Errors.size(), static_cast<size_t>(1));
	CHECK(h.Output.empty());
}

TEST_CASE("RandomSeedIsRepeatable")
{
	Harness a;
	Harness b;
	a.Machine.SetRandomSeed(7);
	b.Machine.SetRandomSeed(7);
	const char* script = "set rolls to []\nrepeat 10 times\n\tadd random(1, 100) to rolls\nend\nsay rolls\n";
	a.Machine.Load(script, "t.forge");
	b.Machine.Load(script, "t.forge");
	CHECK_EQ(a.Output, b.Output);
}

TEST_CASE("NativeObjectsCanCompareEqual")
{
	// Two separate wrappers of one C++ object: equal (and usable as one map key) only with Equals/Hash.
	NativeType idType;
	idType.Name = "Id";
	idType.Equals = [](void* a, void* b) { return *static_cast<int*>(a) == *static_cast<int*>(b); };
	idType.Hash = [](void* data) { return std::hash<int>()(*static_cast<int*>(data)); };
	NativeType plainType;
	plainType.Name = "Plain";

	int first = 7;
	int same = 7;
	int other = 8;
	Harness h;
	h.Machine.SetSharedGlobal("a", h.Machine.MakeNative(idType, &first));
	h.Machine.SetSharedGlobal("b", h.Machine.MakeNative(idType, &same));
	h.Machine.SetSharedGlobal("c", h.Machine.MakeNative(idType, &other));
	h.Machine.SetSharedGlobal("p", h.Machine.MakeNative(plainType, &first));
	h.Machine.SetSharedGlobal("q", h.Machine.MakeNative(plainType, &first));
	h.Machine.Load(
		"say a is b\n"
		"say a is c\n"
		"say p is q\n"
		"set seen to {}\n"
		"set seen[a] to \"first\"\n"
		"say seen[b]\n"
		"say [a] contains b\n",
		"t.forge");
	CHECK_EQ(h.Output, std::string("true\nfalse\nfalse\nfirst\ntrue\n"));
	CHECK(h.Errors.empty());
}

TEST_CASE("NativeFunctionsCanHaveMembers")
{
	VMConfig config;
	config.InitialGCThreshold = 1;
	Harness h(config);
	Value make = h.Machine.MakeFunction("Point", 1, [](CallContext& c) { return Value::FromNumber(c.Number(0) * 10); });
	CHECK(h.Machine.SetFunctionMember(make, "origin", h.Machine.MakeFunction("origin", 0, [](CallContext&) { return Value::FromNumber(0); })));
	CHECK(h.Machine.SetFunctionMember(make, "unit", Value::FromNumber(1)));
	CHECK(!h.Machine.SetFunctionMember(Value::FromNumber(3), "x", Value()));
	h.Machine.SetSharedGlobal("Point", make);
	h.Machine.CollectGarbage();
	h.Machine.Load(
		"say Point(2)\n"
		"say Point.origin()\n"
		"say Point.unit\n"
		"set f to Point.origin\n"
		"say f()\n"
		"try\n\tsay Point.missing\ncatch p\n\tsay p\nend\n",
		"t.forge");
	CHECK_EQ(h.Output, std::string("20\n0\n1\n0\n'Point' has nothing called 'missing'.\n"));
	CHECK(h.Errors.empty());
}

TEST_CASE("GetGlobalNamesListsAModulesNamesInOrder")
{
	Harness h;
	Module* module = h.Machine.CreateModule("names.forge");
	h.Machine.RunInModule(module, "set speed to 2\nfunction jump()\nend\nset title to \"x\"\n", "names.forge");
	const std::vector<std::string> names = h.Machine.GetGlobalNames(module);
	CHECK(std::find(names.begin(), names.end(), "speed") != names.end());
	CHECK(std::find(names.begin(), names.end(), "jump") != names.end());
	CHECK(std::find(names.begin(), names.end(), "title") != names.end());
	CHECK(std::find(names.begin(), names.end(), "speed") < std::find(names.begin(), names.end(), "title"));
}

TEST_CASE("CarryOverVariablesKeepsStateButNotCode")
{
	Harness h;
	Module* before = h.Machine.CreateModule("v1.forge");
	h.Machine.RunInModule(before,
		"set score to 0\nset name to \"old\"\nset items to [1]\nset mode to \"slow\"\nconstant LIMIT is 3\n"
		"function describe()\n\treturn \"old code\"\nend\n"
		"add 5 to score\nadd 2 to items\n", "v1.forge");

	Module* after = h.Machine.CreateModule("v2.forge");
	h.Machine.RunInModule(after,
		"set score to 0\nset name to \"new\"\nset items to []\nset mode to 3\nconstant LIMIT is 9\nset added to true\n"
		"function describe()\n\treturn \"new code\"\nend\n", "v2.forge");

	std::vector<std::string> carried = h.Machine.CarryOverVariables(before, after, { "name" });
	std::sort(carried.begin(), carried.end());
	CHECK_EQ(carried, std::vector<std::string>{ "items", "score" });

	h.Machine.RunInModule(after, "say score\nsay name\nsay items\nsay mode\nsay LIMIT\nsay added\nsay describe()\n", "v2.forge");
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string("5\nnew\n[1, 2]\n3\n9\ntrue\nnew code\n"));
}

TEST_CASE("WhenWithANameRunsOnlyForThatSource")
{
	Harness h;
	Module* menu = h.Machine.CreateModule("menu.forge");
	const char* script =
		"when \"Start\" is clicked\n"
		"\tsay \"start\"\n"
		"end\n"
		"when \"Volume\" is changed with value\n"
		"\tsay \"volume {value}\"\n"
		"end\n"
		"when clicked with widget\n"
		"\tsay \"any {widget}\"\n"
		"end\n"
		"when \"door opened\"\n"
		"\tsay \"door\"\n"
		"end\n";
	REQUIRE(h.Machine.RunInModule(menu, script, "menu.forge").Success);

	// The widget comes last in a UI event's values.
	CHECK(h.Machine.Emit(menu, "clicked", { h.Machine.MakeText("Start") }, "Start"));
	CHECK_EQ(h.Output, std::string("start\nany Start\n"));

	h.Output.clear();
	CHECK(h.Machine.Emit(menu, "clicked", { h.Machine.MakeText("Quit") }, "Quit"));
	CHECK_EQ(h.Output, std::string("any Quit\n"));

	// Names match ignoring letter case.
	h.Output.clear();
	CHECK(h.Machine.Emit(menu, "changed", { Value::FromNumber(0.5), h.Machine.MakeText("volume") }, "volume"));
	CHECK_EQ(h.Output, std::string("volume 0.5\n"));

	// An event without a source, like a script's 'trigger clicked', never runs a named handler.
	h.Output.clear();
	CHECK(h.Machine.Emit(menu, "clicked"));
	CHECK_EQ(h.Output, std::string("any nothing\n"));

	// A text event name without 'is' after it is still an event name.
	h.Output.clear();
	CHECK(h.Machine.Emit(menu, "door opened"));
	CHECK_EQ(h.Output, std::string("door\n"));
	CHECK(h.Errors.empty());
}

TEST_CASE("PhrasesThatYieldToCalls")
{
	// 'show' is a ForgeScript function. A host phrase starting with it, registered to yield, is only the
	// phrase where 'show' can't be a value: "show menu". Calls, 'of' and operators keep their old meaning.
	Harness h;
	CHECK(h.Machine.RegisterPhrase("show (thing)", [](CallContext& c) { return c.GetVM().MakeText("shown " + c.GetVM().ToText(c.Arg(0))); }, nullptr, true));
	CHECK(h.Machine.PhraseYieldsToCalls("show"));
	h.Machine.Load(
		"show(\"call\")\n"
		"show (\"space\")\n"
		"show of \"of\"\n"
		"set f to show\n"
		"f(\"value\")\n"
		"say show \"menu\"\n"
		"say show 5 + 1\n", "t.forge");
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string("\"call\"\n\"space\"\n\"of\"\n\"value\"\nshown menu\nshown 6\n"));

	// Without the flag, as before, a phrase takes its first word followed by brackets too.
	Harness plain;
	CHECK(plain.Machine.RegisterPhrase("greet (name)", [](CallContext& c) { return c.GetVM().MakeText("hi " + c.Text(0)); }));
	CHECK_FALSE(plain.Machine.PhraseYieldsToCalls("greet"));
	plain.Machine.Load("say greet(\"Ron\")\n", "t.forge");
	CHECK_EQ(plain.Output, std::string("hi Ron\n"));
}

TEST_CASE("APhraseIsUsableBesideALocalWithItsFirstWord")
{
	// Bellows 0.6.0's FeatureTest had 'set move to tween(...)' and then 'move box to ...' in the same
	// function, which failed with "Expected the end of the line, but found 'box'." At the start of a
	// statement, 'move box' can't mean the variable, so it is the phrase; everywhere else the local wins.
	Harness h;
	std::string moved;
	CHECK(h.Machine.RegisterPhrase("move (thing) to (place)", [&moved](CallContext& c)
	{
		moved += c.GetVM().ToText(c.Arg(0)) + " to " + c.GetVM().ToText(c.Arg(1)) + ";";
		return Value();
	}, nullptr, true));
	h.Machine.Load(
		"function run()\n"
		"\tset move to 3\n"
		"\tmove \"box\" to 5\n"       // the statement this test is about: the phrase
		"\tsay move\n"               // the local
		"\tsay move + 1\n"           // the local
		"\trepeat move times\n"      // the local
		"\t\tsay \"r\"\n"
		"\tend\n"
		"\tset move to [7]\n"
		"\tsay move[1]\n"            // the local
		"end\n"
		"run()\n", "t.forge");
	for (const std::string& error : h.Errors)
		MESSAGE(error);
	CHECK(h.Errors.empty());
	CHECK_EQ(moved, std::string("box to 5;"));
	CHECK_EQ(h.Output, std::string("3\n4\nr\nr\nr\n7\n"));

	// In a value the local still wins, as before: 'set t to move "box" to 5' is a mistake.
	VM vm;
	vm.RegisterPhrase("move (thing) to (place)", [](CallContext&) { return Value(); }, nullptr, true);
	CHECK_FALSE(vm.FindMistakes("function run()\n\tset move to 3\n\tset t to move \"box\" to 5\nend\n", "t.forge").empty());
}

TEST_CASE("WarningsForBracketsThatHideAPhrase")
{
	auto warnings = [](const std::string& source)
	{
		VM vm;
		vm.RegisterPhrase("show (thing)", [](CallContext&) { return Value(); }, nullptr, true);
		vm.RegisterPhrase("hide (thing)", [](CallContext&) { return Value(); }, nullptr, true);
		return vm.FindWarnings(source, "menu.forge");
	};

	// "show (pause_menu)" calls the show function; the warning says how to write the phrase.
	std::vector<CompileError> found = warnings("set pause_menu to 1\nshow (pause_menu)\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 2);
	CHECK(found[0].Column == 1);
	CHECK_CONTAINS(found[0].Message, "With brackets, 'show (...)' is a call to a function named 'show', not the 'show' phrase.");
	CHECK_CONTAINS(found[0].Message, "like: show pause_menu");
	CHECK_CONTAINS(warnings("show (menus[1])\n")[0].Message, "like: show the_widget");

	// No space means the call was meant; the phrase itself and other uses are fine.
	CHECK(warnings("show(\"hello\")\nshow \"menu\"\nsay show of \"x\"\n").empty());
	// A script that makes its own 'hide' function means to call it.
	CHECK(warnings("function hide(x)\n\treturn x\nend\nhide (5)\n").empty());
}

TEST_CASE("VMCountsInstructionsWhenAsked")
{
	const char* source = "set total to 0\nfor i from 1 to 100 do\n\tadd i to total\nend\n";
	VM quiet;
	REQUIRE(quiet.Load(source, "count.forge").Success);
	CHECK_EQ(quiet.GetInstructionsRun(), static_cast<uint64_t>(0));

	VMConfig config;
	config.CountInstructions = true;
	VM first(config);
	VM second(config);
	REQUIRE(first.Load(source, "count.forge").Success);
	REQUIRE(second.Load(source, "count.forge").Success);
	CHECK(first.GetInstructionsRun() > 300);
	CHECK_EQ(first.GetInstructionsRun(), second.GetInstructionsRun());
	// A run that ends in an error still counts what it did.
	VM failing(config);
	failing.SetErrorHandler([](const ScriptError&) {});
	failing.Load("set x to 1\nfail \"stop\"\n", "fail.forge");
	CHECK(failing.GetInstructionsRun() > 0);
}

TEST_CASE("VMProfilesWhereInstructionsGo")
{
	VM vm;
	vm.SetPrintHandler([](const std::string&) {});
	vm.StartProfile();
	REQUIRE(vm.Load("function fib(n)\n\tif n < 2 then return n end\n\treturn fib(n - 1) + fib(n - 2)\nend\nsay fib(10)\n", "fib.forge").Success);
	const std::vector<ProfileEntry> profile = vm.TakeProfile();
	REQUIRE_EQ(profile.size(), static_cast<size_t>(2));
	// Most instructions first: fib, called 177 times for fib(10), then the top of the script.
	CHECK_EQ(profile[0].Function, std::string("fib"));
	CHECK_EQ(profile[0].File, std::string("fib.forge"));
	CHECK_EQ(profile[0].Line, 2);
	CHECK_EQ(profile[0].Calls, static_cast<uint64_t>(177));
	CHECK(profile[0].Instructions > profile[1].Instructions);
	CHECK_EQ(profile[1].Function, std::string());
	// TakeProfile stops profiling: what runs afterwards isn't counted.
	REQUIRE(vm.Load("set x to 1 + 2\n", "after.forge").Success);
	CHECK(vm.TakeProfile().empty());
}

TEST_CASE("TryEaseMatchesTheScriptsEase")
{
	// Hosts (Bellows' tweens) share ease()'s curves through Core/Easing.h; both must agree exactly.
	const std::vector<std::string_view>& names = EaseStyleNames();
	REQUIRE_EQ(names.size(), size_t(11));
	CHECK_EQ(std::string(names.front()), std::string("linear"));
	CHECK_EQ(std::string(names.back()), std::string("elastic out"));

	Harness h;
	LoadResult result = h.Machine.Load("function eased(t, style)\n\treturn ease(t, style)\nend\n", "t.forge");
	REQUIRE(result.Success);
	Value fn = h.Machine.GetGlobal(result.LoadedModule, "eased");
	for (std::string_view name : names)
	{
		for (double t : { 0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0 })
		{
			double hosted = -1;
			REQUIRE(TryEase(name, t, hosted));
			Value out;
			REQUIRE(h.Machine.Call(fn, { Value::FromNumber(t), h.Machine.MakeText(std::string(name)) }, &out));
			CHECK_EQ(out.AsNumber(), hosted);
		}
		double atStart = -1, atEnd = -1;
		REQUIRE(TryEase(name, 0, atStart));
		REQUIRE(TryEase(name, 1, atEnd));
		CHECK(std::abs(atStart) < 1e-12);
		CHECK(std::abs(atEnd - 1) < 1e-12);
	}

	double value = -1;
	CHECK(TryEase("  In Out ", 0.5, value));
	CHECK_EQ(value, 0.5);
	CHECK(TryEase("out", 7, value));
	CHECK_EQ(value, 1.0);
	CHECK(TryEase("out", -3, value));
	CHECK_EQ(value, 0.0);
	CHECK(TryEase("linear", std::nan(""), value));
	CHECK_EQ(value, 0.0);
	value = 42;
	CHECK_FALSE(TryEase("wobble", 0.5, value));
	CHECK_EQ(value, 42.0);
}

TEST_CASE("ValueStackActsLikeAVector")
{
	ValueStack stack;
	CHECK(stack.empty());
	stack.push_back(Value::FromNumber(1));
	// Pushing a value that lives in the stack itself, while the stack has to grow, keeps the value.
	for (int i = 0; i < 100; i++)
		stack.push_back(stack.back());
	CHECK_EQ(stack.size(), size_t(101));
	CHECK_EQ(stack[100].AsNumber(), 1.0);

	stack.resize(103);
	CHECK(stack[101].IsNothing());
	CHECK(stack[102].IsNothing());
	stack.resize(2);
	stack.pop_back();
	CHECK_EQ(stack.size(), size_t(1));

	const std::vector<Value> more { Value::FromBool(true), Value::FromNumber(7) };
	stack.insert(stack.end(), more.begin(), more.end());
	CHECK_EQ(stack.size(), size_t(3));
	CHECK(stack[1].AsBool());
	CHECK_EQ(stack.back().AsNumber(), 7.0);

	ValueStack copy;
	copy.assign(stack.begin() + 1, stack.end());
	CHECK_EQ(copy.size(), size_t(2));
	CHECK_EQ(copy[1].AsNumber(), 7.0);
	stack.clear();
	CHECK(stack.empty());
}

TEST_CASE("WholeNumbersPrintAsBefore")
{
	CHECK_EQ(FormatNumber(0), std::string("0"));
	CHECK_EQ(FormatNumber(-0.0), std::string("0"));
	CHECK_EQ(FormatNumber(42), std::string("42"));
	CHECK_EQ(FormatNumber(-17), std::string("-17"));
	CHECK_EQ(FormatNumber(999999999999999.0), std::string("999999999999999"));
	CHECK_EQ(FormatNumber(-999999999999999.0), std::string("-999999999999999"));
	CHECK_EQ(FormatNumber(1e15), std::string("1e+15"));
	CHECK_EQ(FormatNumber(2.5), std::string("2.5"));
}

TEST_CASE("TextIsStillInternedAndCollected")
{
	// Equal texts are one String, however they were made, and texts nothing uses are forgotten.
	VM vm;
	const Value a = vm.MakeText("hello");
	const Value b = vm.MakeText(std::string("hel") + "lo");
	CHECK_EQ(a.AsObject(), b.AsObject());
	CHECK_NE(vm.MakeText("hello!").AsObject(), a.AsObject());
	CHECK_EQ(As<String>(a)->Hash, std::hash<std::string>{}("hello"));

	// Many texts at once: the table grows, and every one is still found.
	std::vector<Object*> made;
	for (int i = 0; i < 5000; i++)
		made.push_back(vm.MakeText("t" + std::to_string(i)).AsObject());
	for (int i = 0; i < 5000; i += 499)
		CHECK_EQ(vm.MakeText("t" + std::to_string(i)).AsObject(), made[static_cast<size_t>(i)]);

	// A script that builds and drops many texts, with a collection on every instruction, still sees
	// its kept texts as equal.
	ForgeTest::RunOptions options;
	options.StressGC = true;
	const ForgeTest::ScriptRun run = ForgeTest::RunScript(
		"set kept to \"k{1}\"\nfor i from 1 to 300 do\n\tset dropped to \"d{i}\"\nend\nsay kept is \"k1\"\nsay \"d{300}\" is dropped\n", options);
	CHECK_EQ(run.Output, std::string("true\ntrue\n"));
	CHECK(run.Errors.empty());
}

TEST_CASE("NavigationPhrasesAndEventWorkAsBellowsDefinesThem")
{
	// Bellows' navigation (0.6.0) registers these three phrases, giving way to calls, and sends
	// 'destination reached' with the agent's entity and its name as the source. This pins down the
	// ForgeScript side: the words are free, the values arrive in order, and every handler form works.
	NativeType entityType = MakeEntityType();
	FakeEntity hero{ "Hero" };
	FakeEntity goal{ "Goal", 10, 0, 0 };
	std::vector<std::string> calls;

	Harness h;
	REQUIRE(h.Machine.RegisterPhrase("walk (entity) to (position)", [&](CallContext& c)
	{
		calls.push_back("walk " + c.NativeArg<FakeEntity>(0, entityType)->Name + " to " + c.GetVM().ToText(c.Arg(1)));
		return Value();
	}, nullptr, true));
	REQUIRE(h.Machine.RegisterPhrase("the path from (start) to (end)", [&](CallContext& c)
	{
		calls.push_back("path " + c.GetVM().ToText(c.Arg(0)) + " to " + c.GetVM().ToText(c.Arg(1)));
		return c.GetVM().MakeList({ c.Arg(0), c.Arg(1) });
	}, nullptr, true));
	REQUIRE(h.Machine.RegisterPhrase("halt (entity)", [&](CallContext& c)
	{
		calls.push_back("halt " + c.NativeArg<FakeEntity>(0, entityType)->Name);
		return Value();
	}, nullptr, true));
	h.Machine.SetSharedGlobal("hero", h.Machine.MakeNative(entityType, &hero));
	h.Machine.SetSharedGlobal("goal", h.Machine.MakeNative(entityType, &goal));

	const LoadResult loaded = h.Machine.Load(
		"walk hero to goal.position + vector(0, 0, 2)\n"
		"for each corner in the path from hero.position to goal.position do\n"
		"\tsay corner\n"
		"end\n"
		"say (the path from 1 to 2).length\n"
		"halt hero\n"
		"when destination reached with who\n"
		"\tsay \"arrived: {who.name}\"\n"
		"end\n"
		"when \"Hero\" is destination reached\n"
		"\tsay \"the hero arrived\"\n"
		"end\n"
		"function patrol()\n"
		"\twait for destination reached with who\n"
		"\tsay \"patrol saw {who.name}\"\n"
		"end\n"
		"start patrol()\n", "nav.forge");
	REQUIRE(loaded.LoadedModule != nullptr);
	CHECK(h.Errors.empty());
	CHECK(h.Machine.HasHandler(loaded.LoadedModule, "destination reached"));
	CHECK(h.Machine.Emit(loaded.LoadedModule, "destination reached", { h.Machine.MakeNative(entityType, &hero) }, "Hero"));
	h.Machine.Update(0);

	REQUIRE_EQ(calls.size(), static_cast<size_t>(4));
	CHECK_EQ(calls[0], std::string("walk Hero to (10, 0, 2)"));
	CHECK_EQ(calls[1], std::string("path (0, 0, 0) to (10, 0, 0)"));
	CHECK_EQ(calls[2], std::string("path 1 to 2"));
	CHECK_EQ(calls[3], std::string("halt Hero"));
	CHECK_EQ(h.Output, std::string("(0, 0, 0)\n(10, 0, 0)\n2\narrived: Hero\nthe hero arrived\npatrol saw Hero\n"));
	CHECK(h.Errors.empty());

	// A script's own 'walk' or 'path' still means the script's own, as before.
	Harness own;
	REQUIRE(own.Machine.RegisterPhrase("walk (entity) to (position)", [](CallContext&) { return Value(); }, nullptr, true));
	own.Machine.Load("function walk(x)\n\treturn x * 2\nend\nset path to 3\nsay walk(path)\n", "own.forge");
	CHECK_EQ(own.Output, std::string("6\n"));
	CHECK(own.Errors.empty());
}

TEST_CASE("WatchedScriptVariablesReportTheirChanges")
{
	// For Bellows' 'field changed' event (0.6.0): every way a script variable can change is reported
	// once, after the new value is in, and only when the value really changed.
	Harness h;
	std::vector<std::string> changes;
	h.Machine.SetScriptVariableChangedHandler([&](Module*, const std::string& name, const Value& now, const Value& before)
	{
		changes.push_back(name + ": " + h.Machine.ToText(before) + " -> " + h.Machine.ToText(now));
	});
	const LoadResult loaded = h.Machine.Load(
		"set health to 100\nset speed to 2\nset items to []\n"
		"function hurt(amount)\n\ttake amount from health\nend\n"
		"when healed\n\tadd 5 to health\nend\n", "player.forge");
	REQUIRE(loaded.LoadedModule != nullptr);
	Module* player = loaded.LoadedModule;
	CHECK(changes.empty()); // nothing was watched yet, so the starting values aren't reported

	h.Machine.WatchScriptVariables(player, { "health", "items" });
	CHECK(h.Machine.Call(h.Machine.GetGlobal(player, "hurt"), { Value::FromNumber(10) }));
	h.Machine.Emit(player, "healed");
	h.Machine.SetGlobal(player, "health", Value::FromNumber(95)); // the same value: nothing
	h.Machine.SetGlobal(player, "health", Value::FromNumber(50)); // the host, like the Inspector
	h.Machine.SetGlobal(player, "speed", Value::FromNumber(9));   // not watched
	h.Machine.SetSharedGlobal("p", Value::FromObject(player)); // another script, as Bellows hands them over
	h.Machine.Load("set p.health to 1\np.items.add(3)\n", "other.forge");
	h.Machine.Load("set health to 7\n", "unrelated.forge");       // another script's own variable

	CHECK_EQ(changes, std::vector<std::string>{ "health: 100 -> 90", "health: 90 -> 95", "health: 95 -> 50", "health: 50 -> 1" });
	CHECK(h.Errors.empty());

	h.Machine.WatchScriptVariables(player, {});
	h.Machine.SetGlobal(player, "health", Value::FromNumber(2));
	CHECK_EQ(changes.size(), static_cast<size_t>(4));
}
