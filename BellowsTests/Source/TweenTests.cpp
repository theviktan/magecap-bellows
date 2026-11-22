#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Math/Easing.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scene/Tweens.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/TweenProperties.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	constexpr double c_Frame = 1.0 / 60.0;

	// A playing scene with one entity, and every 'tween finished' it sends.
	struct TweenScene
	{
		Ref<Scene> Play = CreateRef<Scene>("Tweens");
		Entity Box;
		std::vector<Scripting::ScriptValueList> Finished;
		Scripting::ScriptEventListenerID Listener = 0;

		TweenScene()
		{
			Box = Play->CreateEntity("Box");
			Play->OnRuntimeStart();
			Listener = Scripting::AddScriptEventListener([this](const std::string& name, const Scripting::ScriptValueList& values, const Scripting::ScriptEventDelivery&)
			{
				if (name == "tween finished")
					Finished.push_back(values);
			});
		}

		~TweenScene()
		{
			Scripting::RemoveScriptEventListener(Listener);
			Play->OnRuntimeStop();
		}

		SceneTweens& Tweens() { return *Play->GetTweens(); }

		uint64_t Start(const char* property, const glm::vec4& value, double seconds, const char* ease = "linear", double delay = 0.0, bool relative = false)
		{
			TweenSettings settings;
			settings.Property = &Scripting::FindTweenProperty(property);
			settings.Value = value;
			settings.Duration = seconds;
			settings.Delay = delay;
			settings.Relative = relative;
			settings.Ease = Math::FindEaseCurve(ease);
			return Tweens().Start(Box.GetUUID(), settings);
		}

		void Run(double seconds)
		{
			Play->OnUpdateRuntime(Timestep(static_cast<float>(seconds)));
		}

		glm::vec3 Position() { return Box.GetComponent<TransformComponent>().Translation; }
	};

	// A ForgeScript VM with the engine API acting on one scene; prints and errors are collected.
	struct ForgeScriptRunner
	{
		ForgeScript::VM Machine;
		std::string Output;
		std::vector<std::string> Errors;

		explicit ForgeScriptRunner(Scene& scene)
		{
			Machine.SetPrintHandler([this](const std::string& text) { Output += text + "\n"; });
			Machine.SetErrorHandler([this](const ForgeScript::ScriptError& error) { Errors.push_back(error.ToString()); });
			Scripting::RegisterForgeScriptApi(Machine);
			Scripting::SetContextScene(&scene);
		}

		~ForgeScriptRunner() { Scripting::SetContextScene(nullptr); }

		void Run(const std::string& source, Entity self)
		{
			ForgeScript::Module* module = Machine.CreateModule("tween.forge");
			Machine.SetGlobal(module, "self", Scripting::MakeForgeScriptEntity(Machine, Scripting::ScriptEntity(self)));
			Machine.RunInModule(module, source, "tween.forge");
		}
	};

}

TEST_CASE("Easing curves are ForgeScript's ease() curves, by the same names")
{
	Scene scene("Ease");
	Entity entity = scene.CreateEntity("E");
	ForgeScriptRunner forge(scene);
	std::string source;
	std::vector<double> expected;
	for (const std::string& style : Math::GetEaseStyleNames())
	{
		const Math::EaseCurve curve = Math::FindEaseCurve(style);
		REQUIRE(curve);
		for (int i = 0; i <= 20; i++)
		{
			const double t = i / 20.0;
			source += "say ease(" + std::to_string(t) + ", \"" + style + "\") * 1000000\n";
			expected.push_back(curve(t) * 1000000.0);
		}
	}
	forge.Run(source, entity);
	REQUIRE(forge.Errors.empty());
	std::istringstream lines(forge.Output);
	for (double value : expected)
	{
		double printed = 0.0;
		const bool read = static_cast<bool>(lines >> printed);
		REQUIRE(read);
		CHECK(printed == doctest::Approx(value).epsilon(1e-6));
	}

	CHECK(Math::FindEaseCurve("  In Out ") == Math::FindEaseCurve("in out"));
	CHECK_FALSE(Math::FindEaseCurve("wobbly"));
	CHECK(Math::GetEaseStyleNames().size() == 11);
}

