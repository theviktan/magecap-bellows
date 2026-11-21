#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneSerializer.h"
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

	// Kept: counts the goodbyes of entities that go, and the scenes loaded.
	const char* c_Keeper = R"PY(
import bellows

class Keeper(bellows.Script):
    def on_create(self):
        self.goodbyes = 0
        self.loads = []

    def on_event(self, name, *values):
        if name == "scene loaded":
            self.loads.append(values)
)PY";

	// Not kept: says goodbye to the keeper, and makes an entity while it goes.
	const char* c_Goner = R"PY(
import bellows

class Goner(bellows.Script):
    def on_destroy(self):
        bellows.find_entity("Keeper").script.goodbyes += 1
        bellows.create_entity("Made while leaving")
)PY";

	// Kept, in ForgeScript: hears the new scene too.
	const char* c_ForgeKeeper = R"FORGE(
when scene loaded with name, path
	set my name to "loaded {name} from {path}"
end
)FORGE";

	// The next level: a ground, a level entity, and a second copy of the kept player (same ID).
	Json MakeLevel(UUID playerID)
	{
		Scene level("Level 2");
		level.GetSettings().Gravity = { 0.0f, -3.0f, 0.0f };
		level.CreateEntity("Level Thing");
		Entity player = level.CreateEntityWithUUID(playerID, "Player copy in the level");
		level.CreateChildEntity(player, "Copy's child");
		return SceneSerializer::SerializeToJson(level);
	}

	struct SceneChange
	{
		Ref<Scene> Play = CreateRef<Scene>("Level 1");
		Entity Keeper, Goner, Player, Holder, Carried, ForgeKeeper;

		SceneChange()
		{
			REQUIRE(ScriptEngine::IsInitialized());
			ScriptEngine::ResetErrorCount();
			const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "SceneChange";
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Keeper.py", c_Keeper));
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Goner.py", c_Goner));
			REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "ForgeKeeper.forge", c_ForgeKeeper));
			AssetManager::SetAssetDirectory(assets);

			Keeper = Play->CreateEntity("Keeper");
			Keeper.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Keeper.py";
			Keeper.GetComponent<TagComponent>().KeepAcrossScenes = true;
			Goner = Play->CreateEntity("Goner");
			Goner.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Goner.py";
			ForgeKeeper = Play->CreateEntity("Forge keeper");
			ForgeKeeper.AddComponent<ScriptComponent>().ScriptPath = "Scripts/ForgeKeeper.forge";
			ForgeKeeper.GetComponent<TagComponent>().KeepAcrossScenes = true;

			// A falling player whose child comes with it.
			Player = Play->CreateEntity("Player");
			Player.GetComponent<TagComponent>().KeepAcrossScenes = true;
			Player.AddComponent<RigidBodyComponent>();
			Player.AddComponent<BoxColliderComponent>();
			Play->CreateChildEntity(Player, "Hat");

			// A kept entity under one that goes: it moves to the top, where it is in the world.
			Holder = Play->CreateEntity("Holder");
			Holder.GetComponent<TransformComponent>().Translation = { 5.0f, 0.0f, 0.0f };
			Carried = Play->CreateChildEntity(Holder, "Carried");
			Carried.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 0.0f };
			Carried.GetComponent<TagComponent>().KeepAcrossScenes = true;

			Play->OnRuntimeStart();
			ScriptEngine::SetContextScene(Play.get());
			for (int frame = 0; frame < 20; frame++)
				Play->OnUpdateRuntime(1.0f / 60.0f);
		}

		~SceneChange()
		{
			Play->OnRuntimeStop();
			ScriptEngine::SetContextScene(nullptr);
			AssetManager::SetAssetDirectory({});
			ScriptEngine::ResetErrorCount();
		}
	};

}

