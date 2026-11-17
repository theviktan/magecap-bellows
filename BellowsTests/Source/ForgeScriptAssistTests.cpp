#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/FileSystem.h"
#include "ForgeScriptAssist.h"

#include <algorithm>

using namespace Bellows;

namespace {

	const std::filesystem::path c_ApiFile = BellowsTests::GetSourceDirectory() / "Resources" / "forgescript-api.json";

	bool HasLabel(const std::vector<ForgeScriptAssist::Suggestion>& suggestions, const std::string& label)
	{
		return std::any_of(suggestions.begin(), suggestions.end(), [&](const ForgeScriptAssist::Suggestion& s) { return s.Label == label; });
	}

}

TEST_CASE("ForgeScript help converts positions and server text for the code editor")
{
	CHECK(ForgeScriptAssist::ToUtf16Column("say 1", 3) == 3);
	// "é" is one UTF-16 unit, an emoji two; columns past the end of the line count on.
	CHECK(ForgeScriptAssist::ToUtf16Column("say \"\xC3\xA9\" x", 7) == 7);
	CHECK(ForgeScriptAssist::ToUtf16Column("say \"\xF0\x9F\x99\x82\" x", 7) == 8);
	CHECK(ForgeScriptAssist::ToUtf16Column("ab", 5) == 5);
	CHECK(ForgeScriptAssist::MarkdownToText("```forge\nplay_sound(path: str) -> bool\n```\n\nPlays a sound\n\n---\n\n```forge\nmore\n```")
		== "play_sound(path: str) -> bool\n\nPlays a sound\n\nmore");
}

TEST_CASE("ForgeScript help completes engine functions, phrases and the script's own names")
{
	REQUIRE(FileSystem::Exists(c_ApiFile));
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "AssistProject";
	REQUIRE(FileSystem::WriteTextFile(folder / "Assist.bproject", R"({ "Name": "Assist", "AssetDirectory": "Assets" })"));
	REQUIRE(FileSystem::WriteTextFile(folder / "Assets" / "Scripts" / "Helpers.forge", "# Doubles a number.\nfunction double(n)\n\treturn n * 2\nend\n"));
	const std::filesystem::path script = folder / "Assets" / "Scripts" / "Player.forge";

	ForgeScriptAssist assist(c_ApiFile);
	std::string text = "use \"Scripts/Helpers\"\nset speed to 2.0\npla\n";
	const auto engine = assist.Complete(script, text, 2, 3, "pla");
	REQUIRE_FALSE(engine.empty());
	CHECK(HasLabel(engine, "play_sound"));
	CHECK(std::all_of(engine.begin(), engine.end(), [](const auto& s) { return s.Label.find("pla") != std::string::npos; }));

	// The text is synced on every call: names the user just typed and names from used scripts appear.
	text = "use \"Scripts/Helpers\"\nset speed to 2.0\nsay spe\nsay dou\nsave\n";
	CHECK(HasLabel(assist.Complete(script, text, 2, 7, "spe"), "speed"));
	CHECK(HasLabel(assist.Complete(script, text, 3, 7, "dou"), "double"));
	const auto phrases = assist.Complete(script, text, 4, 4, "save");
	CHECK(std::any_of(phrases.begin(), phrases.end(), [](const auto& s) { return s.Label.rfind("save screenshot", 0) == 0; }));

	// Hover: typed signatures and docs for engine functions and the script's own functions.
	text = "use \"Scripts/Helpers\"\nplay_sound(\"x.wav\")\nsay double(2)\n";
	const std::string sound = assist.Hover(script, text, 1, 3);
	// Engine types show their ForgeScript names (the API file uses Python's).
	CHECK(sound.find("play_sound(path: text, volume: number = 1, bus: text = \"Effects\") -> yes/no") != std::string::npos);
	CHECK(sound.find("```") == std::string::npos);
	CHECK(assist.Hover(script, text, 2, 6).find("Doubles a number.") != std::string::npos);
	CHECK(assist.Hover(script, text, 3, 0).empty());		// The empty last line
	assist.Close(script);
	assist.Close(script);
}

TEST_CASE("ForgeScript help still knows the language without an API file")
{
	ForgeScriptAssist assist(BellowsTests::GetTestOutputDirectory() / "Missing" / "forgescript-api.json");
	const std::filesystem::path script = BellowsTests::GetTestOutputDirectory() / "NoApi.forge";
	const auto suggestions = assist.Complete(script, "fun\n", 0, 3, "fun");
	CHECK(HasLabel(suggestions, "function"));
	CHECK_FALSE(HasLabel(assist.Complete(script, "pla\n", 0, 3, "pla"), "play_sound"));
}
