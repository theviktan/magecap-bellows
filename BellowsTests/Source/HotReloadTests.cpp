#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/FileWatcher.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <chrono>

using namespace Bellows;

namespace {

	// Writes a file and moves its modification time forward, so a quick rewrite still counts as a change.
	void Save(const std::filesystem::path& path, const std::string& text)
	{
		std::error_code error;
		const bool existed = std::filesystem::exists(path, error);
		const auto before = existed ? std::filesystem::last_write_time(path, error) : std::filesystem::file_time_type();
		REQUIRE(FileSystem::WriteTextFile(path, text));
		if (existed)
			std::filesystem::last_write_time(path, before + std::chrono::seconds(2), error);
	}

	void RunFrames(Scene& scene, int frames)
	{
		for (int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(1.0f / 60.0f);
	}

	std::string Evaluate(const std::string& expression)
	{
		std::string output;
		REQUIRE(ScriptEngine::ExecuteSource(expression, output));
		while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
			output.pop_back();
		return output;
	}

	const char* c_CounterV1 = R"PY(
import bellows
from Scripts.Steps import STEP

class Counter(bellows.Script):
    step: int = 1

    def on_create(self):
        self.count = 0
        self.reloads = 0

    def on_update(self, dt):
        self.count += self.step * STEP
)PY";

	const char* c_CounterV2 = R"PY(
import bellows
from Scripts.Steps import STEP

class Counter(bellows.Script):
    step: int = 1

    def on_create(self):
        self.count = 0
        self.reloads = 0

    def on_update(self, dt):
        self.count += 1000 * STEP

    def on_reload(self):
        self.reloads += 1
)PY";

}

TEST_CASE("FileWatcher reports files saved under a folder")
{
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Watched";
	std::error_code error;
	std::filesystem::remove_all(folder, error);
	Save(folder / "Scripts" / "A.py", "a = 1\n");
	Save(folder / "Notes.txt", "notes\n");
	Save(folder / "Scripts" / "__pycache__" / "A.py", "cached\n");

	FileWatcher watcher;
	CHECK_FALSE(watcher.IsWatching());
	CHECK(watcher.Poll().empty());
	watcher.Start(folder, { ".py", ".forge" });
	CHECK(watcher.IsWatching());
	CHECK(watcher.Poll().empty());

	Save(folder / "Scripts" / "A.py", "a = 2\n");
	Save(folder / "Scripts" / "B.forge", "say 1\n");
	Save(folder / "Notes.txt", "changed\n");
	Save(folder / "Scripts" / "__pycache__" / "A.py", "changed\n");
	CHECK(watcher.Poll() == std::vector<std::string> { "Scripts/A.py", "Scripts/B.forge" });
	CHECK(watcher.Poll().empty());

	std::filesystem::remove(folder / "Scripts" / "A.py", error);
	CHECK(watcher.Poll().empty());
	watcher.Stop();
	Save(folder / "Scripts" / "B.forge", "say 2\n");
	CHECK(watcher.Poll().empty());
}

TEST_CASE("Saving a Python script during play swaps in its new code and keeps the object's state")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "PythonHotReload";
	std::error_code error;
	std::filesystem::remove_all(assets, error);
	Save(assets / "Scripts" / "Steps.py", "STEP = 1\n");
	Save(assets / "Scripts" / "Counter.py", c_CounterV1);
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity entity = scene->CreateEntity("Counter");
	auto& script = entity.AddComponent<ScriptComponent>();
	script.ScriptPath = "Scripts/Counter.py";
	script.Fields["step"] = int64_t(2);
	scene->OnRuntimeStart();
	ScriptEngine::SetContextScene(scene.get());
	RunFrames(*scene, 3);
	const std::string instance = "bellows.find_entity('Counter').script";
	CHECK(Evaluate(instance + ".count") == "6");

	// New code: the count keeps its value, the Inspector value stays, on_reload runs.
	Save(assets / "Scripts" / "Counter.py", c_CounterV2);
	CHECK(ScriptEngine::ReloadScripts("Scripts/Counter.py"));
	CHECK(Evaluate(instance + ".count, " + instance + ".step, " + instance + ".reloads") == "(6, 2, 1)");
	RunFrames(*scene, 1);
	CHECK(Evaluate(instance + ".count") == "1006");

	// A mistake keeps the old code running and is reported.
	const uint32_t errors = ScriptEngine::GetErrorCount();
	Save(assets / "Scripts" / "Counter.py", "class Counter(:\n");
	CHECK_FALSE(ScriptEngine::ReloadScripts("Scripts/Counter.py"));
	CHECK(ScriptEngine::GetErrorCount() == errors + 1);
	CHECK(ScriptEngine::GetLastError().find("SyntaxError") != std::string::npos);
	RunFrames(*scene, 1);
	CHECK(Evaluate(instance + ".count") == "2006");

	// A helper module imported by the script reloads it too.
	Save(assets / "Scripts" / "Counter.py", c_CounterV2);
	Save(assets / "Scripts" / "Steps.py", "STEP = 3\n");
	CHECK(ScriptEngine::ReloadScripts("Scripts/Steps.py"));
	RunFrames(*scene, 1);
	CHECK(Evaluate(instance + ".count") == "5006");

	// Other files change nothing.
	CHECK(ScriptEngine::ReloadScripts("Scripts/Readme.txt"));
	scene->OnRuntimeStop();
	ScriptEngine::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("A reloaded Python script whose class was renamed keeps its old version")
{
	REQUIRE(ScriptEngine::IsInitialized());
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "PythonHotReloadRename";
	Save(assets / "Scripts" / "Spinner.py", "import bellows\n\nclass Spinner(bellows.Script):\n    def on_update(self, dt):\n        self.entity.name = 'v1'\n");
	AssetManager::SetAssetDirectory(assets);
	Ref<Scene> scene = CreateRef<Scene>();
	Entity entity = scene->CreateEntity("Spinner");
	entity.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Spinner.py";
	scene->OnRuntimeStart();
	RunFrames(*scene, 1);
	CHECK(entity.GetName() == "v1");

	Save(assets / "Scripts" / "Spinner.py", "import bellows\n\nclass Turner(bellows.Script):\n    def on_update(self, dt):\n        self.entity.name = 'v2'\n");
	CHECK_FALSE(ScriptEngine::ReloadScripts("Scripts/Spinner.py"));
	CHECK(ScriptEngine::GetLastError().find("Class 'Spinner' is no longer in the script") != std::string::npos);
	entity.SetName("reset");
	RunFrames(*scene, 1);
	CHECK(entity.GetName() == "v1");
	scene->OnRuntimeStop();
	AssetManager::SetAssetDirectory({});
}
