#include <doctest/doctest.h>

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include "ForgeScript/ForgeScript.h"

#include <string>
#include <vector>

using namespace Bellows;

TEST_CASE("Python can't give the engine a vector with NaN or infinity")
{
	REQUIRE(ScriptEngine::IsInitialized());
	Scene scene("Finite");
	Entity box = scene.CreateEntity("Box");
	box.GetComponent<TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);
	ScriptEngine::SetContextScene(&scene);

	std::string output;
	REQUIRE(ScriptEngine::ExecuteSource("box = bellows.find_entity('Box')\nnan = float('nan')\ninf = float('inf')", output));
	const std::vector<std::pair<const char*, const char*>> cases = {
		{ "box.transform.position = bellows.Vec3(nan, 0, 0)", "Transform.position needs ordinary numbers, not NaN or infinity" },
		{ "box.transform.translation = (0, inf, 0)", "Transform.translation needs ordinary numbers" },
		{ "box.transform.rotation = bellows.Vec3(0, 0, -inf)", "Transform.rotation needs ordinary numbers" },
		{ "box.transform.scale = [1, nan, 1]", "Transform.scale needs ordinary numbers" },
		{ "box.position = bellows.Vec3(nan, nan, nan)", "Entity.position needs ordinary numbers" },
		{ "bellows.raycast(bellows.Vec3(0, 0, 0), bellows.Vec3(0, nan, 0), 10)", "raycast needs ordinary numbers" },
		{ "bellows.shake_camera(nan, 0.4)", "shake_camera needs ordinary numbers, not NaN or infinity" },
		{ "bellows.shake_camera(0.3, inf)", "shake_camera needs ordinary numbers" },
	};
	for (const auto& [line, message] : cases)
	{
		CAPTURE(line);
		CHECK_FALSE(ScriptEngine::ExecuteSource(line, output));
		CHECK(output.find(std::string("ValueError: ") + message) != std::string::npos);
	}
	const TransformComponent& transform = box.GetComponent<TransformComponent>();
	CHECK(transform.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(transform.Scale == glm::vec3(1.0f));

	// Ordinary vectors still go through.
	CHECK(ScriptEngine::ExecuteSource("box.position = bellows.Vec3(4, 5, 6)", output));
	CHECK(transform.Translation == glm::vec3(4.0f, 5.0f, 6.0f));
	ScriptEngine::SetContextScene(nullptr);
}

TEST_CASE("ForgeScript can't give the engine a vector with NaN or infinity")
{
	Scene scene("Finite");
	Entity box = scene.CreateEntity("Box");
	ForgeScript::VM vm;
	std::vector<std::string> errors;
	vm.SetErrorHandler([&errors](const ForgeScript::ScriptError& error) { errors.push_back(error.ToString()); });
	Scripting::RegisterForgeScriptApi(vm);
	Scripting::SetContextScene(&scene);
	ForgeScript::Module* module = vm.CreateModule("finite.forge");
	vm.SetGlobal(module, "box", Scripting::MakeForgeScriptEntity(vm, Scripting::ScriptEntity(box)));

	// 1e308 * 10 is infinity, and 1e39 is too big for the engine's floats.
	for (const char* line : { "set box.position to vector(1e308 * 10, 0, 0)", "set box.scale to vector(1, 1e39, 1)",
		"set box.rotation to [0, 0, -1e308 * 10]", "set box.transform.position to vector(0, 0, 1e39)",
		"shake_camera(1e308 * 10, 1)", "shake the camera by 0.3 over 1e308 * 10 seconds" })
	{
		CAPTURE(line);
		errors.clear();
		vm.RunInModule(module, line, "finite.forge");
		REQUIRE(errors.size() == 1);
		CHECK(errors[0].find("needs ordinary numbers, not NaN or infinity") != std::string::npos);
	}
	CHECK(box.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f));
	CHECK(box.GetComponent<TransformComponent>().Scale == glm::vec3(1.0f));

	errors.clear();
	vm.RunInModule(module, "set box.position to vector(1e30, 0, 0)", "finite.forge");
	CHECK(errors.empty());
	Scripting::SetContextScene(nullptr);
}
