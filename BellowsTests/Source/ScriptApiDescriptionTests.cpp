#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scripting/ScriptApiDescription.h"

#include <algorithm>

using namespace Bellows;

namespace {

	const Json* FindNamed(const Json& list, const std::string& name)
	{
		auto it = std::find_if(list.begin(), list.end(), [&](const Json& item) { return item.value("name", "") == name; });
		return it != list.end() ? &*it : nullptr;
	}

}

TEST_CASE("The script API description names every function, type and parameter")
{
	const Json api = Scripting::DescribeScriptApi();
	CHECK(api["version"] == 1);
	CHECK(api["phrases"].is_array());

	const Json* playSound = FindNamed(api["functions"], "play_sound");
	REQUIRE(playSound);
	CHECK((*playSound)["returns"] == "bool");
	REQUIRE((*playSound)["params"].size() == 3);
	CHECK((*playSound)["params"][0] == Json { { "name", "path" }, { "type", "str" } });
	CHECK((*playSound)["params"][2]["default"] == "Effects");

	const Json* instantiate = FindNamed(api["functions"], "instantiate");
	REQUIRE(instantiate);
	CHECK((*instantiate)["params"][1]["type"] == "Vec3 | None");
	CHECK((*instantiate)["params"][1]["default"].is_null());
	CHECK((*instantiate)["returns"] == "Entity");

	const Json* material = FindNamed(api["types"], "Material");
	REQUIRE(material);
	CHECK((*material)["constructor"]["params"][0]["name"] == "name");
	const Json* load = FindNamed((*material)["members"], "load");
	REQUIRE(load);
	CHECK((*load)["kind"] == "static");
	CHECK((*load)["returns"] == "Material");
	const Json* path = FindNamed((*material)["members"], "path");
	REQUIRE(path);
	CHECK((*path)["readonly"] == true);

	const Json* entity = FindNamed(api["types"], "Entity");
	REQUIRE(entity);
	const Json* lookAt = FindNamed((*entity)["members"], "look_at");
	REQUIRE(lookAt);
	CHECK((*lookAt)["kind"] == "method");

	// Every type is known: an unknown C++ type would show up as "Any".
	CHECK(Scripting::WriteScriptApiDescription().find("\"Any") == std::string::npos);
}

TEST_CASE("Resources/forgescript-api.json is up to date")
{
	const std::filesystem::path file = BellowsTests::GetSourceDirectory() / "Resources" / "forgescript-api.json";
	const std::optional<std::string> checkedIn = FileSystem::ReadTextFile(file);
	REQUIRE(checkedIn);
	std::string normalized = *checkedIn;
	std::erase(normalized, '\r');
	INFO("The script API changed: run BellowsEditor --write-forgescript-api Resources/forgescript-api.json and commit the file");
	CHECK(normalized == Scripting::WriteScriptApiDescription());
}
