#include "TestFramework.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

// VM::FindMistakes: every mistake in a script, for editors and 'forgescript --check'.

using namespace ForgeScript;

namespace {

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::stringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	void UseTestModules(VM& vm)
	{
		const std::filesystem::path folder = std::filesystem::path(FORGESCRIPT_TEST_SCRIPTS_DIR) / "modules";
		vm.SetModuleLoader([folder](const std::string& name, std::string& outName, std::string& outSource)
		{
			const std::filesystem::path file = folder / (name + ".forge");
			if (!std::filesystem::exists(file))
				return false;
			outName = name + ".forge";
			outSource = ReadText(file);
			return true;
		});
	}

	std::string Describe(const std::vector<CompileError>& mistakes)
	{
		std::string text;
		for (const CompileError& mistake : mistakes)
			text += mistake.ToString() + "\n";
		return text;
	}

}

TEST_CASE("FindMistakesReportsEveryStatementOnce")
{
	VM vm;
	const std::string source =
		"set speed to 2\n"                // 1 fine
		"set to 5\n"                      // 2 mistake
		"when created\n"                  // 3
		"\tsay \"hi\"\n"                  // 4 fine
		"\tif then\n"                     // 5 mistake (its whole block is skipped)
		"\t\tsay 2 +\n"                   // 6
		"\tend\n"                         // 7
		"\tsay 1 + * 3\n"                 // 8 mistake
		"end\n"                           // 9
		"say speed + * 2\n"               // 10 mistake
		"function helper()\n"             // 11
		"\tset to 1\n"                    // 12 mistake inside a hoisted function
		"\treturn 1\n"                    // 13
		"end\n"                           // 14
		"end\n"                           // 15 mistake: closes nothing
		"say \"fine\"\n";                 // 16 fine

	const std::vector<CompileError> mistakes = vm.FindMistakes(source, "many.forge");
	INFO(Describe(mistakes));
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(6));
	const int lines[] = { 2, 5, 8, 10, 12, 15 };
	for (size_t i = 0; i < mistakes.size(); i++)
	{
		CHECK_EQ(mistakes[i].Line, lines[i]);
		CHECK_EQ(mistakes[i].File, std::string("many.forge"));
	}
	CHECK_CONTAINS(mistakes[5].Message, "does not close anything");

	// Check still reports one mistake: the first one the compiler meets.
	CompileError first;
	CHECK_FALSE(vm.Check(source, "many.forge", first));
	CHECK_EQ(first.Line, 12);
}

TEST_CASE("FindMistakesFindsNothingInGoodScripts")
{
	VM vm;
	CHECK(vm.FindMistakes("", "empty.forge").empty());
	CHECK(vm.FindMistakes("set x to 1\nwhen created\n\tsay x\nend\n", "good.forge").empty());
}

TEST_CASE("FindMistakesSeesPastElseIf")
{
	// The 'if' of 'else if' shares the open 'if's 'end', so the function ends where it says and the
	// mistake after it is found.
	VM vm;
	const std::string source =
		"function grade(score)\n"                   // 1
		"\tif score is at least 90 then\n"           // 2
		"\t\treturn \"A\"\n"                        // 3
		"\telse if score is at least 80 then\n"      // 4
		"\t\treturn \"B\"\n"                        // 5
		"\totherwise if score is at least 70 then\n" // 6
		"\t\treturn \"C\"\n"                        // 7
		"\tend\n"                                    // 8
		"\treturn \"F\"\n"                           // 9
		"end\n"                                      // 10
		"say 1 + * 2\n";                             // 11 mistake
	std::vector<CompileError> mistakes = vm.FindMistakes(source, "grades.forge");
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Line, 11);
	CHECK(vm.FindMistakes("function f(n)\n\tif n then\n\t\treturn 1\n\telse if n then\n\t\treturn 2\n\tend\nend\nsay f(1)\n", "fine.forge").empty());
}

TEST_CASE("FindMistakesStopsAtUnreadableText")
{
	// After an unfinished string the rest of the file cannot be read reliably, so only that is reported.
	VM vm;
	const std::vector<CompileError> mistakes = vm.FindMistakes("set to 1\nsay \"open\nsay 2 +\n", "text.forge");
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Line, 2);
	CHECK_CONTAINS(mistakes[0].Message, "missing its closing");
}

TEST_CASE("FindMistakesReportsScriptsThatCannotBeFound")
{
	VM vm;
	UseTestModules(vm);
	const std::vector<CompileError> mistakes = vm.FindMistakes("use \"nowhere\"\nuse \"helpers\"\n", "uses.forge");
	INFO(Describe(mistakes));
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Line, 1);
	CHECK_EQ(mistakes[0].Column, 5);
	CHECK_CONTAINS(mistakes[0].Message, "Could not find the script \"nowhere\"");
}

TEST_CASE("FindMistakesStopsAfterOneHundred")
{
	VM vm;
	std::string source;
	for (int i = 0; i < 150; i++)
		source += "set to 1\n";
	CHECK_EQ(vm.FindMistakes(source, "noise.forge").size(), static_cast<size_t>(100));
}