TEST_CASE("Tweens can move every number and vector property of the script API, by its script name")
{
	const auto find = [](const char* name) -> const TweenProperty*
	{
		for (const TweenProperty& property : Scripting::GetTweenProperties())
		{
			if (property.Name == name)
				return &property;
		}
		return nullptr;
	};
	REQUIRE(find("position"));
	CHECK(find("position")->Type == TweenValueType::Vec3);
	CHECK(find("rotation")->Rotation == TweenRotation::Degrees);
	CHECK(find("Transform.rotation")->Rotation == TweenRotation::Radians);
	CHECK(find("Transform.rotation_degrees")->Rotation == TweenRotation::Degrees);
	CHECK(find("scale")->Rotation == TweenRotation::None);
	REQUIRE(find("UIImage.color"));
	CHECK(find("UIImage.color")->Type == TweenValueType::Vec4);
	REQUIRE(find("AudioSource.volume"));
	CHECK(find("AudioSource.volume")->Type == TweenValueType::Number);
	CHECK(find("UIRect.position"));
	CHECK(find("Camera.fov"));
	// Text, flags, quaternions and read-only values aren't tweened.
	CHECK_FALSE(find("name"));
	CHECK_FALSE(find("visible"));
	CHECK_FALSE(find("Transform.quaternion"));
	CHECK_FALSE(find("Transform.forward"));

	try
	{
		Scripting::FindTweenProperty("UIImage.colour");
		FAIL("a misspelled property was found");
	}
	catch (const Scripting::ScriptValueError& error)
	{
		const std::string message = error.what();
		CHECK(message.find("'UIImage.colour' can't be tweened") != std::string::npos);
		CHECK(message.find("UIImage.color") != std::string::npos);
	}
}

TEST_CASE("A tween moves a property over its length, lands exactly, and finishes once")
{
	TweenScene test;
	const uint64_t id = test.Start("position", { 10.0f, 0.0f, -4.0f, 0.0f }, 1.0);
	CHECK(test.Tweens().IsRunning(id));
	test.Run(0.5);
	CHECK(test.Position().x == doctest::Approx(5.0f));
	CHECK(test.Position().z == doctest::Approx(-2.0f));
	CHECK(test.Tweens().GetProgress(id) == doctest::Approx(0.5));
	test.Run(0.6);
	CHECK(test.Position() == glm::vec3(10.0f, 0.0f, -4.0f));
	CHECK_FALSE(test.Tweens().IsRunning(id));
	CHECK(test.Tweens().HasFinished(id));
	CHECK(test.Tweens().GetProgress(id) == 1.0);
	REQUIRE(test.Finished.size() == 1);
	CHECK(std::get<Scripting::ScriptEntity>(test.Finished[0][0].Data).ID == test.Box.GetUUID());
	CHECK(std::get<std::string>(test.Finished[0][1].Data) == "position");
	CHECK(std::get<double>(test.Finished[0][2].Data) == static_cast<double>(id));

	test.Run(1.0);
	CHECK(test.Finished.size() == 1);
	CHECK(test.Tweens().GetCount() == 0);
}

