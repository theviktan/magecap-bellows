#include "TestFramework.h"

#include "ForgeScript/Tools/Formatter.h"

#include <filesystem>
#include <fstream>
#include <sstream>

// forgescript --format: FormatScript lays a script out, changing only whitespace around lines.

using namespace ForgeScript;

namespace {

	std::string Formatted(const std::string& source)
	{
		const FormatResult result = FormatScript(source);
		INFO(result.Error);
		CHECK(result.Error.empty());
		return result.Text;
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

}

TEST_CASE("FormatterIndentsBlocksWithTabs")
{
	CHECK_EQ(Formatted(
		"function heal(amount)\n"
		"    if amount is above 0 then\n"
		"  add amount to health\n"
		"        end\n"
		"   end\n"),
		std::string(
		"function heal(amount)\n"
		"\tif amount is above 0 then\n"
		"\t\tadd amount to health\n"
		"\tend\n"
		"end\n"));
	// A block opened and closed on one line changes nothing.
	CHECK_EQ(Formatted("if x then say 1 end\nsay 2\n"), std::string("if x then say 1 end\nsay 2\n"));
	// Every kind of block.
	CHECK_EQ(Formatted(
		"for each enemy in enemies do\nsay enemy\nend\n"
		"repeat 3 times\nsay 1\nend\n"
		"while x is below 3 do\nadd 1 to x\nend\n"
		"when hit by damage\nsay damage\nend\n"
		"kind Enemy\nset health to 1\nfunction hurt()\ntake 1 from my health\nend\nend\n"
		"every 2 seconds\nsay \"tick\"\nend\n"
		"test \"adds\"\nexpect 1 + 1 is 2\nend\n"),
		std::string(
		"for each enemy in enemies do\n\tsay enemy\nend\n"
		"repeat 3 times\n\tsay 1\nend\n"
		"while x is below 3 do\n\tadd 1 to x\nend\n"
		"when hit by damage\n\tsay damage\nend\n"
		"kind Enemy\n\tset health to 1\n\tfunction hurt()\n\t\ttake 1 from my health\n\tend\nend\n"
		"every 2 seconds\n\tsay \"tick\"\nend\n"
		"test \"adds\"\n\texpect 1 + 1 is 2\nend\n"));
}

TEST_CASE("FormatterPutsTheMiddleOfABlockOneLevelOut")
{
	CHECK_EQ(Formatted(
		"if hp is above 50 then\n\tsay \"healthy\"\n\telse if hp is above 20 then\n\tsay \"hurt\"\n\totherwise\n\tsay \"critical\"\nend\n"),
		std::string("if hp is above 50 then\n\tsay \"healthy\"\nelse if hp is above 20 then\n\tsay \"hurt\"\notherwise\n\tsay \"critical\"\nend\n"));
	CHECK_EQ(Formatted("match weapon\n\tis \"sword\" then\n\t\tsay \"Slash!\"\n\totherwise\n\t\tsay \"Thud.\"\nend\n"),
		std::string("match weapon\nis \"sword\" then\n\tsay \"Slash!\"\notherwise\n\tsay \"Thud.\"\nend\n"));
	CHECK_EQ(Formatted("try\nfail \"x\"\ncatch problem\nsay problem\nfinally\nsay \"done\"\nend\n"),
		std::string("try\n\tfail \"x\"\ncatch problem\n\tsay problem\nfinally\n\tsay \"done\"\nend\n"));
	// 'otherwise' inside a line picks a value; 'finally' outside a try is just a name.
	CHECK_EQ(Formatted("if a then\nset b to 1 if c otherwise 2\nend\nset finally to 3\n"),
		std::string("if a then\n\tset b to 1 if c otherwise 2\nend\nset finally to 3\n"));
}

TEST_CASE("FormatterIndentsBracketsAndCarriedOnLines")
{
	CHECK_EQ(Formatted("set items to [\n1,\n2\n]\n"), std::string("set items to [\n\t1,\n\t2\n]\n"));
	CHECK_EQ(Formatted("set map to {\nstart: { line: \"Hi\",\nnext: [] }\n}\n"),
		std::string("set map to {\n\tstart: { line: \"Hi\",\n\t\tnext: [] }\n}\n"));
	// A function inside brackets: its body one level in, and its 'end' with the closing bracket.
	CHECK_EQ(Formatted("set doubled to items.map(function(x)\nreturn x * 2\nend)\n"),
		std::string("set doubled to items.map(function(x)\n\treturn x * 2\nend)\n"));
	// A statement carried on after an operator or a '\'.
	CHECK_EQ(Formatted("set total to 2 +\n3\nsay total\n"), std::string("set total to 2 +\n\t3\nsay total\n"));
	CHECK_EQ(Formatted("say \"a\" \\\n\"b\"\n"), std::string("say \"a\" \\\n\t\"b\"\n"));
}

TEST_CASE("FormatterNeverTouchesTextOrComments")
{
	// Long text keeps every line as written, and so does a block comment.
	const std::string longText = "if x then\nset note to \"\"\"\n    Line A  \n      Line B\n    \"\"\"\nend\n";
	CHECK_EQ(Formatted(longText), std::string("if x then\n\tset note to \"\"\"\n    Line A  \n      Line B\n    \"\"\"\nend\n"));
	const std::string comment = "if x then\n#[ a note\n   kept   \n]#\nsay 1\nend\n";
	CHECK_EQ(Formatted(comment), std::string("if x then\n\t#[ a note\n   kept   \n]#\n\tsay 1\nend\n"));
	// Spacing inside a line, and inside quotes, stays.
	CHECK_EQ(Formatted("say   \"a    b\"   +   \"c\"\n"), std::string("say   \"a    b\"   +   \"c\"\n"));
	// Comment lines move with the code they sit above.
	CHECK_EQ(Formatted("if x then\n        # why\nsay 1\nend\n"), std::string("if x then\n\t# why\n\tsay 1\nend\n"));
	// 'end' as a property name and 'is a function' as a type test open and close nothing.
	CHECK_EQ(Formatted("if x then\nsay map.end\nsay f is a function\nend\n"),
		std::string("if x then\n\tsay map.end\n\tsay f is a function\nend\n"));
}

TEST_CASE("FormatterTidiesTheEndsOfLines")
{
	CHECK_EQ(Formatted("say 1   \n\n\n\n\nsay 2\t\n\n\n"), std::string("say 1\n\n\nsay 2\n"));
	CHECK_EQ(Formatted("say 1"), std::string("say 1\n"));
	CHECK_EQ(Formatted(""), std::string(""));
	// Windows line breaks stay Windows line breaks.
	CHECK_EQ(Formatted("if x then\r\nsay 1  \r\nend\r\n"), std::string("if x then\r\n\tsay 1\r\nend\r\n"));
	// A comment's line end is tidied too.
	CHECK_EQ(Formatted("say 1   # note   \n"), std::string("say 1   # note\n"));
	CHECK_FALSE(FormatScript("say 1\n").Changed);
	CHECK(FormatScript("say 1 \n").Changed);
}

TEST_CASE("FormatterLeavesScriptsItCantFollowAlone")
{
	auto refused = [](const std::string& source, int line, const std::string& reason)
	{
		const FormatResult result = FormatScript(source);
		CHECK_EQ(result.Text, source);
		CHECK_FALSE(result.Changed);
		CHECK_EQ(result.ErrorLine, line);
		CHECK_CONTAINS(result.Error, reason);
	};
	refused("if x then\n  say 1\n", 1, "missing its 'end'");
	refused("say 1\nend\n", 2, "no block to close");
	refused("set a to [1,\n  2\n", 1, "never closed");
	refused("say 1)\n", 1, "no opening one");
	refused("  say \"never closed\n", 1, "can't read");
	// A '\' with spaces after it joins nothing, which the lexer reports; trimming them would join lines.
	refused("say 1 \\  \nsay 2\n", 1, "can't read");
}

TEST_CASE("FormatterIsSteadyOnEveryTestScript")
{
	// Formatting twice gives what formatting once gave, and the result doesn't depend on how the
	// script was indented before.
	size_t formatted = 0;
	for (const auto& entry : std::filesystem::directory_iterator(FORGESCRIPT_TEST_SCRIPTS_DIR))
	{
		if (entry.path().extension() != ".forge")
			continue;
		const std::string source = ReadText(entry.path());
		const FormatResult once = FormatScript(source);
		if (!once.Error.empty())
			continue; // 16_compile_error.forge is broken on purpose
		INFO(entry.path().filename().string());
		CHECK_EQ(FormatScript(once.Text).Text, once.Text);
		CHECK_FALSE(FormatScript(once.Text).Changed);
		if (source.find("\"\"\"") == std::string::npos && source.find("#[") == std::string::npos)
		{
			std::string flat;
			std::istringstream lines(source);
			for (std::string line; std::getline(lines, line);)
				flat += line.substr(std::min(line.find_first_not_of(" \t"), line.size())) + "\n";
			CHECK_EQ(FormatScript(flat).Text, once.Text);
		}
		formatted++;
	}
	CHECK(formatted > 40);
}

TEST_CASE("FormatterIndentsProperties")
{
	// Inside a kind, a property's block is indented; outside one, a 'property' line is a plain statement.
	const FormatResult result = FormatScript("kind K\nproperty v as number\nreturn 1\nend\nkeep my a between 1 and 2\nwhen my a is set to n\nset my a to n\nend\nend\nproperty 7\n");
	REQUIRE(result.Error.empty());
	CHECK_EQ(result.Text, std::string("kind K\n\tproperty v as number\n\t\treturn 1\n\tend\n\tkeep my a between 1 and 2\n\twhen my a is set to n\n\t\tset my a to n\n\tend\nend\nproperty 7\n"));
}

TEST_CASE("FormatterIndentsContracts")
{
	// A contract's 'needs' lines are indented; 'contract' with a value after it isn't a contract.
	const FormatResult result = FormatScript("contract Damageable\nneeds health\nneeds take_hit(amount)\nend\nkind Crate follows Damageable\nset health to 1\nfunction take_hit(amount)\nend\nend\nset contract to 2\n");
	REQUIRE(result.Error.empty());
	CHECK_EQ(result.Text, std::string("contract Damageable\n\tneeds health\n\tneeds take_hit(amount)\nend\nkind Crate follows Damageable\n\tset health to 1\n\tfunction take_hit(amount)\n\tend\nend\nset contract to 2\n"));
}

TEST_CASE("FormatterKeepsTypesOnVariablesAndFields")
{
	// 'as' types on set and shared lines (Roadmap 3.0 item 5) are laid out like any other line.
	const FormatResult result = FormatScript("set names as list of text to []\nkind Ship\nset fuel as number to 5\nshared count as number is 0\nend\n");
	REQUIRE(result.Error.empty());
	CHECK_EQ(result.Text, std::string("set names as list of text to []\nkind Ship\n\tset fuel as number to 5\n\tshared count as number is 0\nend\n"));
}
