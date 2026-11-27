#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneGroups.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <stdexcept>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	std::vector<std::string> Names(const std::vector<Entity>& entities)
	{
		std::vector<std::string> names;
		for (Entity entity : entities)
			names.push_back(entity.GetName());
		return names;
	}

	std::string WarningsSince(uint64_t sequence)
	{
		std::string warnings;
		for (const LogMessage& message : Log::GetMessagesSince(sequence))
		{
			if (message.Level == LogLevel::Warn)
				warnings += message.Text + "\n";
		}
		return warnings;
	}

}

TEST_CASE("Groups follow one set of rules for names, notes and icons")
{
	Scene scene("Groups");
	SceneGroup& docking = scene.CreateGroup("  Docking  ");
	CHECK(docking.Name == "Docking");
	CHECK(docking.Icon == "folder");

	// Names are unique and found in any letter case.
	CHECK(scene.FindGroup("docking") == &scene.GetGroups()[0]);
	CHECK(scene.FindGroup(" DOCKING ") == &scene.GetGroups()[0]);
	CHECK(scene.FindGroup("Ships") == nullptr);
	CHECK_THROWS_AS(scene.CreateGroup("DOCKING"), std::invalid_argument);
	CHECK_THROWS_AS(scene.CreateGroup("   "), std::invalid_argument);
	CHECK_THROWS_AS(scene.CreateGroup("Two\nlines"), std::invalid_argument);
	CHECK_THROWS_AS(scene.CreateGroup(std::string(65, 'x')), std::invalid_argument);
	CHECK_NOTHROW(scene.CreateGroup(std::string(64, 'x')));

	const UUID id = scene.GetGroups()[0].ID;
	CHECK_NOTHROW(scene.RenameGroup(id, "DOCKING"));
	CHECK(scene.FindGroup(id)->Name == "DOCKING");
	CHECK_THROWS_AS(scene.RenameGroup(id, std::string(64, 'X')), std::invalid_argument);
	CHECK_THROWS_AS(scene.RenameGroup(UUID(12345), "Gone"), std::invalid_argument);

	CHECK_FALSE(SceneGroups::CheckNote(std::string(200, 'n')));
	CHECK(SceneGroups::CheckNote(std::string(201, 'n')));
	CHECK(SceneGroups::IsIcon("anchor"));
	CHECK_FALSE(SceneGroups::IsIcon("Anchor"));
	CHECK(SceneGroups::GetIcons().front() == "folder");
}

TEST_CASE("Top-level entities are filed under groups, and their children follow them")
{
	Scene scene("Station");
	const UUID docking = scene.CreateGroup("Docking").ID;
	const UUID ships = scene.CreateGroup("Ships").ID;
	Entity bayA = scene.CreateEntity("Bay A");
	Entity arm = scene.CreateChildEntity(bayA, "Arm");
	Entity bayB = scene.CreateEntity("Bay B");
	Entity drone = scene.CreateEntity("Drone");

	CHECK(scene.SetEntityGroup(bayA, docking));
	CHECK(scene.SetEntityGroup(bayB, docking));
	CHECK(scene.SetEntityGroup(drone, ships));
	CHECK_FALSE(scene.SetEntityGroup(arm, ships));			// A child follows its parent
	CHECK_FALSE(scene.SetEntityGroup(drone, UUID(777)));	// No such group
	CHECK(scene.GetEntityGroup(arm) == docking);
	CHECK(scene.GetEntityGroup(drone) == ships);

	CHECK(Names(scene.GetGroupEntities(docking)) == std::vector<std::string>{ "Bay A", "Bay B" });
	CHECK(Names(scene.GetGroupEntities(docking, true)) == std::vector<std::string>{ "Bay A", "Arm", "Bay B" });
	CHECK(scene.GetGroupEntities(UUID(777)).empty());

	SUBCASE("A child moved under another parent takes that parent's group")
	{
		CHECK(scene.SetParent(drone, bayB, false));
		CHECK(scene.GetEntityGroup(drone) == docking);
		CHECK(drone.GetComponent<TagComponent>().Group == UUID(0));
	}

	SUBCASE("A child moved to the top stays in the group it was in")
	{
		scene.Unparent(arm, false);
		CHECK(scene.GetEntityGroup(arm) == docking);
		CHECK(Names(scene.GetGroupEntities(docking)) == std::vector<std::string>{ "Bay A", "Bay B", "Arm" });
	}

	SUBCASE("A copy of a top-level entity is in the same group")
	{
		Entity copy = scene.DuplicateEntity(bayA);
		CHECK(scene.GetEntityGroup(copy) == docking);
		CHECK(scene.GetEntityGroup(copy.GetChildren()[0]) == docking);
	}

	SUBCASE("Deleting a group leaves its entities in place, in no group")
	{
		CHECK(scene.DeleteGroup(docking));
		CHECK_FALSE(scene.DeleteGroup(docking));
		CHECK(scene.GetEntityCount() == 4);
		CHECK(scene.GetEntityGroup(bayA) == UUID(0));
		CHECK(bayA.GetComponent<TagComponent>().Group == UUID(0));
		CHECK(scene.GetEntityGroup(drone) == ships);
	}

	SUBCASE("Groups can be reordered")
	{
		scene.MoveGroup(ships, 0);
		CHECK(scene.GetGroups()[0].Name == "Ships");
		scene.MoveGroup(ships, 99);
		CHECK(scene.GetGroups()[1].Name == "Ships");
	}

	SUBCASE("Play mode's copy has the same groups")
	{
		Ref<Scene> source = CreateRef<Scene>("Source");
		const UUID group = source->CreateGroup("Lights").ID;
		Entity lamp = source->CreateEntity("Lamp");
		source->SetEntityGroup(lamp, group);
		Ref<Scene> copy = Scene::Copy(source);
		REQUIRE(copy->GetGroups().size() == 1);
		CHECK(copy->GetGroups()[0].ID == group);
		CHECK(copy->GetEntityGroup(copy->TryGetEntityByUUID(lamp.GetUUID())) == group);
	}
}

