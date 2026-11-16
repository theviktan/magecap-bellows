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

	const char* c_Listener = R"PY(
import bellows

class Listener(bellows.Script):
    def on_create(self):
        self.heard = []

    def on_event(self, name, *values):
        self.heard.append((name, values))
        if name == "loop":
            bellows.emit("loop")
)PY";

	const char* c_Sender = R"PY(
import bellows

class Sender(bellows.Script):
    def on_create(self):
        self.sent = False

    def on_update(self, dt):
        if not self.sent:
            self.sent = True
            bellows.emit("hello", 3, "text", bellows.Vec3(1, 2, 3), self.entity, [1, "x"], {"k": True}, None)
)PY";

	const char* c_Door = R"FORGE(
when hello with number, words, where, who, list, map, empty
	set my name to "forge {number} {words} {who.name} {list[2]} {map.k} {empty}"
	trigger echo with number + 1
end
)FORGE";

}

TEST_CASE("Python scripts send and receive events, across languages too")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "PythonEvents";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Listener.py", c_Listener));
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Sender.py", c_Sender));
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Door.forge", c_Door));
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity listener = scene->CreateEntity("Listener");
	listener.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Listener.py";
	Entity sender = scene->CreateEntity("Sender");
	sender.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Sender.py";
	Entity door = scene->CreateEntity("Door");
	door.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Door.forge";
	scene->OnRuntimeStart();
	ScriptEngine::SetContextScene(scene.get());
	scene->OnUpdateRuntime(1.0f / 60.0f);

	// Python got the values as Python values; ForgeScript heard the same event and answered.
	const std::string heard = "bellows.find_entity('Listener').script.heard";
	CHECK(Evaluate("len(" + heard + ")") == "2");
	CHECK(Evaluate(heard + "[0][0], " + heard + "[0][1][:3]") == "('hello', (3.0, 'text', Vec3(1, 2, 3)))");
	CHECK(Evaluate(heard + "[0][1][3] == bellows.find_entity('Sender'), " + heard + "[0][1][4:]") == "(True, ([1.0, 'x'], {'k': True}, None))");
	CHECK(Evaluate(heard + "[1]") == "('echo', (4.0,))");
	CHECK(door.GetName() == "forge 3 text Sender x true nothing");
	CHECK(ScriptEngine::GetErrorCount() == 0);

	// The sender's own on_event hears its events too, and values Python can't send are an error.
	CHECK(Evaluate("bellows.emit('direct', 1.5); " + heard + "[-1]") == "('direct', (1.5,))");
	std::string output;
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.emit('bad', object())", output));
	CHECK(output.find("not object") != std::string::npos);
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.emit('bad', {1: 2})", output));
	CHECK(output.find("need text keys") != std::string::npos);

	// A script that answers its own event forever stops with an error instead of overflowing the stack.
	ScriptEngine::ExecuteSource("bellows.emit('loop')", output);
	CHECK(ScriptEngine::GetErrorCount() > 0);
	CHECK(ScriptEngine::GetLastError().find("loop") != std::string::npos);

	scene->OnRuntimeStop();
	ScriptEngine::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
	ScriptEngine::ResetErrorCount();
}
