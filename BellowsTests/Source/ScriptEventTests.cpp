#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptApi.h"

#include <string>
#include <vector>

using namespace Bellows;
using namespace Bellows::Scripting;

namespace {

	// Removes a listener when the test ends, even when a check fails.
	struct ListenerScope
	{
		ScriptEventListenerID ID;
		explicit ListenerScope(ScriptEventListener listener) : ID(AddScriptEventListener(std::move(listener))) {}
		~ListenerScope() { RemoveScriptEventListener(ID); }
	};

	struct ReceivedEvent
	{
		std::string Name;
		ScriptValueList Values;
	};

}

TEST_CASE("The script event hub delivers to every listener in order, except the sender")
{
	std::vector<std::string> calls;
	ListenerScope first([&calls](const std::string& name, const ScriptValueList&) { calls.push_back("first " + name); });
	ListenerScope second([&calls](const std::string& name, const ScriptValueList&) { calls.push_back("second " + name); });

	EmitScriptEvent("door opened", { ScriptValue(true) });
	EmitScriptEvent("door closed", {}, first.ID);
	CHECK_EQ(calls, std::vector<std::string>{ "first door opened", "second door opened", "second door closed" });
}

TEST_CASE("Script event listeners may remove themselves and others while an event runs")
{
	std::vector<std::string> calls;
	ScriptEventListenerID later = 0;
	ScriptEventListenerID self = 0;
	self = AddScriptEventListener([&](const std::string&, const ScriptValueList&)
	{
		calls.push_back("self");
		RemoveScriptEventListener(self);
		RemoveScriptEventListener(later);
	});
	later = AddScriptEventListener([&calls](const std::string&, const ScriptValueList&) { calls.push_back("later"); });

	EmitScriptEvent("ping", {});
	EmitScriptEvent("ping", {});
	CHECK_EQ(calls, std::vector<std::string>{ "self" });
}

TEST_CASE("Script events nested too deep stop with an error")
{
	int depth = 0;
	ListenerScope echo([&depth](const std::string& name, const ScriptValueList&)
	{
		depth++;
		EmitScriptEvent(name, {});
	});
	CHECK_THROWS_WITH_AS(EmitScriptEvent("echo", {}), doctest::Contains("sent from inside 64 other events"), ScriptValueError);
	CHECK_EQ(depth, 64);

	// The depth count is back to zero afterwards, so later events work.
	int calls = 0;
	ListenerScope counter([&calls](const std::string&, const ScriptValueList&) { calls++; });
	RemoveScriptEventListener(echo.ID);
	EmitScriptEvent("after", {});
	CHECK_EQ(calls, 1);
}

TEST_CASE("Script events that multiply stop after 10,000")
{
	// Two listeners that each answer by sending the event again: the work doubles at every level.
	int sent = 0;
	auto answer = [&sent](const std::string& name, const ScriptValueList&)
	{
		sent++;
		try
		{
			EmitScriptEvent(name, {});
		}
		catch (const ScriptValueError&)
		{
		}
	};
	ListenerScope first(answer);
	ListenerScope second(answer);
	CHECK_NOTHROW(EmitScriptEvent("storm", {}));
	CHECK(sent <= 2 * 10000);

	// The count starts again with the next outer event.
	int calls = 0;
	RemoveScriptEventListener(first.ID);
	RemoveScriptEventListener(second.ID);
	ListenerScope counter([&calls](const std::string&, const ScriptValueList&) { calls++; });
	EmitScriptEvent("calm", {});
	CHECK_EQ(calls, 1);
}

TEST_CASE("Script event values convert to and from ForgeScript")
{
	Ref<Scene> scene = CreateRef<Scene>();
	SetContextScene(scene.get());
	Entity crate = scene->CreateEntity("Crate");
	ForgeScript::VM vm;
	RegisterForgeScriptApi(vm);

	const ScriptValue original = ScriptValueList {
		ScriptValue(), ScriptValue(true), ScriptValue(2.5), ScriptValue(std::string("text")), ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)),
		ScriptValue(ScriptEntity(crate)),
		ScriptValue(ScriptValueMap { { "b", ScriptValue(1.0) }, { "a", ScriptValue(ScriptValueList { ScriptValue(false) }) } }) };

	const ForgeScript::Value inForgeScript = FromScriptValue(vm, original);
	std::vector<std::string> unsendable;
	CHECK(ToScriptValue(vm, inForgeScript, unsendable) == original);
	CHECK(unsendable.empty());

	// Values other languages cannot read arrive as nothing, and the sender learns what they were.
	ForgeScript::Module* module = vm.CreateModule("values.forge");
	vm.RunInModule(module,
		"set loop to [1]\nadd loop to loop\n"
		"set odd to [function() return 1 end, { 5: \"five\", name: \"kept\" }, loop]\n", "values.forge");
	const ScriptValue sent = ToScriptValue(vm, vm.GetGlobal(module, "odd"), unsendable);
	REQUIRE(std::holds_alternative<ScriptValueList>(sent.Data));
	const ScriptValueList& items = std::get<ScriptValueList>(sent.Data);
	CHECK(items[0].IsNothing());
	CHECK(items[1] == ScriptValue(ScriptValueMap { { "name", ScriptValue(std::string("kept")) } }));
	REQUIRE_EQ(unsendable.size(), static_cast<size_t>(3));
	CHECK(unsendable[0].find("function") != std::string::npos);
	CHECK(unsendable[1].find("not text") != std::string::npos);
	CHECK(unsendable[2].find("nested more than 32 deep") != std::string::npos);
	SetContextScene(nullptr);
}

