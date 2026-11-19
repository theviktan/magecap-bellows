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

namespace {

	// A Python script on a button: it writes down that it ran first.
	const char* c_PythonButton = R"PY(
import bellows

class Button(bellows.Script):
    def on_event(self, name, *values):
        log = bellows.find_entity("Log").children[0]
        log.name += f"python {self.entity.name} {name} {len(values)}; "
)PY";

	// A ForgeScript script on the menu above it: it hears its buttons after their own scripts.
	const char* c_ForgeMenu = R"FORGE(
when "start" is clicked
	set log to find_entity("Log").children[1]
	set log.name to log.name + "forge start; "
end

when clicked with widget
	set log to find_entity("Log").children[1]
	set who to "nothing"
	if widget then set who to widget.name end
	set log.name to log.name + "forge any {who}; "
end

when "Volume" is changed with value, widget
	set log to find_entity("Log").children[1]
	set log.name to log.name + "forge volume {value} from {widget.name}; "
end
)FORGE";

}

TEST_CASE("UI events reach a widget's own script first, then its parents', across languages")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "UIEvents";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Button.py", c_PythonButton));
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Menu.forge", c_ForgeMenu));
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	// The scripts write what ran into the name of the Log's child, in order.
	Entity log = scene->CreateEntity("Lines");
	scene->SetParent(log, scene->CreateEntity("Log"));
	Entity menu = scene->CreateEntity("Menu");
	menu.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Menu.forge";
	Entity start = scene->CreateEntity("Start");
	scene->SetParent(start, menu);
	start.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Button.py";
	Entity volume = scene->CreateEntity("Volume");
	scene->SetParent(volume, menu);
	scene->OnRuntimeStart();
	ScriptEngine::SetContextScene(scene.get());
	scene->OnUpdateRuntime(1.0f / 60.0f);

	using namespace Scripting;
	log.GetComponent<TagComponent>().Tag = "";
	EmitScriptEventFrom(ScriptEntity(start), "clicked", { ScriptValue(ScriptEntity(start)) });
	CHECK_EQ(log.GetName(), std::string("python Start clicked 1; forge start; forge any Start; "));

	// The widget comes last: the value first, then who raised it. A widget without a script still works.
	log.GetComponent<TagComponent>().Tag = "";
	EmitScriptEventFrom(ScriptEntity(volume), "changed", { ScriptValue(0.25), ScriptValue(ScriptEntity(volume)) });
	CHECK_EQ(log.GetName(), std::string("forge volume 0.25 from Volume; "));

	// A broadcast from a script never runs a handler written for one widget.
	log.GetComponent<TagComponent>().Tag = "";
	EmitScriptEvent("clicked", {});
	CHECK_EQ(log.GetName(), std::string("python Start clicked 0; forge any nothing; "));
	CHECK(ScriptEngine::GetErrorCount() == 0);

	scene->OnRuntimeStop();
	ScriptEngine::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
	ScriptEngine::ResetErrorCount();
}
