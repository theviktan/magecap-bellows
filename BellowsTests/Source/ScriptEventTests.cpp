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
	ListenerScope first([&calls](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&) { calls.push_back("first " + name); });
	ListenerScope second([&calls](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&) { calls.push_back("second " + name); });

	EmitScriptEvent("door opened", { ScriptValue(true) });
	EmitScriptEvent("door closed", {}, first.ID);
	CHECK_EQ(calls, std::vector<std::string>{ "first door opened", "second door opened", "second door closed" });
}

TEST_CASE("Script event listeners may remove themselves and others while an event runs")
{
	std::vector<std::string> calls;
	ScriptEventListenerID later = 0;
	ScriptEventListenerID self = 0;
	self = AddScriptEventListener([&](const std::string&, const ScriptValueList&, const ScriptEventDelivery&)
	{
		calls.push_back("self");
		RemoveScriptEventListener(self);
		RemoveScriptEventListener(later);
	});
	later = AddScriptEventListener([&calls](const std::string&, const ScriptValueList&, const ScriptEventDelivery&) { calls.push_back("later"); });

	EmitScriptEvent("ping", {});
	EmitScriptEvent("ping", {});
	CHECK_EQ(calls, std::vector<std::string>{ "self" });
}

TEST_CASE("Script events nested too deep stop with an error")
{
	int depth = 0;
	ListenerScope echo([&depth](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&)
	{
		depth++;
		EmitScriptEvent(name, {});
	});
	CHECK_THROWS_WITH_AS(EmitScriptEvent("echo", {}), doctest::Contains("sent from inside 64 other events"), ScriptValueError);
	CHECK_EQ(depth, 64);

	// The depth count is back to zero afterwards, so later events work.
	int calls = 0;
	ListenerScope counter([&calls](const std::string&, const ScriptValueList&, const ScriptEventDelivery&) { calls++; });
	RemoveScriptEventListener(echo.ID);
	EmitScriptEvent("after", {});
	CHECK_EQ(calls, 1);
}

TEST_CASE("Script events that multiply stop after 10,000")
{
	// Two listeners that each answer by sending the event again: the work doubles at every level.
	int sent = 0;
	auto answer = [&sent](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&)
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
	ListenerScope counter([&calls](const std::string&, const ScriptValueList&, const ScriptEventDelivery&) { calls++; });
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
	ListenerScope python([&received](const std::string& name, const ScriptValueList& values, const ScriptEventDelivery&) { received.push_back({ name, values }); });

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
	ListenerScope echo([&python](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&)
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

TEST_CASE("Events from an entity reach its own scripts, then each parent's, every listener at each")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity menu = scene->CreateEntity("Menu");
	Entity panel = scene->CreateEntity("Panel");
	scene->SetParent(panel, menu);
	Entity button = scene->CreateEntity("Start");
	scene->SetParent(button, panel);

	std::vector<std::string> calls;
	auto record = [&calls](const char* listener)
	{
		return [&calls, listener](const std::string& name, const ScriptValueList& values, const ScriptEventDelivery& delivery)
		{
			const std::string target = delivery.Target ? delivery.Target->Resolve().GetName() : "everyone";
			const std::string source = delivery.Source ? delivery.Source->Resolve().GetName() : "nobody";
			calls.push_back(std::string(listener) + " " + name + " at " + target + " from " + source + " with " + std::to_string(values.size()));
		};
	};
	ListenerScope forge(record("first"));
	ListenerScope python(record("second"));

	EmitScriptEventFrom(ScriptEntity(button), "clicked", { ScriptValue(ScriptEntity(button)) });
	CHECK_EQ(calls, std::vector<std::string>{
		"first clicked at Start from Start with 1", "second clicked at Start from Start with 1",
		"first clicked at Panel from Start with 1", "second clicked at Panel from Start with 1",
		"first clicked at Menu from Start with 1", "second clicked at Menu from Start with 1" });

	// A broadcast has neither a target nor a source.
	calls.clear();
	EmitScriptEvent("cancelled", {});
	CHECK_EQ(calls, std::vector<std::string>{ "first cancelled at everyone from nobody with 0", "second cancelled at everyone from nobody with 0" });

	// An entity that no longer exists raises nothing.
	calls.clear();
	ScriptEntity gone(scene->CreateEntity("Gone"));
	scene->DestroyEntity(gone.Resolve());
	CHECK_NOTHROW(EmitScriptEventFrom(gone, "clicked", {}));
	CHECK(calls.empty());
}

TEST_CASE("Events going up the hierarchy stop where an entity is destroyed")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity menu = scene->CreateEntity("Menu");
	Entity button = scene->CreateEntity("Start");
	scene->SetParent(button, menu);

	// Stands in for a script on the button that closes the whole menu when clicked.
	std::vector<std::string> reached;
	ListenerScope closer([&](const std::string&, const ScriptValueList&, const ScriptEventDelivery& delivery)
	{
		const Entity target = delivery.Target->Resolve();
		reached.push_back(target.GetName());
		if (target == button)
			scene->DestroyEntity(menu);
	});
	ListenerScope after([&reached](const std::string&, const ScriptValueList&, const ScriptEventDelivery&)
	{
		// The second listener still hears the button: the walk only ends before the next entity.
		reached.push_back("after");
	});

	EmitScriptEventFrom(ScriptEntity(button), "clicked", {});
	CHECK_EQ(reached, std::vector<std::string>{ "Start", "after" });
}

