#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"

using namespace Bellows;

namespace {

	// A ForgeScript VM with the engine API, printing into a string, acting on one scene.
	struct ForgeScriptHarness
	{
		ForgeScript::VM Machine;
		std::string Output;
		std::vector<std::string> Errors;

		explicit ForgeScriptHarness(Scene& scene, const ForgeScript::VMConfig& config = ForgeScript::VMConfig())
			: Machine(config)
		{
			Machine.SetPrintHandler([this](const std::string& text) { Output += text + "\n"; });
			Machine.SetErrorHandler([this](const ForgeScript::ScriptError& error) { Errors.push_back(error.ToString()); });
			Scripting::RegisterForgeScriptApi(Machine);
			Scripting::SetContextScene(&scene);
		}

		~ForgeScriptHarness()
		{
			Scripting::SetContextScene(nullptr);
		}

		std::string AllErrors() const
		{
			std::string text;
			for (const std::string& error : Errors)
				text += error + "\n";
			return text;
		}
	};

}

TEST_CASE("ForgeScript reads and changes entities through the shared API")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity crate = scene->CreateEntity("Crate");
	crate.GetComponent<TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);
	crate.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Cube";

	ForgeScriptHarness h(*scene);
	ForgeScript::Module* module = h.Machine.CreateModule("crate.forge");
	h.Machine.SetGlobal(module, "self", Scripting::MakeForgeScriptEntity(h.Machine, Scripting::ScriptEntity(crate)));
	ForgeScript::LoadResult result = h.Machine.RunInModule(module,
		"say my name\n"
		"say self\n"
		"say my position\n"
		"set my position.x to 10\n"
		"add 1 to my transform.position.y\n"
		"set my scale to vector(2, 2, 2)\n"
		"say my components\n"
		"say my get_component(\"MeshRenderer\").mesh\n"
		"say my get_component(\"RigidBody\")\n"
		"set body to self.add_component(\"RigidBody\")\n"
		"set body.mass to 5\n"
		"say body.mass\n"
		"say self.has_component(\"RigidBody\")\n"
		"say find_entity(\"Crate\") is self\n"
		"say get_entity(my id) is self\n",
		"crate.forge");

	INFO(h.AllErrors());
	CHECK(result.Success);
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string(
		"Crate\n"
		"Entity('Crate')\n"
		"(1, 2, 3)\n"
		"[\"Transform\", \"MeshRenderer\"]\n"
		"Builtin/Cube\n"
		"nothing\n"
		"5\n"
		"true\n"
		"true\n"
		"true\n"));

	const TransformComponent& transform = crate.GetComponent<TransformComponent>();
	CHECK_EQ(transform.Translation.x, doctest::Approx(10.0f));
	CHECK_EQ(transform.Translation.y, doctest::Approx(3.0f));
	CHECK_EQ(transform.Scale.z, doctest::Approx(2.0f));
	REQUIRE(crate.HasComponent<RigidBodyComponent>());
	CHECK_EQ(crate.GetComponent<RigidBodyComponent>().Mass, doctest::Approx(5.0f));
}

TEST_CASE("ForgeScript creates entities, uses materials and colors")
{
	Ref<Scene> scene = CreateRef<Scene>();
	ForgeScriptHarness h(*scene);
	h.Machine.Load(
		"set holder to create_entity(\"Holder\")\n"
		"set child to create_entity(\"Child\", holder)\n"
		"say child.parent is holder\n"
		"say holder.children.length\n"
		"say holder.find_child(\"Child\") is child\n"
		"set gold to Material(\"Gold\", [1, 0.8, 0], 1, 0.3)\n"
		"say gold\n"
		"say gold.metallic\n"
		"set gold.roughness to 5\n"
		"say gold.roughness\n"
		"set renderer to child.add_component(\"MeshRenderer\")\n"
		"set renderer.material to gold\n"
		"say renderer.material is gold.path\n"
		"set renderer.color to [0, 0, 1]\n"
		"say renderer.color\n"
		"say get_entities(\"MeshRenderer\").length\n"
		"say is_key_down(\"W\")\n"
		"say is_mouse_down()\n",
		"scene.forge");

	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string(
		"true\n"
		"1\n"
		"true\n"
		"Material('Gold')\n"
		"1\n"
		"1\n"
		"true\n"
		"[0, 0, 1, 1]\n"
		"1\n"
		"false\n"
		"false\n"));
	CHECK(scene->FindEntityByName("Child"));
}

