#include "TestFramework.h"

#include "ForgeScript/Core/Suggest.h"

#include <string_view>
#include <vector>

using namespace ForgeScript;

TEST_CASE("ClosestName finds a typo's intended name")
{
	const std::vector<std::string_view> names = { "health", "greet", "length", "add", "upper", "speed" };
	CHECK(ClosestName("helth", names) == "health");   // A missing letter.
	CHECK(ClosestName("lenght", names) == "length");  // Two letters swapped count as one edit.
	CHECK(ClosestName("gret", names) == "greet");
	CHECK(ClosestName("Health", names) == "health");  // Letter case is ignored.
	CHECK(ClosestName("HEALTH", names) == "health");
	CHECK(ClosestName("spede", names) == "speed");
}

TEST_CASE("ClosestName stays quiet when nothing is close")
{
	const std::vector<std::string_view> names = { "health", "x", "y", "is", "add" };
	CHECK(ClosestName("gold_coins", names).empty());
	CHECK(ClosestName("z", names).empty());       // One letter: another letter is another name.
	CHECK(ClosestName("it", names).empty());      // Two letters: changing one is another name too.
	CHECK(ClosestName("ad", names) == "add");     // But a letter missing from a three-letter name is a typo.
	CHECK(ClosestName("health", names).empty());  // The name itself is never suggested.
	CHECK(ClosestName("helth", {}).empty());
	CHECK(ClosestName("", names).empty());
}

TEST_CASE("ClosestName allows more edits in longer names")
{
	const std::vector<std::string_view> names = { "player_score", "speed" };
	CHECK(ClosestName("plyer_scroe", names) == "player_score");  // 2 edits in 12 letters.
	CHECK(ClosestName("spd", names).empty());                    // 2 edits in 3 letters is too many.
}

TEST_CASE("ClosestName prefers the closest name, then the alphabetically first")
{
	CHECK(ClosestName("heath", { "heat", "health" }) == "health");    // Both are 1 edit away: 'health' comes first.
	CHECK(ClosestName("score", { "scores", "scored" }) == "scored");  // Ties are stable whatever the order.
	CHECK(ClosestName("score", { "scored", "scores" }) == "scored");
	CHECK(ClosestName("helath", { "health", "heal" }) == "health");  // 1 edit (a swap) beats 2.
}

TEST_CASE("ClosestName skips the host's own __ names")
{
	CHECK(ClosestName("__helth", { "__health" }).empty());
}

TEST_CASE("DidYouMean writes the hint to append")
{
	CHECK(DidYouMean("helth", { "health" }) == " Did you mean 'health'?");
	CHECK(DidYouMean("gold", { "health" }).empty());
}
