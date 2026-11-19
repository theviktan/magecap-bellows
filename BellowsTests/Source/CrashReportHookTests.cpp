#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

using namespace Bellows;

namespace {

	std::string Evaluate(const std::string& expression)
	{
		std::string output;
		REQUIRE(ScriptEngine::ExecuteSource(expression, output));
		while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
			output.pop_back();
		return output;
	}

	// Records 'script error' and 'game quitting'; its handler fails once, to show that is not sent again.
	const char* c_Watcher = R"PY(
import bellows

class Watcher(bellows.Script):
    def on_create(self):
        self.errors = []
        self.quitting = 0
        self.fail_in_handler = False

    def on_event(self, name, *values):
        if name == "script error":
            self.errors.append(values)
            if self.fail_in_handler:
                self.fail_in_handler = False
                raise RuntimeError("the handler broke")
        elif name == "game quitting":
            self.quitting += 1
)PY";

	// The error is raised on line 6, inside explode(), called from on_update on line 9.
	const char* c_Faulty = R"PY(
import bellows

class Faulty(bellows.Script):
    def explode(self):
        return self.missing_value

    def on_update(self, dt):
        self.explode()
)PY";

	const char* c_Broken = "import bellows\n\nclass Broken(bellows.Script)\n    pass\n";

	const char* c_ForgeWatcher = R"FORGE(
when script error with message, file, line, stack, language, name
	set my name to "{name} at {file}:{line} ({language}, {stack.length} steps)"
end

when game quitting
	set my name to "quitting"
end
)FORGE";

	struct CrashHookScene
	{
		Ref<Bellows::Scene> Play = CreateRef<Bellows::Scene>();
		Entity Watcher;
		Entity ForgeWatcher;

		CrashHookScene()
		{
			REQUIRE(ScriptEngine::IsInitialized());
			const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "CrashReportHooks";
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Watcher.py", c_Watcher));
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Faulty.py", c_Faulty));
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Broken.py", c_Broken));
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "ForgeWatcher.forge", c_ForgeWatcher));
			AssetManager::SetAssetDirectory(assets);

			Watcher = Play->CreateEntity("Watcher");
			Watcher.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Watcher.py";
			ForgeWatcher = Play->CreateEntity("ForgeWatcher");
			ForgeWatcher.AddComponent<ScriptComponent>().ScriptPath = "Scripts/ForgeWatcher.forge";
		}

		~CrashHookScene()
		{
			Play->OnRuntimeStop();
			ScriptEngine::ResetErrorCount();
		}

		void Start()
		{
			Play->OnRuntimeStart();
			ScriptEngine::SetContextScene(Play.get());
		}
	};

	const std::string c_Errors = "bellows.find_entity('Watcher').script.errors";

}

TEST_CASE("A Python error reaches 'script error' with its message, place, stack and name, in both languages")
{
	CrashHookScene fixture;
	Entity faulty = fixture.Play->CreateEntity("Faulty");
	faulty.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Faulty.py";
	fixture.Start();
	ScriptEngine::ResetErrorCount();
	fixture.Play->OnUpdateRuntime(1.0f / 60.0f);

	CHECK(ScriptEngine::GetErrorCount() == 1);
	REQUIRE(Evaluate("len(" + c_Errors + ")") == "1");
	const std::string error = c_Errors + "[0]";
	CHECK(Evaluate(error + "[0]") == "\"'Faulty' object has no attribute 'missing_value'\"");
	CHECK(Evaluate(error + "[1:3]") == "('Scripts/Faulty.py', 6.0)");
	CHECK(Evaluate(error + "[3]") == "['Scripts/Faulty.py:6 in explode', 'Scripts/Faulty.py:9 in on_update']");
	CHECK(Evaluate(error + "[4:]") == "('python', 'AttributeError')");
	CHECK(fixture.ForgeWatcher.GetName() == "AttributeError at Scripts/Faulty.py:6 (python, 2 steps)");
}