TEST_CASE("ForgeScript reports engine errors clearly and can catch them")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity box = scene->CreateEntity("Box");

	ForgeScriptHarness h(*scene);
	h.Machine.SetSharedGlobal("box", Scripting::MakeForgeScriptEntity(h.Machine, Scripting::ScriptEntity(box)));
	h.Machine.Load(
		"try\n\tbox.add_component(\"NotAComponent\")\ncatch p\n\tsay p\nend\n"
		"try\n\tbox.remove_component(\"Transform\")\ncatch p\n\tsay p\nend\n"
		"try\n\tMaterial.load(\"Materials/Missing.bmat\")\ncatch p\n\tsay p\nend\n"
		"try\n\tinstantiate(\"Prefabs/Missing.bprefab\")\ncatch p\n\tsay p\nend\n"
		"try\n\tis_key_down(\"NoSuchKey\")\ncatch p\n\tsay p\nend\n"
		"try\n\tset box.position to \"up\"\ncatch p\n\tsay p\nend\n"
		"try\n\tbox.look_at()\ncatch p\n\tsay p\nend\n"
		"set box.parent to box\n",
		"errors.forge");

	CHECK_EQ(h.Output, std::string(
		"Unknown component 'NotAComponent'. Valid components: Transform, MeshRenderer, Camera, DirectionalLight, PointLight, SpotLight, SkyLight, RigidBody, BoxCollider, SphereCollider, CapsuleCollider, MeshCollider, Script, AudioSource, AudioListener, Text, UICanvas, UIRect, UIImage, UILayout, UIScroll, UIButton, UIToggle, UISlider, UIDropdown, UITextInput\n"
		"The Transform component cannot be removed\n"
		"Material 'Materials/Missing.bmat' could not be loaded\n"
		"Prefab 'Prefabs/Missing.bprefab' could not be instantiated\n"
		"Unknown key 'NoSuchKey'\n"
		"'position' needs a vector, but got text.\n"
		"'look_at' needs 1 to 2 values, but was given 0.\n"));
	REQUIRE_EQ(h.Errors.size(), static_cast<size_t>(1));
	CHECK_NE(h.Errors[0].find("Cannot create a cycle in the hierarchy"), std::string::npos);
	CHECK_NE(h.Errors[0].find("errors.forge line 36"), std::string::npos);
}

TEST_CASE("ForgeScript handles to destroyed entities fail safely")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity doomed = scene->CreateEntity("Doomed");

	ForgeScriptHarness h(*scene);
	h.Machine.SetSharedGlobal("doomed", Scripting::MakeForgeScriptEntity(h.Machine, Scripting::ScriptEntity(doomed)));
	ForgeScript::Module* module = h.Machine.CreateModule("stale.forge");
	h.Machine.RunInModule(module, "set saved to doomed.transform\nsay doomed.valid\n", "stale.forge");
	scene->DestroyEntity(doomed);
	h.Machine.RunInModule(module,
		"say doomed.valid\n"
		"try\n\tsay doomed.name\ncatch p\n\tsay p\nend\n"
		"try\n\tsay saved.position\ncatch p\n\tsay p\nend\n",
		"stale.forge");

	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string(
		"true\n"
		"false\n"
		"This entity no longer exists (it was destroyed or its scene was unloaded)\n"
		"This entity no longer exists (it was destroyed or its scene was unloaded)\n"));
}

TEST_CASE("ForgeScript engine objects survive garbage collection")
{
	Ref<Scene> scene = CreateRef<Scene>();
	ForgeScript::VMConfig config;
	config.InitialGCThreshold = 1;
	ForgeScriptHarness h(*scene, config);
	h.Machine.Load(
		"set made to []\n"
		"for i from 1 to 20 do\n"
		"\tset e to create_entity(\"Item {i}\")\n"
		"\tset e.position to vector(i, 0, 0)\n"
		"\tadd e to made\n"
		"end\n"
		"say made.length\n"
		"say made.last.position\n"
		"say get_entities().length\n",
		"gc.forge");
	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string("20\n(20, 0, 0)\n20\n"));
}

