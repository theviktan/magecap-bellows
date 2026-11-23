#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Animation/AnimationPlayer.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace Bellows;

namespace {

	// The column of Tools/GenerateTestModels.py: "Bend" (2 s) turns Top about Z to 90 degrees at 1 s and
	// back; "Wave" (1 s) turns Base about Y.
	Ref<Mesh> LoadColumn()
	{
		Ref<Mesh> mesh = MeshImporter::ImportGLTF(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "SkinnedColumn.glb");
		REQUIRE(mesh);
		REQUIRE(mesh->GetSkin());
		return mesh;
	}

	// Where the column's top (0, 2, 0) is drawn for these skin matrices; Top moves it alone.
	glm::vec3 TopOfColumn(const std::vector<glm::mat4>& skin)
	{
		REQUIRE(skin.size() == 2);
		return glm::vec3(skin[1] * glm::vec4(0.0f, 2.0f, 0.0f, 1.0f));
	}

	// How far Bend has turned the top, in degrees, from where it is drawn.
	float BendDegrees(const std::vector<glm::mat4>& skin)
	{
		const glm::vec3 top = TopOfColumn(skin) - glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::degrees(std::atan2(-top.x, top.y));
	}

	// An entity with the column and an Animator, as the scene would load them (no renderer needed).
	Entity MakeAnimated(Scene& scene, const Ref<Mesh>& mesh)
	{
		Entity entity = scene.CreateEntity("Column");
		MeshRendererComponent& meshRenderer = entity.AddComponent<MeshRendererComponent>();
		meshRenderer.MeshPath = "Models/SkinnedColumn.glb";
		meshRenderer.LoadedMeshPath = meshRenderer.MeshPath;
		meshRenderer.RuntimeMesh = mesh;
		AnimatorComponent& animator = entity.AddComponent<AnimatorComponent>();
		animator.Clip = "Bend";
		return entity;
	}

}

