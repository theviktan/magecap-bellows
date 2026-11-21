#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <string>
#include <vector>

using namespace Bellows;

namespace {

	// A play session of ForgeScript scripts, driven the way ScriptEngine drives it.
	struct ReloadHarness
	{
		Ref<Scene> PlayScene = CreateRef<Scene>();
		std::vector<std::string> Errors;
		Scripting::ForgeScriptRuntime Runtime{ [this](const std::string&, const std::string& message) { Errors.push_back(message); } };

		explicit ReloadHarness(const std::string& project)
		{
			AssetManager::SetAssetDirectory(BellowsTests::GetTestOutputDirectory() / project);
			Scripting::SetContextScene(PlayScene.get());
		}

		~ReloadHarness()
		{
			Runtime.Stop();
			Scripting::SetContextScene(nullptr);
			AssetManager::SetAssetDirectory({});
		}

		void Write(const std::string& path, const std::string& source)
		{
			REQUIRE(FileSystem::WriteTextFile(AssetManager::GetAbsolutePath(path), source));
		}

		Entity Spawn(const std::string& name, const std::string& scriptPath)
		{
			Entity entity = PlayScene->CreateEntity(name);
			entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath;
			return entity;
		}

		void Start(const std::vector<Entity>& entities)
		{
			Runtime.Start();
			for (Entity entity : entities)
				REQUIRE(Runtime.CreateInstance(entity));
			for (Entity entity : entities)
				Runtime.CallCreated(entity.GetUUID());
		}

		void Frames(int count)
		{
			for (int i = 0; i < count; i++)
				Runtime.Update(*PlayScene, 1.0f / 60.0f);
		}

		std::string AllErrors() const
		{
			std::string text;
			for (const std::string& error : Errors)
				text += error + "\n";
			return text;
		}
	};

	// The number at the end of a name like "count 12".
	int TrailingNumber(const std::string& text)
	{
		return std::stoi(text.substr(text.find_last_of(' ') + 1));
	}

}

TEST_CASE("ForgeScript hot reload keeps script variables and runs the new code")
{
	ReloadHarness h("ForgeReload");
	h.Write("Scripts/Counter.forge",
		"set count to 0\n"
		"set speed to 2.0\n"
		"set label to \"old\"\n"
		"when created\n\tstart tick()\nend\n"
		"function tick()\n\trepeat forever\n\t\tadd 1 to count\n\t\twait 1 frame\n\tend\nend\n");

	Entity counter = h.Spawn("Counter", "Scripts/Counter.forge");
	counter.GetComponent<ScriptComponent>().Fields["speed"] = 5.0;
	h.Start({ counter });
	h.Frames(10);

	// The new version: tick adds 10, 'label' became a number, and 'extra' is a new setting.
	h.Write("Scripts/Counter.forge",
		"set count to 0\n"
		"set speed to 2.0\n"
		"set label to 7\n"
		"set extra to \"default\"\n"
		"when reloaded\n"
		"\tset my name to \"reloaded {count} {speed} {label} {extra}\"\n"
		"\tstart tick()\n"
		"end\n"
		"when created\n\tset my name to \"created again\"\nend\n"
		"when update with delta\n\tset my name to \"count {count}\"\nend\n"
		"function tick()\n\trepeat forever\n\t\tadd 10 to count\n\t\twait 1 frame\n\tend\nend\n");
	counter.GetComponent<ScriptComponent>().Fields["extra"] = std::string("from the Inspector");
	CHECK(h.Runtime.Reload("Scripts/Counter.forge"));

	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	// count and speed kept their values; label changed type, so it takes the new value; 'when created' did not run.
	const std::string name = counter.GetName();
	REQUIRE(name.rfind("reloaded ", 0) == 0);
	const int countAtReload = std::stoi(name.substr(9, name.find(' ', 9) - 9));
	CHECK(countAtReload >= 9);
	CHECK_EQ(name.substr(name.find(' ', 9)), std::string(" 5 7 from the Inspector"));

	// Only the new tick runs: the old version's waiting task was stopped.
	h.Frames(1);
	const int first = TrailingNumber(counter.GetName());
	h.Frames(1);
	CHECK_EQ(TrailingNumber(counter.GetName()) - first, 10);
}