TEST_CASE("ForgeScript engine phrases call the shared API")
{
	Ref<Scene> scene = CreateRef<Scene>();
	ForgeScriptHarness h(*scene);
	h.Machine.Load(
		"say user folder is user_folder()\n"
		"set folder to user folder\n"
		"say folder.length is above 0\n"
		// A variable named 'user' still works: the phrase needs both words.
		"set user to \"Ron\"\n"
		"say user\n"
		// Nothing has been drawn in this test, so both forms fail the same catchable way.
		"try\n\tsave screenshot to \"shot.png\"\ncatch problem\n\tsay problem\nend\n"
		"try\n\tsave_screenshot(\"shot.png\")\ncatch problem\n\tsay problem\nend\n",
		"phrases.forge");

	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	const size_t firstError = h.Output.find("true\ntrue\nRon\n");
	CHECK_EQ(firstError, 0u);
	const std::string rest = h.Output.substr(std::string("true\ntrue\nRon\n").size());
	const size_t split = rest.find('\n');
	REQUIRE(split != std::string::npos);
	CHECK_EQ(rest.substr(0, split + 1), rest.substr(split + 1));
}

TEST_CASE("ForgeScript UI phrases show, hide, enable, disable and focus widgets")
{
	Ref<Scene> scene = CreateRef<Scene>("UI phrases");
	Entity menu = scene->CreateEntity("Menu");
	menu.AddComponent<UICanvasComponent>();
	Entity play = scene->CreateEntity("Play");
	scene->SetParent(play, menu);
	play.AddComponent<UIRectComponent>().Focusable = true;
	scene->CreateEntity("Label");
	scene->OnRuntimeStart();

	ForgeScriptHarness h(*scene);
	h.Machine.Load(
		"set menu to find_entity(\"Menu\")\n"
		"set play to find_entity(\"Play\")\n"
		"hide menu\nsay menu.visible\nshow menu\nsay menu.visible\n"
		"disable play\nsay play.interactable\nenable play\nsay play.interactable\n"
		"say focus play\n"
		"disable play\nsay focus play\n"
		// 'show' is also ForgeScript's function for seeing a value; with brackets it still is.
		"show(\"text\")\n"
		"try\n\thide find_entity(\"Label\")\ncatch problem\n\tsay problem\nend\n"
		"try\n\thide 5\ncatch problem\n\tsay problem\nend\n",
		"ui_phrases.forge");

	INFO(h.AllErrors());
	CHECK(h.Errors.empty());
	CHECK_EQ(h.Output, std::string("false\ntrue\nfalse\ntrue\ntrue\nfalse\n\"text\"\n"
		"Entity 'Label' has no UIRect or UICanvas for 'visible'\n'hide' needs a UI entity, like: hide find_entity(\"Pause Menu\"). This is a number.\n"));
	scene->OnRuntimeStop();
}

TEST_CASE("Engine phrases are listed for tools")
{
	const std::vector<Scripting::ForgeScriptPhraseInfo> phrases = Scripting::GetForgeScriptPhrases();
	std::vector<std::string> patterns;
	for (const Scripting::ForgeScriptPhraseInfo& phrase : phrases)
	{
		patterns.push_back(phrase.Pattern);
		CHECK_FALSE(phrase.Doc.empty());
		CHECK_FALSE(phrase.Returns.empty());
	}
	std::vector<std::string> expected = { "save screenshot", "save screenshot to (path)", "user folder",
		"pause the game", "resume the game", "the game is paused", "keep (entity) across scenes", "show (widget)", "hide (widget)", "enable (widget)", "disable (widget)", "focus (widget)",
		"tag (entity) as (tag)", "untag (entity) as (tag)", "everything tagged (tag)", "push (entity) by (impulse)" };
	// Each tween phrase comes with "seconds" and "second", each with and without "with (style)".
	for (const char* start : { "move (entity) to (position)", "move (entity) by (offset)", "turn (entity) to (angles)", "turn (entity) by (angles)",
		"scale (entity) to (size)", "fade (entity) to (opacity)" })
	{
		for (const char* unit : { "seconds", "second" })
		{
			expected.push_back(std::string(start) + " over (seconds) " + unit);
			expected.push_back(std::string(start) + " over (seconds) " + unit + " with (style)");
		}
	}
	CHECK_EQ(patterns, expected);
	for (const Scripting::ForgeScriptPhraseInfo& phrase : phrases)
	{
		if (phrase.Pattern.find(" over (seconds) ") != std::string::npos)
		{
			CAPTURE(phrase.Pattern);
			CHECK(phrase.Returns == "Tween");
			CHECK(phrase.YieldsToCalls);
			CHECK(phrase.Doc.find("same as tween") != std::string::npos);
			CHECK((phrase.Doc.find(", style)") != std::string::npos) == (phrase.Pattern.find("with (style)") != std::string::npos));
		}
	}
}