TEST_CASE("An AnimationPlayer plays, loops, stops and fades between clips")
{
	const Ref<Mesh> mesh = LoadColumn();
	const MeshSkin& skin = *mesh->GetSkin();
	const Ref<AnimationClip> bend = skin.FindClip("Bend");
	const Ref<AnimationClip> wave = skin.FindClip("Wave");
	std::vector<glm::mat4> matrices;
	AnimationPlayer player;

	SUBCASE("Without a clip it gives no pose")
	{
		CHECK_FALSE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		CHECK_FALSE(player.IsPlaying());
		CHECK_FALSE(player.Update(1.0f, 1.0f, true));
	}

	SUBCASE("A clip plays at its speed and loops")
	{
		player.Play(bend);
		CHECK(player.IsPlaying());
		CHECK_FALSE(player.Update(0.5f, 2.0f, true));
		CHECK(player.GetTime() == doctest::Approx(1.0f));
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		CHECK(BendDegrees(matrices) == doctest::Approx(90.0f).epsilon(0.001));
		player.Update(1.5f, 1.0f, true);
		CHECK(player.GetTime() == doctest::Approx(0.5f));
		CHECK(player.IsPlaying());
	}

	SUBCASE("A clip that doesn't loop finishes once, at its end, and holds the pose")
	{
		player.Play(wave);
		CHECK_FALSE(player.Update(0.6f, 1.0f, false));
		CHECK(player.Update(0.6f, 1.0f, false));
		CHECK(player.GetTime() == doctest::Approx(1.0f));
		CHECK_FALSE(player.IsPlaying());
		CHECK_FALSE(player.Update(0.6f, 1.0f, false));
		CHECK(player.ComputeSkinMatrices(*skin.Rig, matrices));
		// Played again, it starts over.
		player.Play(wave);
		CHECK(player.IsPlaying());
		CHECK(player.GetTime() == doctest::Approx(0.0f));
	}

	SUBCASE("A negative speed plays backward from the end")
	{
		player.Play(bend, 0.0f, -1.0f);
		CHECK(player.GetTime() == doctest::Approx(2.0f));
		player.Update(0.5f, -1.0f, false);
		CHECK(player.GetTime() == doctest::Approx(1.5f));
		CHECK(player.Update(2.0f, -1.0f, false));
		CHECK(player.GetTime() == doctest::Approx(0.0f));
		player.Play(bend, 0.0f, -1.0f);
		player.Update(2.5f, -1.0f, true);
		CHECK(player.GetTime() == doctest::Approx(1.5f));
	}

	SUBCASE("Playing the clip that's playing changes nothing")
	{
		player.Play(bend);
		player.Update(0.7f, 1.0f, true);
		player.Play(bend, 0.3f);
		CHECK(player.GetTime() == doctest::Approx(0.7f));
		CHECK_FALSE(player.IsFading());
	}

	SUBCASE("A new clip fades in from the pose on screen")
	{
		player.Play(bend);
		player.Update(1.0f, 1.0f, true);	// Bent 90 degrees
		player.Play(wave, 0.5f);
		CHECK(player.IsFading());
		CHECK(player.GetClip() == wave);
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		CHECK(BendDegrees(matrices) == doctest::Approx(90.0f).epsilon(0.001));
		// Halfway, the old clip has kept moving (Bend at 1.25 s) while the new one starts (Wave at
		// 0.25 s), and the pose is half of each.
		player.Update(0.25f, 1.0f, true);
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		std::vector<JointPose> from = skin.Rig->GetRestPose();
		std::vector<JointPose> to = skin.Rig->GetRestPose();
		bend->Sample(1.25f, from);
		wave->Sample(0.25f, to);
		for (size_t joint = 0; joint < to.size(); joint++)
			to[joint] = JointPose::Blend(from[joint], to[joint], 0.5f);
		std::vector<glm::mat4> model;
		std::vector<glm::mat4> expected;
		skin.Rig->ComputeModelMatrices(to, model);
		skin.Rig->ComputeSkinMatrices(model, expected);
		CHECK(glm::length(TopOfColumn(matrices) - TopOfColumn(expected)) < 1e-4f);
		player.Update(0.25f, 1.0f, true);
		CHECK_FALSE(player.IsFading());
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		CHECK(BendDegrees(matrices) == doctest::Approx(0.0f).scale(1.0f));
	}

	SUBCASE("The first clip fades in from the rest pose")
	{
		player.Play(bend);
		player.Update(1.0f, 1.0f, true);
		player.Reset();
		CHECK_FALSE(player.GetClip());
		player.Play(bend, 1.0f);
		player.Update(0.5f, 1.0f, true);	// Bend at 0.5 s is 45 degrees, half faded in from straight
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		CHECK(BendDegrees(matrices) == doctest::Approx(22.5f).epsilon(0.01));
	}

	SUBCASE("Paused, stopped and odd numbers")
	{
		player.Play(bend);
		player.Pause();
		CHECK_FALSE(player.IsPlaying());
		player.Update(1.0f, 1.0f, true);
		CHECK(player.GetTime() == doctest::Approx(0.0f));
		player.Resume();
		player.Update(0.5f, 1.0f, true);
		CHECK(player.GetTime() == doctest::Approx(0.5f));
		player.Stop();
		player.Update(0.5f, 1.0f, true);
		CHECK(player.GetTime() == doctest::Approx(0.5f));

		const float nan = std::numeric_limits<float>::quiet_NaN();
		player.Play(wave, nan, nan);
		player.Update(nan, nan, true);
		player.Update(0.25f, 1e9f, true);	// The speed is held to 10: 2.5 s, wrapped to 0.5 s
		CHECK(player.GetTime() == doctest::Approx(0.5f));
		player.SetTime(nan);
		CHECK(player.GetTime() == doctest::Approx(0.0f));
		player.SetTime(7.0f);
		CHECK(player.GetTime() == doctest::Approx(1.0f));
		REQUIRE(player.ComputeSkinMatrices(*skin.Rig, matrices));
		for (const glm::mat4& matrix : matrices)
		{
			for (int column = 0; column < 4; column++)
				CHECK(glm::all(glm::equal(matrix[column], matrix[column])));
		}
	}
}