TEST_CASE("A delayed tween reads its start when it starts, and a new tween of a property replaces the old")
{
	TweenScene test;
	const uint64_t first = test.Start("position", { 10.0f, 0.0f, 0.0f, 0.0f }, 2.0);
	// Moving the box during the delay changes where the second tween starts from.
	const uint64_t scale = test.Start("scale", { 3.0f, 3.0f, 3.0f, 0.0f }, 1.0, "linear", 0.5);
	test.Run(0.25);
	CHECK(test.Box.GetComponent<TransformComponent>().Scale == glm::vec3(1.0f));
	test.Box.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
	test.Run(0.25 + 0.5);
	CHECK(test.Box.GetComponent<TransformComponent>().Scale.x == doctest::Approx(2.5f));
	CHECK(test.Tweens().IsRunning(scale));

	// The replacement starts where the first one got to (x = 5), not where the first began.
	CHECK(test.Position().x == doctest::Approx(5.0f));
	const uint64_t second = test.Start("position", { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	CHECK_FALSE(test.Tweens().IsRunning(first));
	CHECK_FALSE(test.Tweens().HasFinished(first));
	test.Run(0.5);
	CHECK(test.Position().x == doctest::Approx(2.5f));
	test.Run(0.5);
	CHECK(test.Tweens().HasFinished(second));
	CHECK_FALSE(test.Tweens().HasFinished(first));
}

TEST_CASE("Tweens repeat, go back and forth, and repeating forever never finishes")
{
	TweenScene test;
	const uint64_t id = test.Start("position", { 4.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	test.Tweens().SetRepeat(id, 1);
	test.Tweens().SetYoyo(id, true);
	test.Run(1.5);
	CHECK(test.Position().x == doctest::Approx(2.0f));	// Halfway back
	CHECK(test.Tweens().GetProgress(id) == doctest::Approx(0.75));
	test.Run(0.5);
	CHECK(test.Position().x == doctest::Approx(0.0f));
	CHECK(test.Tweens().HasFinished(id));

	const uint64_t forever = test.Start("position", { 4.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	test.Tweens().SetRepeat(forever, -1);
	test.Run(10.25);
	CHECK(test.Position().x == doctest::Approx(1.0f));	// Each run starts over from the start
	CHECK(test.Tweens().GetProgress(forever) == doctest::Approx(0.25));
	CHECK(test.Tweens().IsRunning(forever));
	CHECK(test.Finished.size() == 1);

	// No length: the value is set at once and it ends, even when told to repeat forever.
	const uint64_t instant = test.Start("position", { 9.0f, 0.0f, 0.0f, 0.0f }, 0.0);
	test.Tweens().SetRepeat(instant, -1);
	test.Run(c_Frame);
	CHECK(test.Position().x == 9.0f);
	CHECK(test.Tweens().HasFinished(instant));
}

TEST_CASE("Rotations turn the shortest way, and tween_by turns by the whole amount")
{
	TweenScene test;
	TransformComponent& transform = test.Box.GetComponent<TransformComponent>();
	const glm::vec3 forward(0.0f, 0.0f, -1.0f);

	// 0 to 270 degrees about Y is a quarter turn the other way: halfway faces -45 degrees.
	test.Start("rotation", { 0.0f, 270.0f, 0.0f, 0.0f }, 1.0);
	test.Run(0.5);
	const glm::vec3 halfway = transform.GetRotation() * forward;
	const glm::vec3 expected = glm::quat(glm::radians(glm::vec3(0.0f, -45.0f, 0.0f))) * forward;
	CHECK(glm::length(halfway - expected) < 1e-4f);
	test.Run(0.5);
	CHECK(transform.GetRotationEuler().y == doctest::Approx(glm::radians(270.0f)));

	// Two whole turns: a quarter of the way through is half a turn from the start.
	transform.SetRotationEuler(glm::vec3(0.0f));
	test.Start("rotation", { 0.0f, 720.0f, 0.0f, 0.0f }, 2.0, "linear", 0.0, true);
	test.Run(0.5);
	CHECK(glm::length(transform.GetRotation() * forward - glm::vec3(0.0f, 0.0f, 1.0f)) < 1e-4f);
	test.Run(1.5);
	CHECK(transform.GetRotationEuler().y == doctest::Approx(glm::radians(720.0f)));
}

TEST_CASE("The game's pause holds tweens unless they run while paused, and each tween pauses on its own")
{
	TweenScene test;
	const uint64_t menu = test.Start("scale", { 2.0f, 2.0f, 2.0f, 0.0f }, 1.0);
	test.Tweens().SetWhilePaused(menu, true);
	const uint64_t world = test.Start("position", { 1.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	test.Play->SetGamePaused(true);
	test.Run(0.5);
	CHECK(test.Position().x == 0.0f);
	CHECK(test.Box.GetComponent<TransformComponent>().Scale.x == doctest::Approx(1.5f));
	test.Play->SetGamePaused(false);

	CHECK(test.Tweens().Pause(world));
	test.Run(0.25);
	CHECK(test.Position().x == 0.0f);
	CHECK(test.Tweens().IsRunning(world));
	CHECK(test.Tweens().Resume(world));
	test.Run(0.25);
	CHECK(test.Position().x == doctest::Approx(0.25f));
}

TEST_CASE("A tween ends quietly when its entity or component goes")
{
	TweenScene test;
	test.Box.AddComponent<UIRectComponent>();
	UIImageComponent& image = test.Box.AddComponent<UIImageComponent>();
	image.Color = glm::vec4(1.0f);
	const uint64_t fade = test.Start("UIImage.color", { 1.0f, 1.0f, 1.0f, 0.0f }, 1.0);
	test.Run(0.5);
	CHECK(test.Box.GetComponent<UIImageComponent>().Color.a == doctest::Approx(0.5f));
	test.Box.RemoveComponent<UIImageComponent>();
	test.Run(0.25);
	CHECK_FALSE(test.Tweens().IsRunning(fade));
	CHECK_FALSE(test.Tweens().HasFinished(fade));

	const uint64_t move = test.Start("position", { 1.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	test.Play->DestroyEntity(test.Box);
	test.Run(0.25);
	test.Run(0.25);
	CHECK_FALSE(test.Tweens().IsRunning(move));
	CHECK(test.Finished.empty());
}

TEST_CASE("Complete jumps to the end and its 'tween finished' follows with the next update")
{
	TweenScene test;
	const uint64_t id = test.Start("position", { 6.0f, 0.0f, 0.0f, 0.0f }, 5.0, "out", 2.0);
	test.Tweens().SetRepeat(id, 3);
	CHECK(test.Tweens().Complete(*test.Play, id));
	CHECK(test.Position().x == 6.0f);
	CHECK(test.Tweens().HasFinished(id));
	CHECK(test.Finished.empty());
	test.Run(c_Frame);
	CHECK(test.Finished.size() == 1);
	CHECK_FALSE(test.Tweens().Complete(*test.Play, id));
	CHECK_FALSE(test.Tweens().Stop(id));
}

TEST_CASE("Tweens of a kept entity keep going through a scene change; the others end")
{
	TweenScene test;
	test.Box.GetComponent<TagComponent>().KeepAcrossScenes = true;
	Entity crate = test.Play->CreateEntity("Crate");
	const uint64_t kept = test.Start("position", { 8.0f, 0.0f, 0.0f, 0.0f }, 1.0);
	TweenSettings settings;
	settings.Property = &Scripting::FindTweenProperty("position");
	settings.Value = glm::vec4(3.0f, 0.0f, 0.0f, 0.0f);
	settings.Duration = 1.0;
	settings.Ease = Math::FindEaseCurve("linear");
	const uint64_t gone = test.Tweens().Start(crate.GetUUID(), settings);
	test.Run(0.5);

	Scene next("Next");
	next.CreateEntity("Ground");
	REQUIRE(test.Play->ChangeScene(SceneSerializer::SerializeToJson(next), "Scenes/Next.bscene"));
	test.Run(0.5);
	CHECK(test.Position().x == doctest::Approx(8.0f));
	CHECK(test.Tweens().HasFinished(kept));
	CHECK_FALSE(test.Tweens().IsRunning(gone));
	CHECK_FALSE(test.Tweens().HasFinished(gone));
}

TEST_CASE("A scene runs at most 10,000 tweens, and tweens go when play stops")
{
	TweenScene test;
	std::vector<Entity> entities;
	for (size_t i = 0; i < SceneTweens::c_MaxTweens; i++)
	{
		TweenSettings settings;
		settings.Property = &Scripting::FindTweenProperty("scale");
		settings.Value = glm::vec4(2.0f);
		settings.Duration = 1.0;
		settings.Ease = Math::FindEaseCurve("linear");
		test.Tweens().Start(test.Play->CreateEntity("E").GetUUID(), settings);
	}
	CHECK_THROWS_AS(test.Start("position", glm::vec4(1.0f), 1.0), std::length_error);
	test.Run(c_Frame);
	CHECK(test.Tweens().GetCount() == SceneTweens::c_MaxTweens);
	test.Play->OnRuntimeStop();
	CHECK_FALSE(test.Play->GetTweens());
	test.Play->OnRuntimeStart();
	CHECK(test.Play->GetTweens()->GetCount() == 0);
}

TEST_CASE("Scripts start and steer tweens through the API, and bad arguments say what's wrong")
{
	TweenScene test;
	test.Box.AddComponent<UIRectComponent>();
	test.Box.AddComponent<UIImageComponent>().Color = glm::vec4(1.0f);
	ForgeScriptRunner forge(*test.Play);
	forge.Run(
		"set t to tween(self, \"position\", vector(0, 4, 0), 2, \"linear\")\n"
		"say t.property\n"
		"say t.running\n"
		"set t.repeat to 1\n"
		"set fade to tween(self, \"UIImage.color\", [1, 1, 1, 0], 1)\n"
		"say fade.entity.name\n"
		"set spin to tween_by(self, \"rotation\", vector(0, 90, 0), 1, \"out\", 0.5)\n"
		"say stop_tweens(self, \"rotation\")\n"
		"say spin.running\n",
		test.Box);
	REQUIRE(forge.Errors.empty());
	CHECK(forge.Output == "position\ntrue\nBox\n1\nfalse\n");
	test.Run(1.0);
	CHECK(test.Position().y == doctest::Approx(2.0f));

	const auto error = [&](const std::string& line)
	{
		forge.Errors.clear();
		forge.Run(line + "\n", test.Box);
		REQUIRE(forge.Errors.size() == 1);
		return forge.Errors[0];
	};
	CHECK(error("tween(self, \"position\", 5, 1)").find("needs a vector of 3 numbers") != std::string::npos);
	CHECK(error("tween(self, \"UIImage.color\", [1, 0, 0], 1)").find("needs 4 numbers (red, green, blue, alpha)") != std::string::npos);
	CHECK(error("tween(self, \"position\", vector(1, 2, 3), -1)").find("A tween's length is a number of seconds from 0 to 1,000,000") != std::string::npos);
	CHECK(error("tween(self, \"position\", vector(1, 2, 3), 1, \"in out\", 2000000)").find("A tween's delay") != std::string::npos);
	CHECK(error("tween(self, \"position\", vector(1, 2, 3), 1, \"wobbly\")").find("'wobbly' isn't an easing style. The styles are \"linear\", \"in\"") != std::string::npos);
	CHECK(error("tween(self, \"name\", 1, 1)").find("'name' can't be tweened") != std::string::npos);
	CHECK(error("set t to tween(self, \"position\", vector(1, 2, 3), 1)\nset t.repeat to -2").find("from -1 (forever) to 1,000,000") != std::string::npos);

	// Outside play there are no tweens to start.
	Scene editing("Editing");
	Entity still = editing.CreateEntity("Still");
	ForgeScriptRunner editor(editing);
	editor.Run("tween(self, \"position\", vector(1, 2, 3), 1)\n", still);
	REQUIRE(editor.Errors.size() == 1);
	CHECK(editor.Errors[0].find("Tweens run while the game is playing") != std::string::npos);
}