TEST_CASE("Events going up the hierarchy that keep raising events stop with an error")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity button = scene->CreateEntity("Start");
	int depth = 0;
	ListenerScope echo([&](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&)
	{
		depth++;
		EmitScriptEventFrom(ScriptEntity(button), name, {});
	});
	CHECK_THROWS_WITH_AS(EmitScriptEventFrom(ScriptEntity(button), "changed", {}), doctest::Contains("sent from inside 64 other events"), ScriptValueError);
	CHECK_EQ(depth, 64);
}

TEST_CASE("Queued script events wait for the flush, go in order, and stop when they multiply")
{
	ClearQueuedScriptEvents();
	Scene scene("Queued");
	const ScriptEntity hero(scene.CreateEntity("Hero"));
	std::vector<std::string> received;
	bool aboutHero = true;
	ListenerScope listener([&](const std::string& name, const ScriptValueList& values, const ScriptEventDelivery& delivery)
	{
		received.push_back(name + " " + std::get<std::string>(values.at(1).Data));
		aboutHero &= !delivery.Target && delivery.Source && delivery.Source->ID == hero.ID;
		// A handler's change, queued while flushing, goes out in the same flush.
		if (std::get<std::string>(values.at(1).Data) == "health")
			QueueScriptEventAbout(hero, "field changed", { hero, std::string("mood") });
	});

	QueueScriptEventAbout(hero, "field changed", { hero, std::string("health") });
	QueueScriptEventAbout(hero, "field changed", { hero, std::string("speed") });
	CHECK(received.empty());
	FlushQueuedScriptEvents();
	CHECK_EQ(received, std::vector<std::string>{ "field changed health", "field changed speed", "field changed mood" });
	CHECK(aboutHero);

	// An event about an entity that's gone is dropped.
	{
		Scene closed("Closed");
		QueueScriptEventAbout(ScriptEntity(closed.CreateEntity("Gone")), "field changed", { hero, std::string("gone") });
	}
	received.clear();
	FlushQueuedScriptEvents();
	CHECK(received.empty());

	// Stopping play drops what's waiting.
	QueueScriptEventAbout(hero, "field changed", { hero, std::string("speed") });
	ClearQueuedScriptEvents();
	received.clear();
	FlushQueuedScriptEvents();
	CHECK(received.empty());
}

TEST_CASE("Queued script events that keep causing more stop after the limit")
{
	ClearQueuedScriptEvents();
	Scene scene("Looping");
	const ScriptEntity hero(scene.CreateEntity("Hero"));
	size_t received = 0;
	// Two fields that keep changing each other.
	ListenerScope listener([&](const std::string&, const ScriptValueList&, const ScriptEventDelivery&)
	{
		received++;
		QueueScriptEventAbout(hero, "field changed", { hero, std::string("ping") });
	});
	QueueScriptEventAbout(hero, "field changed", { hero, std::string("ping") });
	FlushQueuedScriptEvents();
	CHECK(received == c_MaxQueuedEventsPerFlush);
	received = 0;
	FlushQueuedScriptEvents();
	CHECK(received == 0);		// Nothing was left behind
}

TEST_CASE("A running scene sends queued script events each frame")
{
	Scene scene("Running");
	const ScriptEntity hero(scene.CreateEntity("Hero"));
	size_t received = 0;
	ListenerScope listener([&](const std::string& name, const ScriptValueList&, const ScriptEventDelivery&) { received += name == "field changed"; });
	scene.OnRuntimeStart();
	QueueScriptEventAbout(hero, "field changed", { hero, std::string("health") });
	scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
	CHECK(received == 1);
	scene.OnRuntimeStop();
}

TEST_CASE("A ForgeScript entity script's fields send 'field changed' once it's created")
{
	Ref<Scene> scene = CreateRef<Scene>();
	SetContextScene(scene.get());
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "FieldChanged";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts/Hero.forge",
		"set health to 100\n"
		"set mood to \"calm\"\n"
		"set _secret to 1\n"
		"when created\n"
		"\tset health to 90\n"
		"end\n"
		"when hurt\n"
		"\tset health to health - 10\n"
		"\tset health to health\n"
		"\tset _secret to 2\n"
		"\thurt_more()\n"
		"end\n"
		"function hurt_more()\n"
		"\tset mood to \"angry\"\n"
		"end\n"));
	AssetManager::SetAssetDirectory(assets);

	std::vector<std::string> errors;
	ForgeScriptRuntime runtime([&errors](const std::string&, const std::string& message) { errors.push_back(message); });
	std::vector<std::string> changes;
	ListenerScope listener([&changes](const std::string& name, const ScriptValueList& values, const ScriptEventDelivery&)
	{
		if (name != "field changed" || values.size() != 4)
			return;
		auto text = [](const ScriptValue& value)
		{
			if (const double* number = std::get_if<double>(&value.Data))
				return std::to_string(static_cast<int>(*number));
			const std::string* string = std::get_if<std::string>(&value.Data);
			return string ? *string : std::string("?");
		};
		changes.push_back(std::get<std::string>(values[1].Data) + " " + text(values[3]) + " to " + text(values[2]));
	});

	Entity hero = scene->CreateEntity("Hero");
	hero.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Hero.forge";
	runtime.Start();
	REQUIRE(runtime.CreateInstance(hero));
	runtime.CallCreated(hero.GetUUID());
	FlushQueuedScriptEvents();
	CHECK(changes.empty());		// What 'when created' sets is a starting value

	EmitScriptEvent("hurt", {});
	CHECK(changes.empty());		// Queued until the flush
	FlushQueuedScriptEvents();
	CHECK_EQ(changes, std::vector<std::string>{ "health 90 to 80", "mood calm to angry" });
	INFO(errors.size());
	CHECK(errors.empty());
	runtime.Stop();
	AssetManager::SetAssetDirectory({});
	SetContextScene(nullptr);
}