TEST_CASE("FindMistakesAgreesWithCheckOnDamagedScripts")
{
	// Damage every test script in many ways (each line removed, each line repeated) and make sure the
	// checker never crashes, finds a mistake exactly when Check does, and includes Check's mistake.
	size_t damaged = 0;
	for (const auto& entry : std::filesystem::directory_iterator(FORGESCRIPT_TEST_SCRIPTS_DIR))
	{
		if (entry.path().extension() != ".forge")
			continue;
		std::vector<std::string> lines;
		std::istringstream stream(ReadText(entry.path()));
		for (std::string line; std::getline(stream, line);)
			lines.push_back(line);

		for (size_t target = 0; target < lines.size(); target++)
		{
			for (int variant = 0; variant < 2; variant++)
			{
				std::string source;
				for (size_t i = 0; i < lines.size(); i++)
				{
					if (i == target && variant == 0)
						continue;
					source += lines[i] + "\n";
					if (i == target && variant == 1)
						source += lines[i] + "\n";
				}

				VM vm;
				UseTestModules(vm);
				CompileError first;
				const bool fine = vm.Check(source, "damaged.forge", first);
				const std::vector<CompileError> mistakes = vm.FindMistakes(source, "damaged.forge");
				INFO(entry.path().filename().string() << " line " << (target + 1) << (variant == 0 ? " removed" : " repeated"));
				INFO(Describe(mistakes));
				CHECK_EQ(fine, mistakes.empty());
				if (!fine)
				{
					const bool included = std::any_of(mistakes.begin(), mistakes.end(), [&first](const CompileError& m)
					{
						return m.Line == first.Line && m.Column == first.Column && m.Message == first.Message;
					});
					CHECK(included);
				}
				damaged++;
			}
		}
	}
	CHECK(damaged > 500);
}

TEST_CASE("CheckReportsScriptsThatCannotBeFound")
{
	VM vm;
	UseTestModules(vm);
	CompileError error;
	CHECK(vm.Check("use \"helpers\"\n", "uses.forge", error));
	CHECK_FALSE(vm.Check("say 1\nuse \"Scripts/Missing\"\n", "uses.forge", error));
	CHECK_EQ(error.Line, 2);
	CHECK_EQ(error.Message, std::string("Could not find the script \"Scripts/Missing\" to use."));

	// Running is unchanged: the same message comes when the 'use' line runs.
	std::string reported;
	vm.SetErrorHandler([&reported](const ScriptError& e) { reported = e.Message; });
	CHECK_FALSE(vm.Load("use \"Scripts/Missing\"\n", "run.forge").Success);
	CHECK_EQ(reported, std::string("Could not find the script \"Scripts/Missing\" to use."));
}

TEST_CASE("FindNameUsesTellsVariablesApart")
{
	VM vm;
	const std::string source =
		"set score to 0\n"                                   // 1
		"function grow(amount)\n"                            // 2
		"\tset total to score + amount\n"                    // 3
		"\tfor each item in [1, 2]\n"                        // 4
		"\t\tset total to total + item\n"                    // 5
		"\tend\n"
		"\treturn total\n"                                   // 7
		"end\n"
		"function other(amount)\n"                           // 9
		"\tset helper to function(x) return x + amount end\n" // 10
		"\treturn helper(amount)\n"                          // 11
		"end\n"
		"kind Enemy\n"                                       // 13
		"\tset health to 10\n"                               // 14
		"\tfunction hit(amount)\n"                           // 15
		"\t\tset my health to my health - amount\n"           // 16
		"\tend\n"
		"end\n"
		"set e to new Enemy()\n"                             // 19
		"grow(5)\n";                                         // 20
	std::vector<CompileError> mistakes;
	const std::vector<NameUse> uses = vm.FindNameUses(source, "uses.forge", &mistakes);
	CHECK(mistakes.empty());
	auto keyAt = [&uses](int line, int column) -> std::string
	{
		for (const NameUse& use : uses)
		{
			if (use.Line == line && use.Column == column)
				return use.Key + (use.Declares ? " made" : "");
		}
		return "";
	};
	// A script-level variable, made on line 1 and read in a function.
	CHECK_EQ(keyAt(1, 5), std::string("G:score made"));
	CHECK_EQ(keyAt(3, 15), std::string("G:score"));
	// Three parameters named 'amount' are three variables.
	const std::string growAmount = keyAt(2, 15);
	CHECK_EQ(growAmount.substr(0, 1), std::string("L"));
	CHECK_EQ(keyAt(3, 23) + " made", growAmount);
	CHECK_NE(keyAt(9, 16), growAmount);
	CHECK_NE(keyAt(15, 15), growAmount);
	// A function inside a function reaches the outer 'amount'.
	CHECK_EQ(keyAt(10, 39) + " made", keyAt(9, 16));
	CHECK_EQ(keyAt(11, 16) + " made", keyAt(9, 16));
	// A variable a function makes by setting it: the same one on every line.
	CHECK_EQ(keyAt(3, 6), keyAt(5, 7));
	CHECK_EQ(keyAt(5, 16), keyAt(7, 9));
	CHECK_EQ(keyAt(4, 11), keyAt(5, 24) + " made");
	// Fields share a key by name, since which kind's can't be told.
	CHECK_EQ(keyAt(14, 6), std::string("F:health"));
	CHECK_EQ(keyAt(16, 10), std::string("F:health"));
	// Functions and kinds of the script.
	CHECK_EQ(keyAt(2, 10), std::string("G:grow made"));
	CHECK_EQ(keyAt(20, 1), std::string("G:grow"));
	CHECK_EQ(keyAt(13, 6), std::string("G:Enemy made"));
	CHECK_EQ(keyAt(19, 14), std::string("G:Enemy"));
	// Listed in the order they are written.
	CHECK(std::is_sorted(uses.begin(), uses.end(), [](const NameUse& a, const NameUse& b) { return a.Token < b.Token; }));
}