TEST_CASE("An Animator plays its clip in game time while the game runs")
{
	const Ref<Mesh> mesh = LoadColumn();
	Scene scene("Animated");
	Entity column = MakeAnimated(scene, mesh);
	AnimatorComponent& edited = column.GetComponent<AnimatorComponent>();

	// Not running: the rest pose.
	scene.OnUpdateEditor(Timestep(0.5f));
	CHECK(edited.RuntimeSkinMatrices.empty());

	scene.OnRuntimeStart();
	AnimatorComponent& animator = column.GetComponent<AnimatorComponent>();
	CHECK(animator.RuntimePlayer.IsPlaying());
	CHECK(animator.RuntimePlayer.GetClip() == mesh->GetSkin()->FindClip("Bend"));
	CHECK(BendDegrees(animator.RuntimeSkinMatrices) == doctest::Approx(0.0f).scale(1.0f));

	scene.OnUpdateRuntime(Timestep(0.5f));
	CHECK(BendDegrees(animator.RuntimeSkinMatrices) == doctest::Approx(45.0f).epsilon(0.001));

	// The game's pause holds it.
	scene.SetGamePaused(true);
	scene.OnUpdateRuntime(Timestep(0.5f));
	CHECK(animator.RuntimePlayer.GetTime() == doctest::Approx(0.5f));
	scene.SetGamePaused(false);

	SUBCASE("Speed and Loop apply as it plays")
	{
		animator.Speed = 2.0f;
		animator.Loop = false;
		scene.OnUpdateRuntime(Timestep(1.0f));
		CHECK(animator.RuntimePlayer.GetTime() == doctest::Approx(2.0f));
		CHECK_FALSE(animator.RuntimePlayer.IsPlaying());
	}

	SUBCASE("A new mesh starts its clip again; a static one has no pose")
	{
		MeshRendererComponent& meshRenderer = column.GetComponent<MeshRendererComponent>();
		meshRenderer.RuntimeMesh = LoadColumn();
		scene.OnUpdateRuntime(Timestep(0.25f));
		CHECK(animator.RuntimePlayer.GetTime() == doctest::Approx(0.25f));
		meshRenderer.RuntimeMesh = CreateRef<Mesh>("Plain", std::vector<Vertex>(3), std::vector<uint32_t> { 0, 1, 2 },
			std::vector<Submesh> {}, std::vector<Ref<Material>> {});
		scene.OnUpdateRuntime(Timestep(0.25f));
		CHECK(animator.RuntimeSkinMatrices.empty());
		CHECK_FALSE(animator.RuntimePlayer.GetClip());
	}
	scene.OnRuntimeStop();
}

TEST_CASE("An Animator's start: Play On Start, the first clip, an unknown clip")
{
	const Ref<Mesh> mesh = LoadColumn();
	Scene scene("Animated");
	Entity column = MakeAnimated(scene, mesh);
	AnimatorComponent& animator = column.GetComponent<AnimatorComponent>();

	SUBCASE("No clip named: the mesh's first")
	{
		animator.Clip.clear();
		scene.OnRuntimeStart();
		CHECK(animator.RuntimePlayer.GetClip() == mesh->GetSkin()->Clips.front());
	}
	SUBCASE("Play On Start off: the rest pose until a script plays something")
	{
		animator.PlayOnStart = false;
		scene.OnRuntimeStart();
		CHECK_FALSE(animator.RuntimePlayer.GetClip());
		CHECK(animator.RuntimeSkinMatrices.empty());
	}
	SUBCASE("A clip the mesh doesn't have: the rest pose")
	{
		animator.Clip = "Dance";
		scene.OnRuntimeStart();
		CHECK_FALSE(animator.RuntimePlayer.GetClip());
		CHECK(animator.RuntimeSkinMatrices.empty());
	}
	scene.OnRuntimeStop();
}

