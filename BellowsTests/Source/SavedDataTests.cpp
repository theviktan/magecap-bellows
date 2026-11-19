#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ScriptApi.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <string>

using namespace Bellows;
using namespace Bellows::Scripting;

namespace {

	// Saves go to a test folder for a project named "Save Game".
	struct SavedDataFixture
	{
		std::filesystem::path UserData = BellowsTests::GetTestOutputDirectory() / "SavedDataUser";
		std::filesystem::path Saves = UserData / "MageCapBellows" / "Games" / "Save Game" / "Saves";
		Ref<Project> GameProject;

		SavedDataFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(UserData, error);
			Platform::SetEnvVar("BELLOWS_USER_DATA_DIR", UserData.string());
			GameProject = Project::Create(BellowsTests::GetTestOutputDirectory() / "SavedDataProject", "Save Game");
		}

		~SavedDataFixture()
		{
			Platform::SetEnvVar("BELLOWS_USER_DATA_DIR", "");
		}
	};

}

TEST_CASE("save_data and load_data keep every kind of value, in order")
{
	SavedDataFixture fixture;
	REQUIRE(fixture.GameProject);
	CHECK_FALSE(HasData("starfall"));
	CHECK(LoadData("starfall", ScriptValue(std::string("fresh start"))) == ScriptValue(std::string("fresh start")));

	const ScriptValue save(ScriptValueMap {
		{ "credits", ScriptValue(1250.5) },
		{ "station", ScriptValue(std::string("Starfall")) },
		{ "docked", ScriptValue(true) },
		{ "captain", ScriptValue() },
		{ "spawn", ScriptValue(glm::vec3(1.0f, -2.5f, 3.0f)) },
		{ "ships", ScriptValue(ScriptValueList { ScriptValue(std::string("Comet")), ScriptValue(ScriptValueMap { { "fuel", ScriptValue(0.75) } }) }) },
		{ "a key with spaces", ScriptValue(ScriptValueList {}) } });
	REQUIRE(SaveData("starfall", save));
	CHECK(HasData("starfall"));
	CHECK(FileSystem::Exists(fixture.Saves / "starfall.json"));
	// No temporary file is left behind.
	CHECK_FALSE(FileSystem::Exists(fixture.Saves / "starfall.json.tmp"));
	CHECK(LoadData("starfall", ScriptValue()) == save);

	// Saving again replaces the old value.
	REQUIRE(SaveData("starfall", ScriptValue(3.0)));
	CHECK(LoadData("starfall", ScriptValue()) == ScriptValue(3.0));

	CHECK(DeleteData("starfall"));
	CHECK_FALSE(HasData("starfall"));
	CHECK_FALSE(DeleteData("starfall"));
}

TEST_CASE("save_data refuses what it can't keep")
{
	SavedDataFixture fixture;
	Ref<Scene> scene = CreateRef<Scene>();
	const ScriptEntity ship(scene->CreateEntity("Ship"));
	CHECK_THROWS_WITH_AS(SaveData("game", ScriptValue(ScriptValueList { ScriptValue(ship) })), doctest::Contains("entities only exist while the game plays"), ScriptValueError);
	CHECK_THROWS_WITH_AS(SaveData("game", ScriptValue(std::numeric_limits<double>::infinity())), doctest::Contains("isn't finite"), ScriptValueError);
	CHECK_THROWS_WITH_AS(SaveData("game", ScriptValue(ScriptValueMap { { "__vector", ScriptValue(1.0) } })), doctest::Contains("marks saved vectors"), ScriptValueError);
	// Nothing was written by a refused save.
	CHECK_FALSE(HasData("game"));
}

TEST_CASE("Save names stay plain files in the Saves folder")
{
	SavedDataFixture fixture;
	CHECK_NOTHROW(SaveData("Slot 1_auto-save", ScriptValue(1.0)));
	CHECK(FileSystem::Exists(fixture.Saves / "Slot 1_auto-save.json"));

	CHECK_THROWS_WITH_AS(SaveData("", ScriptValue()), doctest::Contains("1 to 64 characters"), ScriptValueError);
	CHECK_THROWS_WITH_AS(SaveData(std::string(65, 'a'), ScriptValue()), doctest::Contains("1 to 64 characters"), ScriptValueError);
	CHECK_THROWS_WITH_AS(LoadData("../escape", ScriptValue()), doctest::Contains("has '.'"), ScriptValueError);
	CHECK_THROWS_WITH_AS(HasData("a/b"), doctest::Contains("has '/'"), ScriptValueError);
	CHECK_THROWS_WITH_AS(DeleteData(" padded"), doctest::Contains("start or end with a space"), ScriptValueError);
	CHECK_THROWS_WITH_AS(SaveData("con", ScriptValue()), doctest::Contains("Windows keeps it for a device"), ScriptValueError);
	CHECK_THROWS_WITH_AS(SaveData("LPT1", ScriptValue()), doctest::Contains("Windows keeps it for a device"), ScriptValueError);
	CHECK_NOTHROW(SaveData("COM", ScriptValue()));
}

TEST_CASE("A damaged save is kept aside and the default comes back")
{
	SavedDataFixture fixture;
	REQUIRE(FileSystem::WriteTextFile(fixture.Saves / "broken.json", "{ \"credits\": 12"));
	CHECK(LoadData("broken", ScriptValue(5.0)) == ScriptValue(5.0));
	CHECK_FALSE(HasData("broken"));
	const std::optional<std::string> kept = FileSystem::ReadTextFile(fixture.Saves / "broken.damaged.json");
	REQUIRE(kept);
	CHECK_EQ(*kept, std::string("{ \"credits\": 12"));

	// A vector key holding something else is damage too, not a crash.
	REQUIRE(FileSystem::WriteTextFile(fixture.Saves / "odd.json", "{ \"__vector\": [1, 2] }"));
	CHECK(LoadData("odd", ScriptValue()) == ScriptValue());
	CHECK(FileSystem::Exists(fixture.Saves / "odd.damaged.json"));

	// A save written by hand with integers and an empty file loads what JSON allows.
	REQUIRE(FileSystem::WriteTextFile(fixture.Saves / "hand.json", "[1, 2, {\"x\": null}]"));
	CHECK(LoadData("hand", ScriptValue()) == ScriptValue(ScriptValueList { ScriptValue(1.0), ScriptValue(2.0), ScriptValue(ScriptValueMap { { "x", ScriptValue() } }) }));
	REQUIRE(FileSystem::WriteTextFile(fixture.Saves / "empty.json", ""));
	CHECK(LoadData("empty", ScriptValue(true)) == ScriptValue(true));
	CHECK(FileSystem::Exists(fixture.Saves / "empty.damaged.json"));
}

TEST_CASE("real_time is the real clock in seconds since 1970")
{
	const double now = GetRealTime();
	const double expected = static_cast<double>(std::time(nullptr));
	CHECK(std::abs(now - expected) < 5.0);
	CHECK(GetRealTime() >= now);
}
