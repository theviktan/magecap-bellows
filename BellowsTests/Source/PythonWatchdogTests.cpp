#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/PythonWatchdog.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <chrono>

using namespace Bellows;

namespace {

	// A short limit for the test's duration, put back afterwards.
	struct ShortTimeLimit
	{
		double Previous = Scripting::PythonWatchdog::GetTimeLimit();
		explicit ShortTimeLimit(double seconds) { Scripting::PythonWatchdog::SetTimeLimit(seconds); }
		~ShortTimeLimit() { Scripting::PythonWatchdog::SetTimeLimit(Previous); }
	};

	const char* c_Spinner = R"PY(
import bellows

class Spinner(bellows.Script):
    def on_create(self):
        self.updates = 0

    def on_update(self, dt):
        self.updates += 1
        if self.updates == 1:
            while True:
                pass
)PY";

}

TEST_CASE("Python that runs too long in the Console is stopped, and the next code runs normally")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ShortTimeLimit limit(0.3);
	const uint32_t stops = Scripting::PythonWatchdog::GetStopCount();

	const auto start = std::chrono::steady_clock::now();
	std::string output;
	CHECK_FALSE(ScriptEngine::ExecuteSource("while True:\n    pass\n", output));
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK(output.find("KeyboardInterrupt") != std::string::npos);
	CHECK(output.find("without returning") != std::string::npos);
	CHECK(seconds < 5.0);
	CHECK(Scripting::PythonWatchdog::GetStopCount() == stops + 1);

	// 'except Exception' doesn't swallow it, and nothing is left over for the next code.
	CHECK_FALSE(ScriptEngine::ExecuteSource("try:\n    while True:\n        pass\nexcept Exception:\n    pass\n", output));
	CHECK(ScriptEngine::ExecuteSource("1 + 1", output));
	CHECK(output == "2\n");
	CHECK(Scripting::PythonWatchdog::GetStopCount() == stops + 2);

	// Code that finishes within the limit is never stopped.
	CHECK(ScriptEngine::ExecuteSource("total = sum(i for i in range(10000))\ntotal", output));
	CHECK(output == "49995000\n");
	CHECK(Scripting::PythonWatchdog::GetStopCount() == stops + 2);
}

TEST_CASE("Python that catches everything is still stopped, from every line")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ShortTimeLimit limit(0.3);
	const auto start = std::chrono::steady_clock::now();
	std::string output;
	CHECK_FALSE(ScriptEngine::ExecuteSource("while True:\n    try:\n        while True:\n            pass\n    except:\n        pass\n", output));
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	CHECK(output.find("kept running") != std::string::npos);
	CHECK(seconds < 5.0);

	// Once the run is over, lines run normally again.
	CHECK(ScriptEngine::ExecuteSource("total = 0\nfor i in range(1000):\n    total += i\ntotal", output));
	CHECK(output == "499500\n");
}

TEST_CASE("A game sets its own Python time limit, and shipped games have none")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ShortTimeLimit limit(0.3);
	std::string output;

	// Turned off, a long load finishes.
	CHECK(ScriptEngine::ExecuteSource("bellows.set_python_time_limit(None)\nimport time\nend = time.monotonic() + 0.8\n"
		"while time.monotonic() < end:\n    pass\nbellows.python_time_limit()", output));
	CHECK(output == "0.0\n");
	CHECK(ScriptEngine::ExecuteSource("bellows.set_python_time_limit(30)\nbellows.python_time_limit()", output));
	CHECK(output == "30.0\n");
	CHECK(Scripting::PythonWatchdog::GetTimeLimit() == 30.0);
	for (const char* bad : { "-1", "float('nan')", "float('inf')" })
	{
		CAPTURE(bad);
		CHECK_FALSE(ScriptEngine::ExecuteSource(std::string("bellows.set_python_time_limit(") + bad + ")", output));
		CHECK(output.find("ValueError: set_python_time_limit needs a number of seconds from 0 up") != std::string::npos);
	}

	// The editor's default comes back when play stops; a shipped game's default is no limit.
	const double editorDefault = Scripting::PythonWatchdog::GetDefaultTimeLimit();
	CHECK(editorDefault == Scripting::PythonWatchdog::EditorTimeLimit);
	Scripting::PythonWatchdog::ResetTimeLimit();
	CHECK(Scripting::PythonWatchdog::GetTimeLimit() == editorDefault);
	Scripting::PythonWatchdog::SetDefaultTimeLimit(0.0);
	CHECK(Scripting::PythonWatchdog::GetTimeLimit() == 0.0);
	Scripting::PythonWatchdog::SetDefaultTimeLimit(editorDefault);
}

TEST_CASE("A game script stuck in on_update is stopped and the game keeps running")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ShortTimeLimit limit(0.3);
	ScriptEngine::ResetErrorCount();
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "PythonWatchdog";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Spinner.py", c_Spinner));
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity spinner = scene->CreateEntity("Spinner");
	spinner.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Spinner.py";
	scene->OnRuntimeStart();
	ScriptEngine::SetContextScene(scene.get());
	scene->OnUpdateRuntime(1.0f / 60.0f);
	CHECK(ScriptEngine::GetErrorCount() == 1);
	CHECK(ScriptEngine::GetLastError().find("without returning") != std::string::npos);

	// The script was stopped, not disabled: its next frame runs.
	scene->OnUpdateRuntime(1.0f / 60.0f);
	std::string output;
	CHECK(ScriptEngine::ExecuteSource("bellows.find_entity('Spinner').script.updates", output));
	CHECK(output == "2\n");
	CHECK(ScriptEngine::GetErrorCount() == 1);

	scene->OnRuntimeStop();
	ScriptEngine::SetContextScene(nullptr);
	ScriptEngine::ResetErrorCount();
}