TEST_CASE("ForgeScript hot reload keeps the old version when the new one has mistakes")
{
	ReloadHarness h("ForgeReloadMistake");
	h.Write("Scripts/Guard.forge", "when update with delta\n\tset my name to \"working\"\nend\n");
	Entity first = h.Spawn("First", "Scripts/Guard.forge");
	Entity second = h.Spawn("Second", "Scripts/Guard.forge");
	h.Start({ first, second });

	h.Write("Scripts/Guard.forge", "when update with delta\n\tset to 5\n\tsay 1 + * 2\nend\n");
	std::vector<std::string> sent;
	const Scripting::ScriptEventListenerID listener = Scripting::AddScriptEventListener(
		[&sent](const std::string& name, const Scripting::ScriptValueList& values, const Scripting::ScriptEventDelivery&)
		{
			if (name == "script error" && values.size() == 6)
				sent.push_back(std::get<std::string>(values[1].Data) + ":" + std::to_string(static_cast<int>(std::get<double>(values[2].Data))));
		});
	CHECK_FALSE(h.Runtime.Reload("Scripts/Guard.forge"));
	Scripting::RemoveScriptEventListener(listener);

	// Each mistake is reported once, not once per entity, with its file and line, and sent to 'script error'.
	INFO(h.AllErrors());
	REQUIRE_EQ(h.Errors.size(), static_cast<size_t>(2));
	CHECK_EQ(h.Errors[0].rfind("Scripts/Guard.forge line 2: ", 0), 0u);
	CHECK_EQ(h.Errors[1].rfind("Scripts/Guard.forge line 3: ", 0), 0u);
	CHECK_EQ(sent, std::vector<std::string>{ "Scripts/Guard.forge:2", "Scripts/Guard.forge:3" });

	first.SetName("reset");
	h.Frames(1);
	CHECK_EQ(first.GetName(), std::string("working"));
	CHECK_EQ(second.GetName(), std::string("working"));
}

TEST_CASE("ForgeScript hot reload reads changed helper scripts")
{
	ReloadHarness h("ForgeReloadHelpers");
	h.Write("Scripts/Helpers.forge", "function greeting()\n\treturn \"hello\"\nend\n");
	h.Write("Scripts/Greeter.forge",
		"use \"Scripts/Helpers\"\n"
		"when created\n\tset my name to greeting()\nend\n"
		"when reloaded\n\tset my name to greeting()\nend\n");
	Entity greeter = h.Spawn("Greeter", "Scripts/Greeter.forge");
	h.Start({ greeter });
	CHECK_EQ(greeter.GetName(), std::string("hello"));

	// Only the helper changed; the script that uses it picks up the new version.
	h.Write("Scripts/Helpers.forge", "function greeting()\n\treturn \"hi again\"\nend\n");
	CHECK(h.Runtime.Reload("Scripts/Helpers.forge"));
	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(greeter.GetName(), std::string("hi again"));

	// Other files never reload ForgeScript.
	CHECK(h.Runtime.Reload("Scripts/Mover.py"));
	CHECK(h.Errors.empty());
}

TEST_CASE("ForgeScript 'use' stays inside the asset folder")
{
	// Audit S-3: a game's scripts can't load files outside its assets, by path or through a link.
	ReloadHarness h("ForgeUseInside");
	REQUIRE(FileSystem::WriteTextFile(BellowsTests::GetTestOutputDirectory() / "ForgeUseOutside.forge", "set secret to 42\n"));
	h.Write("Scripts/Climbs.forge", "use \"../ForgeUseOutside\"\nwhen created\n\tset my name to text(secret)\nend\n");
	Entity climbs = h.Spawn("Climbs", "Scripts/Climbs.forge");
	h.Runtime.Start();
	CHECK_FALSE(h.Runtime.CreateInstance(climbs));
	CHECK(h.AllErrors().find("'use' only reaches scripts inside") != std::string::npos);
	CHECK_EQ(climbs.GetName(), std::string("Climbs"));

	std::error_code error;
	std::filesystem::create_symlink(BellowsTests::GetTestOutputDirectory() / "ForgeUseOutside.forge",
		AssetManager::GetAbsolutePath("Scripts/Linked.forge"), error);
	if (!error)
	{
		h.Errors.clear();
		h.Write("Scripts/Links.forge", "use \"Scripts/Linked\"\nwhen created\n\tset my name to text(secret)\nend\n");
		Entity links = h.Spawn("Links", "Scripts/Links.forge");
		h.Runtime.CreateInstance(links);
		h.Runtime.CallCreated(links.GetUUID());
		CHECK_EQ(links.GetName(), std::string("Links"));
		CHECK(h.AllErrors().find("Could not find the script") != std::string::npos);
	}
}

