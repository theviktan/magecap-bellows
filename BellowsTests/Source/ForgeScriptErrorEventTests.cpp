#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

using namespace Bellows;

namespace {

	// ForgeScript errors reach the 'script error' event like Python's: the place, a stack of
	// "file:line in function" (innermost first) and the language. ForgeScript errors have no name.
	struct ErrorEventScene
	{
		Ref<Bellows::Scene> Play = CreateRef<Bellows::Scene>();
		std::vector<Scripting::ScriptValueList> Errors;
		Scripting::ScriptEventListenerID Listener = 0;

		explicit ErrorEventScene(const char* faulty, const char* watcher = nullptr)
		{
			REQUIRE(ScriptEngine::IsInitialized());
			const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "ForgeScriptErrorEvents";
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Faulty.forge", faulty));
			AssetManager::SetAssetDirectory(assets);
			Play->CreateEntity("Faulty").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Faulty.forge";
			if (watcher)
			{
				REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Watcher.forge", watcher));
				Play->CreateEntity("Watcher").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Watcher.forge";
			}
			Listener = Scripting::AddScriptEventListener([this](const std::string& name, const Scripting::ScriptValueList& values,
				const Scripting::ScriptEventDelivery&)
			{
				if (name == "script error")
					Errors.push_back(values);
			});
		}

		~ErrorEventScene()
		{
			Scripting::RemoveScriptEventListener(Listener);
			Play->OnRuntimeStop();
			ScriptEngine::ResetErrorCount();
		}

		void Run(int frames = 1)
		{
			Play->OnRuntimeStart();
			ScriptEngine::SetContextScene(Play.get());
			for (int i = 0; i < frames; i++)
				Play->OnUpdateRuntime(1.0f / 60.0f);
		}
	};

	std::string Text(const Scripting::ScriptValue& value) { return std::get<std::string>(value.Data); }
	double Number(const Scripting::ScriptValue& value) { return std::get<double>(value.Data); }

	std::vector<std::string> Stack(const Scripting::ScriptValue& value)
	{
		std::vector<std::string> lines;
		for (const Scripting::ScriptValue& line : std::get<Scripting::ScriptValueList>(value.Data))
			lines.push_back(Text(line));
		return lines;
	}

}

TEST_CASE("A ForgeScript error reaches 'script error' with its place and stack")
{
	// The error is raised on line 2, in heal(), called on line 5, from 'when update' on line 8.
	ErrorEventScene fixture(
		"function heal(amount)\n"
		"\tsay [1][amount]\n"
		"end\n"
		"function turn()\n"
		"\theal(5)\n"
		"end\n"
		"when update with delta\n"
		"\tturn()\n"
		"end\n");
	fixture.Run();

	REQUIRE(fixture.Errors.size() == 1);
	const Scripting::ScriptValueList& error = fixture.Errors[0];
	REQUIRE(error.size() == 6);
	CHECK(Text(error[0]).find("outside the list") != std::string::npos);
	CHECK(Text(error[1]) == "Scripts/Faulty.forge");
	CHECK(Number(error[2]) == 2.0);
	CHECK(Stack(error[3]) == std::vector<std::string>{ "Scripts/Faulty.forge:2 in heal", "Scripts/Faulty.forge:5 in turn",
		"Scripts/Faulty.forge:8 in when update" });
	CHECK(Text(error[4]) == "forgescript");
	CHECK(Text(error[5]).empty());
}

TEST_CASE("A ForgeScript script that doesn't compile reaches 'script error' too")
{
	ErrorEventScene fixture("set speed to 5 6\n");
	fixture.Run();

	REQUIRE(fixture.Errors.size() == 1);
	const Scripting::ScriptValueList& error = fixture.Errors[0];
	REQUIRE(error.size() == 6);
	CHECK(Text(error[1]) == "Scripts/Faulty.forge");
	CHECK(Number(error[2]) == 1.0);
	CHECK(Stack(error[3]) == std::vector<std::string>{ "Scripts/Faulty.forge:1" });
	CHECK(Text(error[4]) == "forgescript");
}

TEST_CASE("A ForgeScript 'script error' handler that fails is only logged, never sent again")
{
	// The watcher hears Faulty's error each frame and fails while handling it; its own failures must not
	// come back to it as new 'script error' events, or it would loop.
	ErrorEventScene fixture(
		"when update with delta\n\tsay [1][5]\nend\n",
		"set heard to 0\n"
		"when script error with message, file, line, stack, language, name\n"
		"\tadd 1 to heard\n"
		"\tset my name to \"heard {heard} from {file}:{line}\"\n"
		"\tsay [1][9]\n"
		"end\n");
	ScriptEngine::ResetErrorCount();
	fixture.Run(2);

	// Faulty's two errors were sent; the watcher's two failures were only logged.
	CHECK(ScriptEngine::GetErrorCount() == 4);
	REQUIRE(fixture.Errors.size() == 2);
	for (const Scripting::ScriptValueList& error : fixture.Errors)
		CHECK(Text(error[1]) == "Scripts/Faulty.forge");
	// The watcher renamed itself on each error it heard, before failing.
	CHECK(static_cast<bool>(fixture.Play->FindEntityByName("heard 2 from Scripts/Faulty.forge:2")));
}