TEST_CASE("load_scene keeps entities marked Keep Across Scenes, with their scripts, children and physics")
{
	SceneChange test;
	Scene& scene = *test.Play;
	const UUID keeper = test.Keeper.GetUUID(), goner = test.Goner.GetUUID(), player = test.Player.GetUUID();
	const UUID carried = test.Carried.GetUUID(), holder = test.Holder.GetUUID(), forgeKeeper = test.ForgeKeeper.GetUUID();
	const uint64_t sceneID = scene.GetID();
	const float timeBefore = scene.GetTime();
	const glm::vec3 velocityBefore = scene.GetPhysicsWorld()->GetLinearVelocity(test.Player);
	REQUIRE(velocityBefore.y < -0.1f);
	const std::string keeperScript = Evaluate("id(bellows.find_entity('Keeper').script)");
	scene.SetGamePaused(true);

	REQUIRE(scene.ChangeScene(MakeLevel(player), "Scenes/Level2.bscene"));

	SUBCASE("What stays and what goes")
	{
		CHECK(scene.TryGetEntityByUUID(keeper));
		CHECK(scene.TryGetEntityByUUID(forgeKeeper));
		CHECK_FALSE(scene.TryGetEntityByUUID(goner));
		CHECK_FALSE(scene.TryGetEntityByUUID(holder));
		CHECK_FALSE(scene.FindEntityByName("Made while leaving"));
		CHECK(scene.FindEntityByName("Level Thing"));
		CHECK(scene.GetName() == "Level 2");
		CHECK(scene.GetSettings().Gravity.y == doctest::Approx(-3.0f));
	}

	SUBCASE("A kept entity's children come with it; a kept child of one that goes moves to the top")
	{
		Entity kept = scene.TryGetEntityByUUID(player);
		REQUIRE(kept.GetChildren().size() == 1);
		CHECK(kept.GetChildren()[0].GetName() == "Hat");
		Entity moved = scene.TryGetEntityByUUID(carried);
		CHECK_FALSE(moved.GetParent());
		CHECK(scene.GetWorldPosition(moved).x == doctest::Approx(6.0f));
		CHECK(scene.GetWorldPosition(moved).y == doctest::Approx(2.0f));
	}

	SUBCASE("The kept copy wins over the level's copy of the same entity")
	{
		CHECK(scene.TryGetEntityByUUID(player).GetName() == "Player");
		CHECK_FALSE(scene.FindEntityByName("Player copy in the level"));
		CHECK_FALSE(scene.FindEntityByName("Copy's child"));
	}

	SUBCASE("Kept scripts carry on, and every script hears the new scene")
	{
		const std::string script = "bellows.find_entity('Keeper').script";
		CHECK(Evaluate("id(" + script + ")") == keeperScript);
		CHECK(Evaluate(script + ".goodbyes") == "1");
		CHECK(Evaluate(script + ".loads") == "[('Level 2', 'Scenes/Level2.bscene')]");
		CHECK(scene.TryGetEntityByUUID(forgeKeeper).GetName() == "loaded Level 2 from Scenes/Level2.bscene");
		CHECK(ScriptEngine::GetErrorCount() == 0);
	}

	SUBCASE("Handles, physics, time and the pause")
	{
		CHECK(scene.GetID() == sceneID);
		CHECK(Evaluate("bellows.find_entity('Player').valid") == "True");
		const glm::vec3 velocityAfter = scene.GetPhysicsWorld()->GetLinearVelocity(scene.TryGetEntityByUUID(player));
		CHECK(velocityAfter.y == doctest::Approx(velocityBefore.y));
		CHECK_FALSE(scene.IsGamePaused());
		scene.OnUpdateRuntime(1.0f / 60.0f);
		CHECK(scene.GetTime() > timeBefore);
	}
}

TEST_CASE("ChangeScene refuses what isn't a scene, and a scene that isn't running")
{
	SceneChange test;
	CHECK_FALSE(test.Play->ChangeScene(Json::object(), "Scenes/Broken.bscene"));
	CHECK_FALSE(test.Play->ChangeScene(Json { { "Entities", 3 } }, "Scenes/Broken.bscene"));
	CHECK(test.Play->TryGetEntityByUUID(test.Goner.GetUUID()));

	Scene stopped;
	CHECK_FALSE(stopped.ChangeScene(MakeLevel(UUID()), "Scenes/Level2.bscene"));
	CHECK_FALSE(stopped.FindEntityByName("Level Thing"));
}

