#include "TestFramework.h"

#include <cmath>

using namespace Forge;

// These tests use Forge the way a game engine does: C++ objects exposed to scripts,
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

FORGE_TEST(NativeFunctionsAreCallable)
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

FORGE_TEST(NativeErrorsAreCatchable)
{
	Harness h;
	h.Machine.RegisterFunction("needs_number", 1, [](CallContext& c) { return Value::FromNumber(c.Number(0)); });
	h.Machine.Load("try\n\tneeds_number(\"x\")\ncatch problem\n\tsay problem\nend\n", "t.forge");
	CHECK_EQ(h.Output, std::string("needs_number needs a number for value 1, but got text.\n"));
}

FORGE_TEST(NativePhrases)
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

FORGE_TEST(NativeTypesExposeProperties)
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

FORGE_TEST(NativeTypeErrors)
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

FORGE_TEST(OneModulePerEntityWithEvents)
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

FORGE_TEST(WaitUsesGameTime)
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

FORGE_TEST(UnloadCancelsWaitingTasks)
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

FORGE_TEST(HostCallsScriptFunctions)
{
	Harness h;
	LoadResult result = h.Machine.Load("function score(a, b)\n\treturn a * 10 + b\nend\nfunction slow()\n\twait 1 second\nend\n", "t.forge");
	CHECK(result.Success);

	Value fn = h.Machine.GetGlobal(result.LoadedModule, "score");
	Value out;
	CHECK(h.Machine.Call(fn, { Value::FromNumber(4), Value::FromNumber(2) }, &out));
	CHECK(out.IsNumber() && out.AsNumber() == 42.0);

	CHECK(!h.Machine.Call(fn, { Value::FromNumber(1) }, &out));
	CHECK(!h.Machine.Call(h.Machine.GetGlobal(result.LoadedModule, "slow"), {}, &out));
	CHECK_EQ(h.Errors.size(), static_cast<size_t>(2));
	if (h.Errors.size() == 2)
	{
		CHECK_CONTAINS(h.Errors[0], "needs 2 values");
		CHECK_CONTAINS(h.Errors[1], "'wait' cannot be used here");
	}
}

FORGE_TEST(ScriptsShareGlobalsSetByTheHost)
{
	Harness h;
	h.Machine.SetSharedGlobal("gravity", Value::FromNumber(9.8));
	h.Machine.Load("say gravity\n", "a.forge");
	h.Machine.Load("set gravity to 1\nsay gravity\n", "b.forge");
	h.Machine.Load("say gravity\n", "c.forge");
	CHECK_EQ(h.Output, std::string("9.8\n1\n9.8\n"));
}

FORGE_TEST(TriggerHandlerRoutesEvents)
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

FORGE_TEST(HandlesKeepValuesAlive)
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

FORGE_TEST(GarbageIsCollected)
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

FORGE_TEST(CallbacksSurviveGCStress)
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

FORGE_TEST(ModuleLoaderAndCaching)
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

FORGE_TEST(CompileErrorsAreReportedToTheHost)
{
	Harness h;
	LoadResult result = h.Machine.Load("say 1\nset x to\n", "broken.forge");
	CHECK(!result.Success);
	CHECK_CONTAINS(result.ErrorMessage, "broken.forge:2");
	CHECK_EQ(h.Errors.size(), static_cast<size_t>(1));
	CHECK(h.Output.empty());
}

FORGE_TEST(RandomSeedIsRepeatable)
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
