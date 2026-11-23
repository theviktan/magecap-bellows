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

TEST_CASE("ForgeScript help converts UTF-16 columns back, and makes edits from the last to the first")
{
	CHECK(ForgeScriptAssist::FromUtf16Column("say 1", 3) == 3);
	CHECK(ForgeScriptAssist::FromUtf16Column("say \"\xF0\x9F\x99\x82\" x", 8) == 7);		// The emoji's two units are one column
	CHECK(ForgeScriptAssist::FromUtf16Column("ab", 5) == 5);
	for (size_t column = 0; column < 10; column++)
		CHECK(ForgeScriptAssist::FromUtf16Column("a\xC3\xA9\xF0\x9F\x99\x82z", ForgeScriptAssist::ToUtf16Column("a\xC3\xA9\xF0\x9F\x99\x82z", column)) == column);

	using Edit = ForgeScriptAssist::TextEdit;
	// Given in any order; a later edit's range is still the original text's.
	const std::string text = "set hp to 1\r\nsay hp\n";
	CHECK(ForgeScriptAssist::ApplyEdits(text, { Edit { { 0, 4, 0, 6 }, "health" }, Edit { { 1, 4, 1, 6 }, "health" } }) == "set health to 1\r\nsay health\n");
	CHECK(ForgeScriptAssist::ApplyEdits(text, { Edit { { 1, 4, 1, 6 }, "health" }, Edit { { 0, 4, 0, 6 }, "health" } }) == "set health to 1\r\nsay health\n");
	// Columns are code points, and positions past a line or the text are held to it.
	CHECK(ForgeScriptAssist::ApplyEdits("say \"\xC3\xA9\" x\n", { Edit { { 0, 8, 0, 9 }, "y" } }) == "say \"\xC3\xA9\" y\n");
	CHECK(ForgeScriptAssist::ApplyEdits("ab\ncd", { Edit { { 0, 9, 0, 9 }, "!" } }) == "ab!\ncd");
	CHECK(ForgeScriptAssist::ApplyEdits("ab", { Edit { { 7, 0, 9, 0 }, "\nend" } }) == "ab\nend");
}

TEST_CASE("ForgeScript help finds every use of a name, renames it, and formats the script")
{
	REQUIRE(FileSystem::Exists(c_ApiFile));
	ForgeScriptAssist assist(c_ApiFile);
	const std::filesystem::path file = BellowsTests::GetTestOutputDirectory() / "Assist" / "Hero.forge";
	const std::string text =
		"set health to 3\n"
		"function heal(amount)\n"
		"\tset health to health + amount\n"
		"end\n"
		"function hurt(amount)\n"
		"\tset health to health - amount\n"
		"end\n";

	SUBCASE("References: every use of the script's 'health', not the two different 'amount's")
	{
		const std::vector<ForgeScriptAssist::Reference> uses = assist.FindReferences(file, text, 2, 6);
		REQUIRE(uses.size() == 5);
		CHECK(uses[0].Range == ForgeScriptAssist::TextRange { 0, 4, 0, 10 });
		CHECK(uses[0].LineText == "set health to 3");
		CHECK(uses[4].Range.StartLine == 5);
		CHECK(uses[0].File == file);
		CHECK(assist.FindReferences(file, text, 2, 25).size() == 2);	// heal's 'amount' only
	}
	SUBCASE("Rename changes each use, and says why when it can't")
	{
		std::string name, error;
		const std::optional<ForgeScriptAssist::TextRange> range = assist.PrepareRename(file, text, 0, 6, name, error);
		REQUIRE(range);
		CHECK(name == "health");
		CHECK(error.empty());

		const ForgeScriptAssist::RenameResult rename = assist.Rename(file, text, 0, 6, "hp");
		CHECK(rename.Error.empty());
		REQUIRE(rename.Edits.count(file) == 1);
		const std::string renamed = ForgeScriptAssist::ApplyEdits(text, rename.Edits.at(file));
		CHECK(renamed.find("health") == std::string::npos);
		CHECK(renamed.find("set hp to hp + amount") != std::string::npos);

		const ForgeScriptAssist::RenameResult keyword = assist.Rename(file, text, 0, 6, "end");
		CHECK_FALSE(keyword.Error.empty());
		CHECK(keyword.Edits.empty());
		const ForgeScriptAssist::RenameResult taken = assist.Rename(file, text, 0, 6, "heal");
		CHECK_FALSE(taken.Error.empty());

		// A built-in can't be renamed here.
		const std::string calls = "say abs(-2)\n";
		CHECK_FALSE(assist.PrepareRename(file, calls, 0, 5, name, error));
	}
	SUBCASE("Format lays the script out, and refuses one with mistakes")
	{
		std::string error;
		const std::optional<std::string> formatted = assist.Format(file, "function heal(amount)\nset health to amount\n    end\n", error);
		REQUIRE(formatted);
		CHECK(error.empty());
		CHECK(*formatted == "function heal(amount)\n\tset health to amount\nend\n");
		const std::optional<std::string> same = assist.Format(file, text, error);
		REQUIRE(same);
		CHECK(*same == text);
		CHECK_FALSE(assist.Format(file, "function broken(\n", error));
		CHECK_FALSE(error.empty());
	}
}