TEST_CASE("Keep Across Scenes is saved only when set, and survives copies and duplicates")
{
	Scene scene;
	Entity kept = scene.CreateEntity("Kept");
	kept.GetComponent<TagComponent>().KeepAcrossScenes = true;
	Entity plain = scene.CreateEntity("Plain");

	CHECK(SceneSerializer::SerializeEntity(kept).value("KeepAcrossScenes", false));
	CHECK_FALSE(SceneSerializer::SerializeEntity(plain).contains("KeepAcrossScenes"));

	Scene loaded;
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, SceneSerializer::SerializeToJson(scene)));
	CHECK(loaded.TryGetEntityByUUID(kept.GetUUID()).GetComponent<TagComponent>().KeepAcrossScenes);
	CHECK_FALSE(loaded.TryGetEntityByUUID(plain.GetUUID()).GetComponent<TagComponent>().KeepAcrossScenes);

	Ref<Scene> source = CreateRef<Scene>();
	Entity original = source->CreateEntity("Original");
	original.GetComponent<TagComponent>().KeepAcrossScenes = true;
	Ref<Scene> copy = Scene::Copy(source);
	CHECK(copy->TryGetEntityByUUID(original.GetUUID()).GetComponent<TagComponent>().KeepAcrossScenes);
	CHECK(source->DuplicateEntity(original).GetComponent<TagComponent>().KeepAcrossScenes);
}

TEST_CASE("A scene file without settings or a name gets a new scene's, not the old scene's")
{
	SceneChange test;
	test.Play->GetSettings().Gravity = { 0.0f, -1.0f, 0.0f };
	REQUIRE(test.Play->ChangeScene(Json { { "Entities", Json::array() } }, "Scenes/Bare.bscene"));
	CHECK(test.Play->GetSettings().Gravity.y == doctest::Approx(SceneSettings {}.Gravity.y));
	CHECK(test.Play->GetName() == "Untitled");
}

namespace {

	// Started in the old scene: the kept one's wait finishes in the new scene, the other's is cancelled.
	const char* c_KeptWaiter = R"FORGE(
when created
	wait 0.25 seconds
	set my name to "kept wait finished"
end
)FORGE";

	const char* c_LeftWaiter = R"FORGE(
when created
	wait 0.25 seconds
	set thing to find_entity("Level Thing")
	set thing.name to "the old scene's wait ran"
end
)FORGE";

}

TEST_CASE("A kept ForgeScript script's wait finishes in the new scene; one that goes is cancelled")
{
	REQUIRE(ScriptEngine::IsInitialized());
	ScriptEngine::ResetErrorCount();
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "SceneChangeWaits";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "KeptWaiter.forge", c_KeptWaiter));
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "LeftWaiter.forge", c_LeftWaiter));
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> scene = CreateRef<Scene>();
	Entity kept = scene->CreateEntity("Kept waiter");
	kept.AddComponent<ScriptComponent>().ScriptPath = "Scripts/KeptWaiter.forge";
	kept.GetComponent<TagComponent>().KeepAcrossScenes = true;
	scene->CreateEntity("Left waiter").AddComponent<ScriptComponent>().ScriptPath = "Scripts/LeftWaiter.forge";
	scene->OnRuntimeStart();
	ScriptEngine::SetContextScene(scene.get());
	scene->OnUpdateRuntime(1.0f / 60.0f);

	REQUIRE(scene->ChangeScene(MakeLevel(UUID()), "Scenes/Level2.bscene"));
	for (int frame = 0; frame < 30; frame++)
		scene->OnUpdateRuntime(1.0f / 60.0f);
	CHECK(kept.GetName() == "kept wait finished");
	CHECK(scene->FindEntityByName("Level Thing"));
	CHECK_FALSE(scene->FindEntityByName("the old scene's wait ran"));
	CHECK(ScriptEngine::GetErrorCount() == 0);

	scene->OnRuntimeStop();
	ScriptEngine::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
}