TEST_CASE("An error inside a 'script error' handler is only logged, never sent again")
{
	CrashHookScene fixture;
	Entity faulty = fixture.Play->CreateEntity("Faulty");
	faulty.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Faulty.py";
	fixture.Start();
	Evaluate("bellows.find_entity('Watcher').script.fail_in_handler = True");
	ScriptEngine::ResetErrorCount();
	fixture.Play->OnUpdateRuntime(1.0f / 60.0f);

	// Both errors are logged; only Faulty's was sent.
	CHECK(ScriptEngine::GetErrorCount() == 2);
	CHECK(Evaluate("len(" + c_Errors + ")") == "1");

	// The guard ends with the handler: the next error is sent as usual.
	fixture.Play->OnUpdateRuntime(1.0f / 60.0f);
	CHECK(Evaluate("len(" + c_Errors + ")") == "2");
}

TEST_CASE("A Python syntax error is placed at its line, with the file it is in")
{
	CrashHookScene fixture;
	fixture.Start();
	Entity broken = fixture.Play->CreateEntity("Broken");
	broken.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Broken.py";
	ScriptEngine::ResetErrorCount();
	fixture.Play->OnUpdateRuntime(1.0f / 60.0f);

	CHECK(ScriptEngine::GetErrorCount() == 1);
	REQUIRE(Evaluate("len(" + c_Errors + ")") == "1");
	const std::string error = c_Errors + "[0]";
	CHECK(Evaluate(error + "[1:3]") == "('Scripts/Broken.py', 3.0)");
	CHECK(Evaluate(error + "[3][0]") == "'Scripts/Broken.py:3'");
	CHECK(Evaluate(error + "[5]") == "'SyntaxError'");
	CHECK(Evaluate(error + "[0]") != "''");
}

TEST_CASE("ReportScriptError sends every value in order, and the stack as a list")
{
	CrashHookScene fixture;
	fixture.Start();

	Scripting::ScriptErrorReport report;
	report.Message = "Can't divide by zero.";
	report.File = "Scripts/player.forge";
	report.Line = 12;
	report.Stack = { "Scripts/player.forge:12 in heal", "Scripts/player.forge:3" };
	report.Language = "forgescript";
	Scripting::ReportScriptError(report);

	REQUIRE(Evaluate("len(" + c_Errors + ")") == "1");
	CHECK(Evaluate(c_Errors + "[0]") ==
		"(\"Can't divide by zero.\", 'Scripts/player.forge', 12.0, ['Scripts/player.forge:12 in heal', 'Scripts/player.forge:3'], 'forgescript', '')");
	CHECK(fixture.ForgeWatcher.GetName() == " at Scripts/player.forge:12 (forgescript, 2 steps)");
}

TEST_CASE("'game quitting' reaches every script once, and a scene change does not send it")
{
	CrashHookScene fixture;
	fixture.Start();

	// load_scene stops the old scene: on_destroy runs, 'game quitting' does not.
	Ref<Scene> next = CreateRef<Scene>();
	Entity watcher = next->CreateEntity("Watcher");
	watcher.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Watcher.py";
	fixture.Play->OnRuntimeStop();
	fixture.Play = next;
	fixture.Start();
	CHECK(Evaluate("bellows.find_entity('Watcher').script.quitting") == "0");

	Scripting::EmitGameQuitting();
	CHECK(Evaluate("bellows.find_entity('Watcher').script.quitting") == "1");
	CHECK(ScriptEngine::GetErrorCount() == 0);
}

TEST_CASE("ForgeScript hears 'game quitting'")
{
	CrashHookScene fixture;
	fixture.Start();
	Scripting::EmitGameQuitting();
	CHECK(fixture.ForgeWatcher.GetName() == "quitting");
}

TEST_CASE("log_folder() is user_folder()/Logs")
{
	CHECK(Scripting::GetLogFolder() == Scripting::GetUserFolder() / "Logs");
	CHECK(Evaluate("bellows.log_folder() == bellows.user_folder() + __import__('os').sep + 'Logs'") == "True");
}