TEST_CASE("The editor's preview plays an Animator without the game, and stops cleanly")
{
	const Ref<Mesh> mesh = LoadColumn();
	const Ref<Scene> edited = CreateRef<Scene>("Animated");
	Scene& scene = *edited;
	Entity column = MakeAnimated(scene, mesh);
	AnimatorComponent& animator = column.GetComponent<AnimatorComponent>();
	animator.PlayOnStart = false;	// The preview plays the clip anyway

	animator.RuntimePreview = true;
	scene.OnUpdateEditor(Timestep(0.5f));
	CHECK(BendDegrees(animator.RuntimeSkinMatrices) == doctest::Approx(45.0f).epsilon(0.001));

	// Choosing another clip switches the preview to it.
	animator.Clip = "Wave";
	scene.OnUpdateEditor(Timestep(0.25f));
	CHECK(animator.RuntimePlayer.GetClip() == mesh->GetSkin()->FindClip("Wave"));
	CHECK(animator.RuntimePlayer.GetTime() == doctest::Approx(0.25f));

	// Scrubbing: paused, the time is wherever it's set.
	animator.RuntimePlayer.Pause();
	animator.RuntimePlayer.SetTime(0.75f);
	scene.OnUpdateEditor(Timestep(0.5f));
	CHECK(animator.RuntimePlayer.GetTime() == doctest::Approx(0.75f));

	// Play copies the scene: the copy starts from the Animator's own settings, not the preview.
	const Ref<Scene> playing = Scene::Copy(edited);
	REQUIRE(playing);
	playing->OnRuntimeStart();
	Entity copy = playing->TryGetEntityByUUID(column.GetUUID());
	REQUIRE(copy);
	const AnimatorComponent& copied = copy.GetComponent<AnimatorComponent>();
	CHECK_FALSE(copied.RuntimePreview);
	CHECK_FALSE(copied.RuntimePlayer.GetClip());	// Play On Start is off
	playing->OnRuntimeStop();

	animator.RuntimePreview = false;
	scene.OnUpdateEditor(Timestep(0.1f));
	CHECK(animator.RuntimeSkinMatrices.empty());
	CHECK_FALSE(animator.RuntimePlayer.GetClip());
}

TEST_CASE("An Animator is saved with the scene and copied with its entity")
{
	Scene scene("Saved");
	Entity entity = scene.CreateEntity("Hero");
	AnimatorComponent& animator = entity.AddComponent<AnimatorComponent>();
	animator.Clip = "Run";
	animator.PlayOnStart = false;
	animator.Loop = false;
	animator.Speed = -1.5f;

	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity hero = loaded.FindEntityByName("Hero");
	REQUIRE(hero);
	REQUIRE(hero.HasComponent<AnimatorComponent>());
	const AnimatorComponent& read = hero.GetComponent<AnimatorComponent>();
	CHECK(read.Clip == "Run");
	CHECK_FALSE(read.PlayOnStart);
	CHECK_FALSE(read.Loop);
	CHECK(read.Speed == doctest::Approx(-1.5f));

	// An older file without the component's newer values keeps the defaults.
	Json old = json;
	for (Json& entityJson : old["Entities"])
	{
		if (entityJson.contains("Animator"))
			entityJson["Animator"] = Json::object({ { "Clip", "Idle" } });
	}
	Scene older("Older");
	REQUIRE(SceneSerializer::DeserializeFromJson(older, old));
	Entity olderHero = older.FindEntityByName("Hero");
	REQUIRE(olderHero);
	const AnimatorComponent& defaults = olderHero.GetComponent<AnimatorComponent>();
	CHECK(defaults.Clip == "Idle");
	CHECK(defaults.PlayOnStart);
	CHECK(defaults.Loop);
	CHECK(defaults.Speed == doctest::Approx(1.0f));

	Entity duplicate = scene.DuplicateEntity(entity);
	REQUIRE(duplicate.HasComponent<AnimatorComponent>());
	CHECK(duplicate.GetComponent<AnimatorComponent>().Clip == "Run");
}