TEST_CASE("ForgeScript events reach other languages and their events reach ForgeScript")
{
	Ref<Scene> scene = CreateRef<Scene>();
	SetContextScene(scene.get());
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "ScriptEvents";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts/Door.forge",
		"set opened_by to nothing\n"
		"when created\n"
		"\ttrigger door opened with self, [1, \"two\"], { color: \"red\" }\n"
		"end\n"
		"when door opened with door, numbers, details\n"
		"\tset my name to \"heard {details.color}\"\n"
		"end\n"
		"when python says with who, list, map\n"
		"\tset my name to \"{who.name} {list[2]} {map.mood}\"\n"
		"end\n"
		"when ping pong\n"
		"\ttry\n\t\ttrigger ping pong\n\tcatch problem\n\t\tset my name to \"stopped: {problem}\"\n\tend\n"
		"end\n"));
	AssetManager::SetAssetDirectory(assets);

	std::vector<std::string> errors;
	ForgeScriptRuntime runtime([&errors](const std::string&, const std::string& message) { errors.push_back(message); });
	std::vector<ReceivedEvent> received;
	// Stands in for the Python side, which MCBellows connects to the hub the same way.
	ListenerScope python([&received](const std::string& name, const ScriptValueList& values) { received.push_back({ name, values }); });

	Entity door = scene->CreateEntity("Door");
	door.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Door.forge";
	runtime.Start();
	REQUIRE(runtime.CreateInstance(door));
	runtime.CallCreated(door.GetUUID());

	// ForgeScript's own handler ran once, and the other language got the same values, converted.
	CHECK_EQ(door.GetName(), std::string("heard red"));
	REQUIRE_EQ(received.size(), static_cast<size_t>(1));
	CHECK_EQ(received[0].Name, std::string("door opened"));
	REQUIRE_EQ(received[0].Values.size(), static_cast<size_t>(3));
	CHECK(received[0].Values[0] == ScriptValue(ScriptEntity(door)));
	CHECK(received[0].Values[1] == ScriptValue(ScriptValueList { ScriptValue(1.0), ScriptValue(std::string("two")) }));
	CHECK(received[0].Values[2] == ScriptValue(ScriptValueMap { { "color", ScriptValue(std::string("red")) } }));

	// An event from the other language reaches ForgeScript's 'when' with ForgeScript values.
	EmitScriptEvent("python says", { ScriptValue(ScriptEntity(door)), ScriptValue(ScriptValueList { ScriptValue(1.0), ScriptValue(2.0) }),
		ScriptValue(ScriptValueMap { { "mood", ScriptValue(std::string("happy")) } }) }, python.ID);
	CHECK_EQ(door.GetName(), std::string("heard red 2 happy"));

	// Two languages answering each other forever stop with an error the script can catch: every round
	// doubles the events, so the limit on events per outer event is what ends it.
	// Like a real language's listener, it reports its scripts' errors instead of letting them escape.
	ListenerScope echo([&python](const std::string& name, const ScriptValueList&)
	{
		if (name != "ping pong")
			return;
		try
		{
			EmitScriptEvent(name, {}, python.ID);
		}
		catch (const ScriptValueError&)
		{
		}
	});
	EmitScriptEvent("ping pong", {}, python.ID);
	INFO(door.GetName());
	CHECK(door.GetName().rfind("stopped: The event 'ping pong' ", 0) == 0);
	CHECK(door.GetName().find("need a way to stop") != std::string::npos);

	INFO(errors.size());
	CHECK(errors.empty());
	runtime.Stop();
	AssetManager::SetAssetDirectory({});
	SetContextScene(nullptr);
}
