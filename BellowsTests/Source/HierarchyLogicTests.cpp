#include <doctest/doctest.h>

#include "HierarchyLogic.h"

#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <string>
#include <vector>

using namespace Bellows;

TEST_CASE("Hierarchy search: words, tag:, has: and group:")
{
	using namespace HierarchyLogic;
	const Filter filter = ParseFilter("  Crate tag:Heavy has:physics has:wings group:docks ");
	CHECK(filter.Words == std::vector<std::string>{ "crate" });
	CHECK(filter.Tags == std::vector<std::string>{ "heavy" });
	CHECK(filter.Has == std::vector<std::string>{ "physics" });
	CHECK(filter.Unknown == std::vector<std::string>{ "wings" });
	CHECK(filter.Groups == std::vector<std::string>{ "docks" });
	CHECK(filter.IsActive());
	CHECK_FALSE(ParseFilter("   ").IsActive());
	CHECK(ParseFilter("tag:").Words == std::vector<std::string>{ "tag:" });

	Scene scene("Search");
	const UUID docks = scene.CreateGroup("Docks").ID;
	Entity crate = scene.CreateEntity("Big Crate");
	crate.AddComponent<RigidBodyComponent>();
	crate.GetComponent<TagComponent>().Tags = { "heavy" };
	scene.SetEntityGroup(crate, docks);
	Entity lid = scene.CreateChildEntity(crate, "Lid");
	Entity lamp = scene.CreateEntity("Lamp");
	lamp.AddComponent<PointLightComponent>();

	CHECK(Matches(scene, crate, ParseFilter("crate tag:HEAVY has:physics group:Docks")));
	CHECK_FALSE(Matches(scene, crate, ParseFilter("crate has:light")));
	CHECK(Matches(scene, lid, ParseFilter("group:docks")));
	CHECK_FALSE(Matches(scene, lamp, ParseFilter("group:docks")));
	CHECK(Matches(scene, lamp, ParseFilter("has:light lam")));
	CHECK(Matches(scene, crate, ParseFilter("has:tag")));
	CHECK_FALSE(Matches(scene, lamp, ParseFilter("has:tag")));

	Entity comet = scene.CreateEntity("Comet");
	comet.AddComponent<TrailComponent>();
	CHECK(ParseFilter("has:trail").Unknown.empty());
	CHECK(Matches(scene, comet, ParseFilter("has:trail")));
	CHECK_FALSE(Matches(scene, lamp, ParseFilter("has:trail")));
}

TEST_CASE("Entities are classified for the By type view, the first kind that fits winning")
{
	using namespace HierarchyLogic;
	Scene scene("Kinds");
	Entity scripted = scene.CreateEntity("Guard");
	scripted.AddComponent<MeshRendererComponent>();
	scripted.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Guard.forge";
	Entity sun = scene.CreateEntity("Sun");
	sun.AddComponent<DirectionalLightComponent>();
	Entity camera = scene.CreateEntity("Camera");
	camera.AddComponent<CameraComponent>();
	Entity speaker = scene.CreateEntity("Speaker");
	speaker.AddComponent<AudioSourceComponent>();
	Entity radio = scene.CreateEntity("Radio");
	radio.AddComponent<AudioSourceComponent>();
	radio.AddComponent<MeshRendererComponent>();
	Entity ball = scene.CreateEntity("Ball");
	ball.AddComponent<MeshRendererComponent>();
	ball.AddComponent<RigidBodyComponent>();
	Entity empty = scene.CreateEntity("Empty");

	CHECK(Classify(scripted) == EntityKind::Scripted);
	CHECK(Classify(sun) == EntityKind::Light);
	CHECK(Classify(camera) == EntityKind::Camera);
	CHECK(Classify(speaker) == EntityKind::Audio);
	CHECK(Classify(radio) == EntityKind::Mesh);
	CHECK(Classify(ball) == EntityKind::Physics);
	CHECK(Classify(empty) == EntityKind::Other);
	CHECK(std::string(KindName(EntityKind::Light)) == "Lights");
}

TEST_CASE("Numbered sibling names share a stack name")
{
	using HierarchyLogic::StackName;
	CHECK(StackName("Crate 0-1") == "Crate");
	CHECK(StackName("Crate (2)") == "Crate");
	CHECK(StackName("SM_Env_Flower_57") == "SM_Env_Flower");
	CHECK(StackName("Orbiting Light 3") == "Orbiting Light");
	CHECK(StackName("Lantern").empty());
	CHECK(StackName("42").empty());
	CHECK(StackName("Wall (left)").empty());
}

TEST_CASE("Organize suggests groups for top-level lights, cameras, UI and sound, and applies them")
{
	using namespace HierarchyLogic;
	Scene scene("Organize");
	Entity sun = scene.CreateEntity("Sun");
	sun.AddComponent<DirectionalLightComponent>();
	Entity camera = scene.CreateEntity("Main Camera");
	camera.AddComponent<CameraComponent>();
	Entity hud = scene.CreateEntity("HUD");
	hud.AddComponent<UICanvasComponent>();
	Entity music = scene.CreateEntity("Music");
	music.AddComponent<AudioSourceComponent>();
	Entity torch = scene.CreateEntity("Scripted Torch");
	torch.AddComponent<PointLightComponent>();
	torch.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Torch.forge";
	Entity lamp = scene.CreateChildEntity(scene.CreateEntity("House"), "Lamp");
	lamp.AddComponent<PointLightComponent>();
	SceneGroup& lighting = scene.CreateGroup("lighting");
	const UUID lightingID = lighting.ID;
	Entity filed = scene.CreateEntity("Filed Light");
	filed.AddComponent<PointLightComponent>();
	scene.SetEntityGroup(filed, lightingID);

	const std::vector<OrganizeMove> moves = SuggestOrganize(scene);
	REQUIRE(moves.size() == 4);
	CHECK(moves[0].Entity == sun.GetUUID());
	CHECK(moves[0].Group == "lighting");		// The scene's own group, in its own spelling
	CHECK_FALSE(moves[0].NewGroup);
	CHECK(moves[1].Group == "Cameras");
	CHECK(moves[1].NewGroup);
	CHECK(moves[2].Group == "UI");
	CHECK(moves[3].Group == "Audio");

	CHECK(ApplyOrganize(scene, moves) == 4);
	CHECK(scene.GetEntityGroup(sun) == lightingID);
	REQUIRE(scene.FindGroup("Cameras"));
	CHECK(scene.FindGroup("Cameras")->Icon == "camera");
	CHECK(scene.GetEntityGroup(camera) == scene.FindGroup("Cameras")->ID);
	CHECK_FALSE(scene.GetEntityGroup(torch).IsValid());
	CHECK_FALSE(scene.GetEntityGroup(lamp).IsValid());
	CHECK(SuggestOrganize(scene).empty());
}
