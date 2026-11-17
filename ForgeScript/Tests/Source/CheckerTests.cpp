#include "TestFramework.h"

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