TEST_CASE("ForgeScript while the game is paused: only Run While Paused scripts update and wait")
{
	ReloadHarness h("ForgePause");
	h.Write("Scripts/Ticker.forge", "set ticks to 0\nwhen update\n\tset ticks to ticks + 1\n\tset my name to \"ticks {ticks}\"\nend\n");
	h.Write("Scripts/Waiter.forge", "when created\n\twait 0.25 seconds\n\tset my name to \"waited\"\nend\n");
	h.Write("Scripts/Pauser.forge", "when created\n\tpause the game\n\tif the game is paused then\n\t\tset my name to \"paused\"\n\tend\nend\n");
	h.Write("Scripts/Resumer.forge", "when created\n\tresume the game\n\tset my name to \"resumed {the game is paused}\"\nend\n");
	Entity gameTicker = h.Spawn("Game Ticker", "Scripts/Ticker.forge");
	Entity menuTicker = h.Spawn("Menu Ticker", "Scripts/Ticker.forge");
	Entity gameWaiter = h.Spawn("Game Waiter", "Scripts/Waiter.forge");
	Entity menuWaiter = h.Spawn("Menu Waiter", "Scripts/Waiter.forge");
	Entity pauser = h.Spawn("Pauser", "Scripts/Pauser.forge");
	menuTicker.GetComponent<ScriptComponent>().RunWhilePaused = true;
	menuWaiter.GetComponent<ScriptComponent>().RunWhilePaused = true;
	h.Start({ gameTicker, menuTicker, gameWaiter, menuWaiter, pauser });
	CHECK(h.PlayScene->IsGamePaused());
	CHECK_EQ(pauser.GetName(), std::string("paused"));

	// Paused: the marked scripts go on in real time, the others hold.
	h.Frames(30);
	CHECK_EQ(gameTicker.GetName(), std::string("Game Ticker"));
	CHECK_EQ(gameWaiter.GetName(), std::string("Game Waiter"));
	CHECK_EQ(menuTicker.GetName(), std::string("ticks 30"));
	CHECK_EQ(menuWaiter.GetName(), std::string("waited"));

	// Resumed: the held scripts carry on from where they were.
	Entity resumer = h.Spawn("Resumer", "Scripts/Resumer.forge");
	REQUIRE(h.Runtime.CreateInstance(resumer));
	h.Runtime.CallCreated(resumer.GetUUID());
	CHECK_FALSE(h.PlayScene->IsGamePaused());
	CHECK_EQ(resumer.GetName(), std::string("resumed false"));
	h.Frames(30);
	CHECK_EQ(gameTicker.GetName(), std::string("ticks 30"));
	CHECK_EQ(gameWaiter.GetName(), std::string("waited"));
	CHECK_EQ(menuTicker.GetName(), std::string("ticks 60"));
	CHECK(h.Errors.empty());

	// The Inspector's hint: a menu button that waits holds while paused unless it's marked.
	h.Write("Scripts/Menu.forge", "function fade()\n\twait 0.2 seconds\nend\nwhen \"Resume\" is clicked\n\tfade()\n\tresume the game\nend\n"
		"when \"Quit\" is clicked\n\tsay \"bye\"\nend\nwhen update\n\twait 1 frame\nend\n");
	ScriptClassInfo info;
	std::string error;
	REQUIRE(h.Runtime.GetScriptInfo("Scripts/Menu.forge", info, error));
	REQUIRE_EQ(info.WaitingUIHandlers.size(), static_cast<size_t>(1));
	CHECK_EQ(info.WaitingUIHandlers[0].Event, std::string("\"Resume\" is clicked"));
	CHECK_EQ(info.WaitingUIHandlers[0].Line, 4);
}

TEST_CASE("ForgeScript 'wait for' and timers in the engine")
{
	using Scripting::EmitScriptEventFrom;
	using Scripting::ScriptEntity;
	using Scripting::ScriptValue;
	ReloadHarness h("ForgeWaitFor");
	h.Write("Scripts/Menu.forge",
		"when created\n"
		"\twait for \"Start\" is clicked\n"
		"\tset my name to \"started\"\n"
		"\twait for collided with other\n"
		"\tset my name to \"touched {other.name}\"\n"
		"end\n");
	h.Write("Scripts/Spawner.forge",
		"set count to 0\n"
		"every 0.25 seconds\n"
		"\tadd 1 to count\n"
		"\tset my name to \"spawned {count}\"\n"
		"\tif count is 3 then\n"
		"\t\tstop\n"
		"\tend\n"
		"end\n");
	Entity menu = h.Spawn("Menu", "Scripts/Menu.forge");
	Entity spawner = h.Spawn("Spawner", "Scripts/Spawner.forge");
	Entity quit = h.PlayScene->CreateEntity("Quit");
	Entity start = h.PlayScene->CreateEntity("Start");
	h.PlayScene->SetParent(quit, menu);
	h.PlayScene->SetParent(start, menu);
	h.Start({ menu, spawner });

	// A click climbs to the menu's script; only the Start button's ends the wait.
	EmitScriptEventFrom(ScriptEntity(quit), "clicked", { ScriptValue(ScriptEntity(quit)) });
	CHECK_EQ(menu.GetName(), std::string("Menu"));
	EmitScriptEventFrom(ScriptEntity(start), "clicked", { ScriptValue(ScriptEntity(start)) });
	CHECK_EQ(menu.GetName(), std::string("started"));

	// A contact reaches a script that only waits for it, with no 'when collided'.
	h.Runtime.Contact(menu.GetUUID(), quit, ContactEventType::CollisionEnter);
	CHECK_EQ(menu.GetName(), std::string("touched Quit"));

	// The timer ticks every quarter second, three times, then stops.
	h.Frames(60);
	CHECK_EQ(spawner.GetName(), std::string("spawned 3"));
	CHECK(h.Errors.empty());
}