TEST_CASE("Groups are saved with the scene, and damaged groups load cleaned with a warning")
{
	Scene scene("Saved Groups");
	SceneGroup& docking = scene.CreateGroup("Docking");
	docking.Icon = "anchor";
	docking.Color = { 0.25f, 0.8f, 0.5f };
	docking.Note = "Where ships dock";
	const UUID dockingID = docking.ID;
	scene.CreateGroup("Empty");
	Entity bay = scene.CreateEntity("Bay");
	scene.CreateChildEntity(bay, "Arm");
	scene.SetEntityGroup(bay, dockingID);
	scene.CreateEntity("Loose");

	const Json json = SceneSerializer::SerializeToJson(scene);
	const std::string text = json.dump();
	CHECK(text.find("\"Group\":" + std::to_string(static_cast<uint64_t>(dockingID))) != std::string::npos);
	CHECK(json["Entities"][1].contains("Group") == false);	// The child follows its parent

	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	REQUIRE(loaded.GetGroups().size() == 2);
	const SceneGroup& group = loaded.GetGroups()[0];
	CHECK(group.ID == dockingID);
	CHECK(group.Name == "Docking");
	CHECK(group.Icon == "anchor");
	CHECK(group.Color.y == doctest::Approx(0.8f));
	CHECK(group.Note == "Where ships dock");
	CHECK(loaded.GetGroups()[1].Note.empty());
	CHECK(loaded.GetEntityGroup(loaded.FindEntityByName("Arm")) == dockingID);
	CHECK(loaded.GetEntityGroup(loaded.FindEntityByName("Loose")) == UUID(0));

	// A scene without groups writes no list.
	Scene plain("Plain");
	plain.CreateEntity("Thing");
	CHECK_FALSE(SceneSerializer::SerializeToJson(plain).contains("Groups"));

	const Json damaged = Json::parse(R"({ "Version": 1,
		"Groups": [
			{ "ID": 1, "Name": " Lights ", "Icon": "no-such-icon", "Color": [2, -1, 0.5] },
			{ "ID": 2, "Name": "LIGHTS" },
			{ "ID": 1, "Name": "Again" },
			{ "ID": 0, "Name": "No ID" },
			{ "ID": 3, "Name": "" },
			{ "ID": 4, "Name": "Audio", "Color": "red" } ],
		"Entities": [
			{ "ID": 21, "Name": "Lamp", "Group": 1 },
			{ "ID": 22, "Name": "Speaker", "Group": 2 },
			{ "ID": 23, "Name": "Bulb", "Parent": 21, "Group": 4 } ] })");
	const uint64_t before = Log::GetLatestSequence();
	Scene cleaned("Cleaned");
	REQUIRE(SceneSerializer::DeserializeFromJson(cleaned, damaged));
	REQUIRE(cleaned.GetGroups().size() == 2);
	CHECK(cleaned.GetGroups()[0].Name == "Lights");
	CHECK(cleaned.GetGroups()[0].Icon == "folder");
	CHECK(cleaned.GetGroups()[0].Color == glm::vec3(1.0f, 0.0f, 0.5f));
	CHECK(cleaned.GetGroups()[1].Name == "Audio");
	CHECK(cleaned.GetEntityGroup(cleaned.FindEntityByName("Lamp")) == UUID(1));
	CHECK(cleaned.GetEntityGroup(cleaned.FindEntityByName("Speaker")) == UUID(0));
	CHECK(cleaned.GetEntityGroup(cleaned.FindEntityByName("Bulb")) == UUID(1));
	const std::string warnings = WarningsSince(before);
	CHECK(warnings.find("the group 'LIGHTS' was left out") != std::string::npos);
	CHECK(warnings.find("the group 'Again' was left out") != std::string::npos);
	CHECK(warnings.find("the group 'No ID' was left out") != std::string::npos);
	CHECK(warnings.find("Group 'Audio': Color could not be read") != std::string::npos);
}

