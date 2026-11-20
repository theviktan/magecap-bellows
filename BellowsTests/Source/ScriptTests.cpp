#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

using namespace Bellows;

namespace {

	// A throwaway project folder with the given scripts (path -> source).
	std::filesystem::path MakeScriptProject(const std::string& name, const std::vector<std::pair<std::string, std::string>>& scripts)
	{
		const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / name;
		for (const auto& [path, source] : scripts)
			REQUIRE(FileSystem::WriteTextFile(assets / path, source));
		AssetManager::SetAssetDirectory(assets);
		return assets;
	}

	void RunFrames(Scene& scene, int frames)
	{
		for (int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(1.0f / 60.0f);
	}

}

TEST_CASE("Scripts run their lifecycle callbacks")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	MakeScriptProject("ScriptLifecycle", { { "Scripts/Mover.py", R"PY(
import bellows

class Mover(bellows.Script):
    speed: float = 2.0
    label: str = "default"

    def on_create(self):
        self.updates = 0
        self.entity.name = "Created:" + self.label
        bellows.create_entity("SpawnedByScript")

    def on_update(self, dt):
        self.updates += 1
        self.entity.translate(bellows.Vec3(self.speed * dt, 0.0, 0.0))

    def on_destroy(self):
        marker = bellows.create_entity("DestroyMarker")
)PY" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity entity = scene->CreateEntity("Mover");
	auto& script = entity.AddComponent<ScriptComponent>();
	script.ScriptPath = "Scripts/Mover.py";
	script.Fields["speed"] = 6.0;
	script.Fields["label"] = std::string("custom");

	scene->OnRuntimeStart();
	CHECK(entity.GetName() == "Created:custom");
	CHECK(scene->FindEntityByName("SpawnedByScript"));
	RunFrames(*scene, 60);
	CHECK(entity.GetTransform().Translation.x == doctest::Approx(6.0f).epsilon(0.01));

	const UUID uuid = entity.GetUUID();
	void* instance = ScriptEngine::GetScriptInstance(uuid);
	REQUIRE(instance != nullptr);

	scene->DestroyEntity(entity);
	RunFrames(*scene, 1);
	CHECK(scene->FindEntityByName("DestroyMarker"));
	CHECK(ScriptEngine::GetScriptInstance(uuid) == nullptr);
	scene->OnRuntimeStop();
	CHECK(ScriptEngine::GetErrorCount() == 0);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Script exceptions are logged and never crash the engine")
{
	ScriptEngine::ResetErrorCount();
	MakeScriptProject("ScriptErrors", {
		{ "Scripts/Broken.py", R"PY(
import bellows

class Broken(bellows.Script):
    def on_update(self, dt):
        raise ValueError("intentional failure")
)PY" },
		{ "Scripts/Syntax.py", "import bellows\nclass Bad(bellows.Script)\n    pass\n" },
		{ "Scripts/NoClass.py", "x = 1\n" },
	});

	Ref<Scene> scene = CreateRef<Scene>();
	scene->CreateEntity("Broken").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Broken.py";
	scene->CreateEntity("Syntax").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Syntax.py";
	scene->CreateEntity("NoClass").AddComponent<ScriptComponent>().ScriptPath = "Scripts/NoClass.py";
	scene->CreateEntity("Missing").AddComponent<ScriptComponent>().ScriptPath = "Scripts/DoesNotExist.py";

	scene->OnRuntimeStart();
	RunFrames(*scene, 30);
	// 3 load failures + the failing update, which is disabled after 10 errors.
	CHECK(ScriptEngine::GetErrorCount() == 13);
	CHECK(ScriptEngine::GetLastError().find("intentional failure") != std::string::npos);
	scene->OnRuntimeStop();
	ScriptEngine::ResetErrorCount();
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Script class info exposes annotated fields")
{
	MakeScriptProject("ScriptFields", { { "Scripts/Fields.py", R"PY(
import bellows

class Base(bellows.Script):
    health: int = 100

class Fields(Base):
    speed: float = 3.5
    enabled: bool = True
    title: str = "Player"
    offset: bellows.Vec3 = bellows.Vec3(1, 2, 3)
    tint: bellows.Vec4 = bellows.Vec4(1, 0, 0, 1)
    _private: float = 1.0
    untyped = 5
)PY" } });

	ScriptClassInfo info;
	const bool found = ScriptEngine::GetClassInfo("Scripts/Fields.py", "Fields", info);
	INFO(ScriptEngine::GetLastError());
	REQUIRE(found);
	CHECK(info.ClassName == "Fields");
	std::map<std::string, ScriptFieldValue> fields;
	for (const ScriptFieldInfo& field : info.Fields)
		fields[field.Name] = field.DefaultValue;
	CHECK(fields.size() == 6);
	CHECK(std::get<int64_t>(fields.at("health")) == 100);
	CHECK(std::get<double>(fields.at("speed")) == doctest::Approx(3.5));
	CHECK(std::get<bool>(fields.at("enabled")));
	CHECK(std::get<std::string>(fields.at("title")) == "Player");
	CHECK(std::get<glm::vec3>(fields.at("offset")) == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(std::get<glm::vec4>(fields.at("tint")) == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
	CHECK_FALSE(fields.contains("_private"));

	CHECK_FALSE(ScriptEngine::GetClassInfo("Scripts/Missing.py", "", info));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Scripts receive input, physics callbacks and can raycast")
{
	ScriptEngine::ResetErrorCount();
	MakeScriptProject("ScriptPhysics", { { "Scripts/Ball.py", R"PY(
import bellows

class Ball(bellows.Script):
    def on_create(self):
        self.hits = []
        self.jumped = False

    def on_update(self, dt):
        if bellows.is_key_pressed(bellows.Key.Space) and not self.jumped:
            self.jumped = True
            self.entity.get_component("RigidBody").add_impulse((0.0, 5.0, 0.0))

    def on_collision_enter(self, other):
        self.hits.append(other.name)
)PY" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity ground = scene->CreateEntity("Ground");
	ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
	ground.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	ground.AddComponent<BoxColliderComponent>().HalfExtents = { 10.0f, 0.5f, 10.0f };
	Entity ball = scene->CreateEntity("Ball");
	ball.GetTransform().Translation = { 0.0f, 2.0f, 0.0f };
	ball.AddComponent<RigidBodyComponent>();
	ball.AddComponent<SphereColliderComponent>();
	ball.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Ball.py";

	scene->OnRuntimeStart();
	RunFrames(*scene, 120);

	std::string output;
	ScriptEngine::SetContextScene(scene.get());
	REQUIRE(ScriptEngine::ExecuteSource("bellows.find_entity('Ball').script.hits", output));
	CHECK(output.find("Ground") != std::string::npos);

	Input::BeginFrame();
	Input::InjectKey(KeyCode::Space, true);
	scene->OnUpdateRuntime(1.0f / 60.0f);
	Input::InjectKey(KeyCode::Space, false);
	Input::BeginFrame();
	RunFrames(*scene, 10);
	CHECK(ball.GetTransform().Translation.y > 0.8f);

	REQUIRE(ScriptEngine::ExecuteSource("hit = bellows.raycast((0, 10, 3), (0, -1, 0))\nprint(hit.entity.name, abs(round(hit.point.y, 3)))", output));
	INFO(output);
	CHECK(output.find("Ground 0.0") != std::string::npos);

	scene->OnRuntimeStop();
	CHECK(ScriptEngine::GetErrorCount() == 0);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ExecuteSource evaluates expressions and reports exceptions")
{
	std::string output;
	CHECK(ScriptEngine::ExecuteSource("1 + 2", output));
	CHECK(output == "3\n");
	CHECK(ScriptEngine::ExecuteSource("print('hello')", output));
	CHECK(output == "hello\n");
	CHECK(ScriptEngine::ExecuteSource("value = bellows.Vec3(1, 2, 3) * 2\nprint(value)", output));
	CHECK(output == "Vec3(2, 4, 6)\n");
	// Like the Python prompt, a trailing expression after statements prints its value.
	CHECK(ScriptEngine::ExecuteSource("count = 2\nfor i in range(3):\n    count += i\ncount * 10", output));
	CHECK(output == "50\n");
	CHECK(ScriptEngine::ExecuteSource("unused = 1", output));
	CHECK(output.empty());
	CHECK_FALSE(ScriptEngine::ExecuteSource("raise RuntimeError('boom')", output));
	CHECK(output.find("boom") != std::string::npos);
	CHECK_FALSE(ScriptEngine::ExecuteSource("def broken(:", output));
	CHECK(output.find("SyntaxError") != std::string::npos);
}

TEST_CASE("ExecuteFile gives the script its own path in __file__")
{
	const std::filesystem::path script = std::filesystem::absolute(BellowsTests::GetTestOutputDirectory() / "FileScript" / "where.py");
	REQUIRE(FileSystem::WriteTextFile(script, "location = __file__\nprint(location)"));
	std::string output;
	REQUIRE(ScriptEngine::ExecuteFile(script, output));
	CHECK(output == script.string() + "\n");
	// Afterwards the prompt's globals are back to normal: no stale __file__.
	REQUIRE(ScriptEngine::ExecuteSource("'__file__' in globals()", output));
	CHECK(output == "False\n");
}

TEST_CASE("Stale entity handles fail safely")
{
	Ref<Scene> scene = CreateRef<Scene>();
	ScriptEngine::SetContextScene(scene.get());
	std::string output;
	REQUIRE(ScriptEngine::ExecuteSource("stale = bellows.create_entity('Temp')", output));
	REQUIRE(ScriptEngine::ExecuteSource("stale.valid", output));
	CHECK(output == "True\n");
	scene->DestroyEntity(scene->FindEntityByName("Temp"));
	REQUIRE(ScriptEngine::ExecuteSource("stale.valid", output));
	CHECK(output == "False\n");
	CHECK_FALSE(ScriptEngine::ExecuteSource("stale.name", output));
	CHECK(output.find("no longer exists") != std::string::npos);

	scene.reset();
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.create_entity('X')", output));
	CHECK(output.find("No scene is active") != std::string::npos);
}

TEST_CASE("The script context forgets a scene that was deleted while active")
{
	// Regression: checking whether the context scene still existed used to read the deleted scene.
	Ref<Scene> scene = CreateRef<Scene>();
	ScriptEngine::SetContextScene(scene.get());
	CHECK_EQ(Scripting::GetContextScene(), scene.get());
	scene.reset();
	CHECK_EQ(Scripting::GetContextScene(), static_cast<Scene*>(nullptr));
	CHECK_THROWS_AS(Scripting::RequireContextScene(), std::runtime_error);
	ScriptEngine::SetContextScene(nullptr);
}

TEST_CASE("A game's files can't stand in for the standard library or the engine's modules")
{
	REQUIRE(ScriptEngine::IsInitialized());
	// colorsys: a standard module nothing has imported yet, so the import below finds it by the path.
	const std::filesystem::path assets = MakeScriptProject("ScriptImportPath", {
		{ "colorsys.py", "FROM_THE_GAME = True\n" },
		{ "bellows.py", "FROM_THE_GAME = True\n" },
		{ "Scripts/Helper.py", "VALUE = 42\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	scene->OnRuntimeStart();
	std::string output;
	REQUIRE(ScriptEngine::ExecuteSource(
		"import sys, colorsys, bellows\n"
		"from Scripts import Helper\n"
		"print(hasattr(colorsys, 'rgb_to_hsv'), hasattr(colorsys, 'FROM_THE_GAME'), hasattr(bellows, 'FROM_THE_GAME'), Helper.VALUE)\n"
		"print(sys.path[-1])\n", output));
	scene->OnRuntimeStop();
	AssetManager::SetAssetDirectory({});

	// The standard module and the engine's own, while the game's own packages still import.
	CHECK(output.find("True False False 42") != std::string::npos);
	CHECK(output.find(assets.string()) != std::string::npos);
}
