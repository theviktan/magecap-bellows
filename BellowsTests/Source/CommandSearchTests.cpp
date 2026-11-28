#include <doctest/doctest.h>

#include "CommandSearch.h"

using namespace Bellows;
using namespace Bellows::CommandSearch;

TEST_CASE("The title bar's search scores whole words, word starts and scattered letters")
{
	CHECK(Score("Save Scene", "") == 0);
	CHECK(Score("Save Scene", "xyz") == -1);
	CHECK(Score("Save Scene", "enecs") == -1);			// Letters out of order don't match

	// Any letter case; spaces in the query are ignored.
	CHECK(Score("Save Scene", "SAVE") > 0);
	CHECK(Score("Save Scene", "sv sc") > 0);

	// A prefix beats a word start, which beats the middle of a word, which beats scattered letters.
	const int prefix = Score("Scene View", "sce");
	const int wordStart = Score("Save Scene", "sce");
	const int middle = Score("Ascent", "sce");
	const int scattered = Score("Save Scene", "ssc");
	CHECK(prefix > wordStart);
	CHECK(wordStart > middle);
	CHECK(middle > scattered);
	CHECK(scattered > 0);

	// Scattered letters landing on word starts beat ones in the middle of words.
	CHECK(Score("Docking Bay A", "dba") > Score("Dodgeball", "dba"));
	// CamelCase starts a word too.
	CHECK(Score("DockingBay", "db") > Score("Dumbbell", "db"));
	// A shorter text wins a tie of the same kind of match.
	CHECK(Score("Play", "pl") > Score("Playground", "pl"));
}

TEST_CASE("The title bar's search ranks its items and lists commands for an empty query")
{
	std::vector<Item> items = {
		{ ResultKind::Command, "Save Scene", "Ctrl+S", true },
		{ ResultKind::Command, "Play", "Ctrl+P", true },
		{ ResultKind::Entity, "Player", "", true },
		{ ResultKind::Scene, "Playground", "Scenes", true },
		{ ResultKind::Script, "PlayerController", "Scripts", true },
		{ ResultKind::Command, "Export Game...", "", false },
	};

	// Nothing typed: the commands, in their own order (disabled ones too, shown dimmed).
	CHECK(Rank(items, "") == std::vector<size_t> { 0, 1, 5 });
	CHECK(Rank(items, "   ") == std::vector<size_t> { 0, 1, 5 });

	// "play": the exact command first, then longer names by length; kinds break ties.
	const std::vector<size_t> play = Rank(items, "play");
	REQUIRE(play.size() == 4);
	CHECK(play[0] == 1);
	CHECK(play[1] == 2);
	CHECK(play[2] == 3);
	CHECK(play[3] == 4);

	// The limit keeps the best.
	CHECK(Rank(items, "play", 2) == std::vector<size_t> { 1, 2 });
	CHECK(Rank(items, "nothing like it").empty());

	// Same score, same name length: commands come before entities, then by name.
	std::vector<Item> tie = {
		{ ResultKind::Entity, "Door", "", true },
		{ ResultKind::Command, "Dock", "", true },
	};
	CHECK(Rank(tie, "do") == std::vector<size_t> { 1, 0 });

	CHECK(std::string(KindName(ResultKind::Command)) == "command");
	CHECK(std::string(KindName(ResultKind::Entity)) == "entity");
	CHECK(std::string(KindName(ResultKind::Scene)) == "scene");
	CHECK(std::string(KindName(ResultKind::Script)) == "script");
}