TEST_CASE("A group can be hidden and locked in the editor, and an entity pinned; all are saved")
{
	Scene scene("Editor Flags");
	SceneGroup& props = scene.CreateGroup("Props");
	props.HiddenInEditor = true;
	props.LockedInEditor = true;
	const UUID propsID = props.ID;
	Entity crate = scene.CreateEntity("Crate");
	Entity lid = scene.CreateChildEntity(crate, "Lid");
	Entity loose = scene.CreateEntity("Loose");
	scene.SetEntityGroup(crate, propsID);
	loose.GetComponent<TagComponent>().Pinned = true;
	CHECK(scene.IsHiddenInEditor(lid));
	CHECK(scene.IsLockedInEditor(lid));
	CHECK_FALSE(scene.IsHiddenInEditor(loose));

	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	REQUIRE(loaded.FindGroup(propsID));
	CHECK(loaded.FindGroup(propsID)->HiddenInEditor);
	CHECK(loaded.FindGroup(propsID)->LockedInEditor);
	CHECK(loaded.FindEntityByName("Loose").GetComponent<TagComponent>().Pinned);
	CHECK_FALSE(loaded.FindEntityByName("Crate").GetComponent<TagComponent>().Pinned);

	// A plain group writes neither flag.
	Scene plain("Plain");
	plain.CreateGroup("Plain");
	const std::string text = SceneSerializer::SerializeToJson(plain).dump();
	CHECK(text.find("HiddenInEditor") == std::string::npos);
	CHECK(text.find("Pinned") == std::string::npos);
}

TEST_CASE("A scene change keeps the groups of kept entities, and joins a file group of the same name")
{
	Scene scene("Level 1");
	const UUID party = scene.CreateGroup("Party").ID;
	scene.CreateGroup("Level 1 Props");
	Entity player = scene.CreateEntity("Player");
	scene.SetEntityGroup(player, party);
	player.GetComponent<TagComponent>().KeepAcrossScenes = true;
	scene.DestroyEntity(scene.CreateEntity("Old Prop"));

	// What ChangeScene does after the old entities are gone: the next scene's file comes in.
	const Json next = Json::parse(R"({ "Version": 1, "Scene": "Level 2",
		"Groups": [ { "ID": 50, "Name": "party", "Icon": "users" }, { "ID": 51, "Name": "Rocks" } ],
		"Entities": [
			{ "ID": 60, "Name": "Companion", "Group": 50 },
			{ "ID": 61, "Name": "Boulder", "Group": 51 } ] })");
	REQUIRE(SceneSerializer::DeserializeFromJson(scene, next, true));

	REQUIRE(scene.GetGroups().size() == 2);
	CHECK(scene.GetGroups()[0].ID == party);
	CHECK(scene.GetGroups()[0].Name == "Party");
	CHECK(scene.GetGroups()[1].Name == "Rocks");
	CHECK(Names(scene.GetGroupEntities(party)) == std::vector<std::string>{ "Player", "Companion" });
	CHECK(scene.GetEntityGroup(scene.FindEntityByName("Boulder")) == UUID(51));
	CHECK(scene.FindGroup("Level 1 Props") == nullptr);
}

TEST_CASE("A prefab keeps its group in the scene that has it, and goes in none elsewhere")
{
	const std::filesystem::path projectAssets = BellowsTests::GetTestOutputDirectory() / "GroupPrefabProject";
	AssetManager::SetAssetDirectory(projectAssets);

	Scene scene("Made Here");
	const UUID crates = scene.CreateGroup("Crates").ID;
	Entity crate = scene.CreateEntity("Crate");
	scene.SetEntityGroup(crate, crates);
	REQUIRE(SceneSerializer::SavePrefab(crate, projectAssets / "Prefabs" / "Crate.bprefab"));

	Entity here = SceneSerializer::InstantiatePrefab(scene, "Prefabs/Crate.bprefab");
	CHECK(scene.GetEntityGroup(here) == crates);
	Entity under = SceneSerializer::InstantiatePrefab(scene, "Prefabs/Crate.bprefab", crate);
	CHECK(under.GetComponent<TagComponent>().Group == UUID(0));

	Scene elsewhere("Elsewhere");
	Entity there = SceneSerializer::InstantiatePrefab(elsewhere, "Prefabs/Crate.bprefab");
	CHECK(elsewhere.GetEntityGroup(there) == UUID(0));
	CHECK(there.GetComponent<TagComponent>().Group == UUID(0));
	AssetManager::SetAssetDirectory({});
}
