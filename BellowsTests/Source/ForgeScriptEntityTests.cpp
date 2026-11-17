#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <cmath>
#include <limits>

using namespace Bellows;

namespace {

	// A throwaway project folder with the given scripts (path -> source).
	void MakeForgeProject(const std::string& name, const std::vector<std::pair<std::string, std::string>>& scripts)
	{
		const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / name;
		for (const auto& [path, source] : scripts)
			REQUIRE(FileSystem::WriteTextFile(assets / path, source));
		AssetManager::SetAssetDirectory(assets);
	}

	void RunFrames(Scene& scene, int frames)
	{
		for (int i = 0; i < frames; i++)
			scene.OnUpdateRuntime(1.0f / 60.0f);
	}

	Entity AddScripted(Scene& scene, const std::string& name, const std::string& scriptPath)
	{
		Entity entity = scene.CreateEntity(name);
		entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath;
		return entity;
	}

}

TEST_CASE("ForgeScript scripts run their lifecycle events on entities")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeLifecycle", {
		{ "Scripts/Mover.forge",
			"set speed to 2\n"
			"set label to \"default\"\n"
			"set updates to 0\n"
			"\n"
			"when created\n"
			"\tset my name to \"Created:{label}\"\n"
			"\tcreate_entity(\"SpawnedByForge\")\n"
			"\tstart spawn_later()\n"
			"end\n"
			"\n"
			"function spawn_later()\n"
			"\twait 0.5 seconds\n"
			"\tcreate_entity(\"AfterWait\")\n"
			"end\n"
			"\n"
			"when update with delta\n"
			"\tadd 1 to updates\n"
			"\tself.translate(vector(speed * delta, 0, 0))\n"
			"end\n"
			"\n"
			"when destroyed\n"
			"\tcreate_entity(\"DestroyMarker {updates}\")\n"
			"end\n" },
		{ "Scripts/Listener.forge",
			"when created\n"
			"\ttrigger listener ready with my name\n"
			"end\n"
			"when listener ready with who\n"
			"\tset my name to \"Heard \" + who\n"
			"end\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity mover = AddScripted(*scene, "Mover", "Scripts/Mover.forge");
	ScriptComponent& script = mover.GetComponent<ScriptComponent>();
	script.Fields["speed"] = 6.0;
	script.Fields["label"] = std::string("custom");
	Entity listener = AddScripted(*scene, "Listener", "Scripts/Listener.forge");

	scene->OnRuntimeStart();
	CHECK_EQ(mover.GetName(), std::string("Created:custom"));
	CHECK(scene->FindEntityByName("SpawnedByForge"));
	// 'trigger' reaches every script, including the one that sent it.
	CHECK_EQ(listener.GetName(), std::string("Heard Listener"));
	CHECK_FALSE(scene->FindEntityByName("AfterWait"));

	RunFrames(*scene, 60);
	CHECK(scene->FindEntityByName("AfterWait"));
	CHECK(mover.GetTransform().Translation.x == doctest::Approx(6.0f).epsilon(0.01));

	scene->DestroyEntity(mover);
	RunFrames(*scene, 1);
	CHECK(scene->FindEntityByName("DestroyMarker 60"));
	scene->OnRuntimeStop();
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript scripts get destroyed when play stops and reload after an edit")
{
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeReload", { { "Scripts/Counter.forge",
		"when created\n\tset my name to \"first\"\nend\n"
		"when destroyed\n\tcreate_entity(\"Stopped\")\nend\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity entity = AddScripted(*scene, "Counter", "Scripts/Counter.forge");
	scene->OnRuntimeStart();
	CHECK_EQ(entity.GetName(), std::string("first"));
	scene->OnRuntimeStop();
	CHECK(scene->FindEntityByName("Stopped"));

	// Each play session reads the script again, so edits made while stopped take effect.
	REQUIRE(FileSystem::WriteTextFile(AssetManager::GetAbsolutePath("Scripts/Counter.forge"),
		"when created\n\tset my name to \"second\"\nend\n"));
	scene->OnRuntimeStart();
	CHECK_EQ(entity.GetName(), std::string("second"));
	scene->OnRuntimeStop();
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript errors are reported with their line and never crash the engine")
{
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeErrors", {
		{ "Scripts/Broken.forge", "when created\n\tset x to\nend\n" },
		{ "Scripts/Faulty.forge",
			"set count to 0\n"
			"when update with delta\n"
			"\tadd 1 to count\n"
			"\tsay nothing + 1\n"
			"end\n" },
		{ "Scripts/Forever.forge", "when created\n\trepeat forever\n\t\tset busy to true\n\tend\nend\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity broken = AddScripted(*scene, "Broken", "Scripts/Broken.forge");
	Entity faulty = AddScripted(*scene, "Faulty", "Scripts/Faulty.forge");
	AddScripted(*scene, "Forever", "Scripts/Forever.forge");
	AddScripted(*scene, "Missing", "Scripts/Missing.forge");

	scene->OnRuntimeStart();
	// The syntax mistake, the endless loop and the missing file.
	CHECK_EQ(ScriptEngine::GetErrorCount(), 3u);

	RunFrames(*scene, 30);
	// The failing update stops after ten errors, so the console stays readable.
	CHECK_EQ(ScriptEngine::GetErrorCount(), 13u);
	CHECK(ScriptEngine::GetLastError().find("Faulty.forge") != std::string::npos);
	CHECK(ScriptEngine::GetLastError().find("line 4") != std::string::npos);
	scene->OnRuntimeStop();
	CHECK(broken);
	CHECK(faulty);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript script info lists top-level settings as Inspector fields")
{
	MakeForgeProject("ForgeInfo", {
		{ "Scripts/Player.forge",
			"set speed to 4.5\n"
			"set lives to 3\n"
			"set title to \"Hero\"\n"
			"set flying to no\n"
			"set spawn to vector(1, 2, 3)\n"
			"set tint to [1, 0, 0]\n"
			"set _hidden to 1\n"
			"when update with delta\n\tset local to 1\nend\n" },
		{ "Scripts/Broken.forge", "set to 5\n" } });

	ScriptClassInfo info;
	REQUIRE(ScriptEngine::GetClassInfo("Scripts/Player.forge", "", info));
	CHECK_EQ(info.ClassName, std::string("Player"));
	REQUIRE_EQ(info.Fields.size(), static_cast<size_t>(6));
	CHECK_EQ(info.Fields[0].Name, std::string("speed"));
	CHECK_EQ(std::get<double>(info.Fields[0].DefaultValue), 4.5);
	CHECK_EQ(std::get<int64_t>(info.Fields[1].DefaultValue), 3);
	CHECK_EQ(std::get<std::string>(info.Fields[2].DefaultValue), std::string("Hero"));
	CHECK_FALSE(std::get<bool>(info.Fields[3].DefaultValue));
	CHECK_EQ(std::get<glm::vec3>(info.Fields[4].DefaultValue), glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK_EQ(std::get<glm::vec4>(info.Fields[5].DefaultValue), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

	CHECK_FALSE(ScriptEngine::GetClassInfo("Scripts/Broken.forge", "", info));
	CHECK(ScriptEngine::GetLastError().find("line 1") != std::string::npos);
	CHECK_FALSE(ScriptEngine::GetClassInfo("Scripts/Nowhere.forge", "", info));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript scripts receive physics events")
{
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgePhysics", { { "Scripts/Ball.forge",
		"set hits to []\n"
		"set steps to 0\n"
		"when physics update with delta\n\tadd 1 to steps\nend\n"
		"when collided with other\n\tadd other.name to hits\nend\n"
		"when destroyed\n\tcreate_entity(\"Hits {hits} after {steps}\")\nend\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity ground = scene->CreateEntity("Ground");
	ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
	ground.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	ground.AddComponent<BoxColliderComponent>().HalfExtents = { 10.0f, 0.5f, 10.0f };
	Entity ball = AddScripted(*scene, "Ball", "Scripts/Ball.forge");
	ball.GetTransform().Translation = { 0.0f, 2.0f, 0.0f };
	ball.AddComponent<RigidBodyComponent>();
	ball.AddComponent<SphereColliderComponent>();

	scene->OnRuntimeStart();
	RunFrames(*scene, 120);
	scene->OnRuntimeStop();

	bool found = false;
	for (entt::entity handle : scene->GetAllEntitiesWith<TagComponent>())
	{
		const std::string name = Entity(handle, scene.get()).GetName();
		if (name.rfind("Hits [\"Ground\"] after ", 0) == 0)
			found = name != "Hits [\"Ground\"] after 0";
	}
	CHECK(found);
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript sees frame timing as numbers")
{
	// Regression: frame() returned an unsigned 64-bit value, which ForgeScript shows as text (like IDs).
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeTiming", { { "Scripts/Clock.forge",
		"when update with delta\n"
		"\tif frame() is above 0 and time() is above 0 and delta_time() is above 0 then set my name to \"ticking\" end\n"
		"end\n" } });

	Ref<Scene> scene = CreateRef<Scene>();
	Entity clock = AddScripted(*scene, "Clock", "Scripts/Clock.forge");
	scene->OnRuntimeStart();
	RunFrames(*scene, 3);
	scene->OnRuntimeStop();
	CHECK_EQ(clock.GetName(), std::string("ticking"));
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Script field overrides of another number type are shown and applied as the field's type")
{
	// Regression: set_field("walk_speed", 10.0) on "set walk_speed to 4" ran with 10, but the Inspector showed 4.
	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeFieldTypes", { { "Scripts/Walker.forge",
		"set walk_speed to 4\n"
		"set turn_speed to 90.0\n"
		"when created\n\tset my name to \"{walk_speed} {turn_speed}\"\nend\n" } });

	ScriptClassInfo info;
	REQUIRE(ScriptEngine::GetClassInfo("Scripts/Walker.forge", "", info));
	REQUIRE_EQ(info.Fields.size(), static_cast<size_t>(2));

	ScriptFieldValue shown;
	REQUIRE(ConvertScriptFieldValue(ScriptFieldValue(10.0), info.Fields[0].DefaultValue, shown));
	CHECK_EQ(std::get<int64_t>(shown), 10);
	REQUIRE(ConvertScriptFieldValue(ScriptFieldValue(2.6), info.Fields[0].DefaultValue, shown));
	CHECK_EQ(std::get<int64_t>(shown), 3);
	REQUIRE(ConvertScriptFieldValue(ScriptFieldValue(int64_t(45)), info.Fields[1].DefaultValue, shown));
	CHECK_EQ(std::get<double>(shown), 45.0);
	CHECK_FALSE(ConvertScriptFieldValue(ScriptFieldValue(std::string("fast")), info.Fields[0].DefaultValue, shown));
	CHECK_FALSE(ConvertScriptFieldValue(ScriptFieldValue(true), info.Fields[1].DefaultValue, shown));

	Ref<Scene> scene = CreateRef<Scene>();
	Entity walker = AddScripted(*scene, "Walker", "Scripts/Walker.forge");
	ScriptComponent& script = walker.GetComponent<ScriptComponent>();
	script.Fields["walk_speed"] = 10.0;
	script.Fields["turn_speed"] = int64_t(45);
	scene->OnRuntimeStart();
	CHECK_EQ(walker.GetName(), std::string("10 45"));
	scene->OnRuntimeStop();
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript reads engine floats as the numbers they were written as")
{
	// The float 0.1 is 0.100000001490116 exactly; scripts see the number its shortest text means.
	CHECK_EQ(Scripting::ForgeScriptNumber(0.1f), 0.1);
	CHECK_EQ(Scripting::ForgeScriptNumber(1.0f / 60.0f), 0.016666668);
	CHECK_EQ(Scripting::ForgeScriptNumber(-2.5f), -2.5);
	CHECK_EQ(Scripting::ForgeScriptNumber(16777216.0f), 16777216.0);
	CHECK(std::isinf(Scripting::ForgeScriptNumber(std::numeric_limits<float>::infinity())));
	CHECK(std::isnan(Scripting::ForgeScriptNumber(std::numeric_limits<float>::quiet_NaN())));
	// Turned back into a float, it is the same float, for every kind of float.
	for (float value : { 0.1f, 1.0f / 3.0f, 3.14159265f, 1e-38f, 1e-45f, 3.4e38f, -123.456f, 0.0f, -0.0f })
		CHECK_EQ(static_cast<float>(Scripting::ForgeScriptNumber(value)), value);

	ScriptEngine::ResetErrorCount();
	MakeForgeProject("ForgeFloats", { { "Scripts/Floats.forge",
		"when created\n"
		"\tset my position to vector(0.1, 0.2, 0.3)\n"
		"end\n"
		"when update with delta\n"
		"\tset my name to \"{my position} {delta} {my position.x is 0.1}\"\n"
		"end\n" } });
	Ref<Scene> scene = CreateRef<Scene>();
	Entity floats = AddScripted(*scene, "Floats", "Scripts/Floats.forge");
	scene->OnRuntimeStart();
	RunFrames(*scene, 1);
	scene->OnRuntimeStop();
	CHECK_EQ(floats.GetName(), std::string("(0.1, 0.2, 0.3) 0.016666668 true"));
	CHECK_EQ(ScriptEngine::GetErrorCount(), 0u);
	AssetManager::SetAssetDirectory({});
}
