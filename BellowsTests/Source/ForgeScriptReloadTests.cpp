#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptApi.h"

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
	h.Runtime.Reload("Scripts/Counter.forge");

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
	h.Runtime.Reload("Scripts/Guard.forge");

	// Each mistake is reported once, not once per entity, with its file and line.
	INFO(h.AllErrors());
	REQUIRE_EQ(h.Errors.size(), static_cast<size_t>(2));
	CHECK_EQ(h.Errors[0].rfind("Scripts/Guard.forge line 2: ", 0), 0u);
	CHECK_EQ(h.Errors[1].rfind("Scripts/Guard.forge line 3: ", 0), 0u);

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
	h.Runtime.Reload("Scripts/Helpers.forge");
	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(greeter.GetName(), std::string("hi again"));

	// Other files never reload ForgeScript.
	h.Runtime.Reload("Scripts/Mover.py");
	CHECK(h.Errors.empty());
}
