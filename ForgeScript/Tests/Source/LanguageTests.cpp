#include "TestFramework.h"

#include "ForgeScript/Compiler/ScriptVariables.h"

using namespace ForgeScript;

// Compile errors: every mistake should point at the right line with a helpful message.

namespace {

	CompileError CompileFails(const std::string& source)
	{
		VM vm;
		CompileError error;
		bool ok = vm.Check(source, "test.forge", error);
		if (ok)
			ForgeTest::ReportFailure(__FILE__, __LINE__, "expected a compile error for:\n" + source);
		return error;
	}

	// What a script prints when it runs.
	std::string Output(const std::string& source)
	{
		VM vm;
		std::string output;
		vm.SetPrintHandler([&output](const std::string& text) { output += text + "\n"; });
		vm.RunInModule(vm.CreateModule("test"), source, "test.forge");
		return output;
	}

	void CompilesFine(const std::string& source)
	{
		VM vm;
		CompileError error;
		if (!vm.Check(source, "test.forge", error))
			ForgeTest::ReportFailure(__FILE__, __LINE__, "unexpected compile error: " + error.ToString() + "\nfor:\n" + source);
	}

}

TEST_CASE("CompileErrorMissingEnd")
{
	CompileError error = CompileFails("say 1\nwhile true do\n\tsay 2\n");
	CHECK_CONTAINS(error.Message, "missing its 'end'");
	CHECK_EQ(error.Line, 2);
}

TEST_CASE("CompileErrorEqualsSign")
{
	CHECK_CONTAINS(CompileFails("set x = 5").Message, "Use 'to' instead of '='");
	CHECK_CONTAINS(CompileFails("if x = 5 then end").Message, "Use 'is' to compare");
}

TEST_CASE("CompileErrorExtraEnd")
{
	CompileError error = CompileFails("say 1\nend");
	CHECK_CONTAINS(error.Message, "does not close anything");
	CHECK_EQ(error.Line, 2);
}

TEST_CASE("CompileErrorKeywordAsName")
{
	CHECK_CONTAINS(CompileFails("set end to 1").Message, "keyword");
	CHECK_CONTAINS(CompileFails("function if()\nend").Message, "keyword");
}

TEST_CASE("CompileErrorLoopControlOutsideLoop")
{
	CHECK_CONTAINS(CompileFails("stop").Message, "only be used inside a loop");
	CHECK_CONTAINS(CompileFails("skip").Message, "only be used inside a loop");
}

TEST_CASE("CompileErrorIncompletePhrase")
{
	CompileError error = CompileFails("random number from 1");
	CHECK_CONTAINS(error.Message, "not complete");
	CHECK_CONTAINS(error.Message, "random number from (low) to (high)");
}

TEST_CASE("CompileErrorBadPlacement")
{
	CHECK_CONTAINS(CompileFails("function f()\n\tconstant X is 1\nend").Message, "top level");
	CHECK_CONTAINS(CompileFails("function f()\n\tphrase hi\n\tend\nend").Message, "top level");
	CHECK_CONTAINS(CompileFails("say parent.x").Message, "'parent' can only be used");
	CHECK_CONTAINS(CompileFails("kind A\n\tsay 1\nend").Message, "Inside a kind");
	CHECK_CONTAINS(CompileFails("start 5").Message, "'start' needs a function call");
	CHECK_CONTAINS(CompileFails("else").Message, "needs an 'if'");
}

TEST_CASE("CompileErrorUnfinishedExpression")
{
	CHECK_CONTAINS(CompileFails("set x to").Message, "Expected a value");
	CHECK_CONTAINS(CompileFails("say (1 + 2").Message, "Expected ')'");
	CHECK_CONTAINS(CompileFails("say [1, 2").Message, "Expected ']'");
	CHECK_CONTAINS(CompileFails("repeat 3\nend").Message, "times");
}

TEST_CASE("CompilesSingleLineBlocks")
{
	CompilesFine("if true then say 1 else say 2 end");
	CompilesFine("for each x in [1] do say x end");
	CompilesFine("set f to function(x) return x end");
	CompilesFine("while false do end");
}

TEST_CASE("CompilesKeywordsAsPropertyNames")
{
	CompilesFine("set m to {}\nset m.end to 1\nsay m.end\nsay {if: 1}.if");
}

TEST_CASE("RuntimeErrorReportsLineAndTrace")
{
	ForgeTest::ScriptRun run = ForgeTest::RunScript("function inner()\n\treturn 1 / 0\nend\nfunction outer()\n\treturn inner()\nend\nouter()\n");
	CHECK(!run.Success);
	CHECK_EQ(run.Errors.size(), static_cast<size_t>(1));
	if (!run.Errors.empty())
	{
		CHECK_CONTAINS(run.Errors[0], "Cannot divide by zero.");
		CHECK_CONTAINS(run.Errors[0], "in 'inner' (test.forge line 2)");
		CHECK_CONTAINS(run.Errors[0], "in 'outer' (test.forge line 5)");
		CHECK_CONTAINS(run.Errors[0], "in test.forge line 7");
	}
}

TEST_CASE("InstructionLimitStopsEndlessLoops")
{
	VMConfig config;
	config.InstructionLimit = 100000;
	VM vm(config);
	std::string error;
	vm.SetErrorHandler([&error](const ScriptError& e) { error = e.Message; });
	LoadResult result = vm.Load("while true do\nend\n", "loop.forge");
	CHECK(!result.Success);
	CHECK_CONTAINS(error, "ran too long without waiting");
}

TEST_CASE("ReplShowsExpressionValues")
{
	VM vm;
	std::string output;
	vm.SetPrintHandler([&output](const std::string& text) { output += text + "\n"; });
	Module* module = vm.CreateModule("prompt");
	CompileOptions options;
	options.Repl = true;
	vm.RunInModule(module, "set x to 5\n", "prompt", options);
	vm.RunInModule(module, "x + 1\n", "prompt", options);
	vm.RunInModule(module, "\"text\"\n", "prompt", options);
	CHECK_EQ(output, std::string("6\n\"text\"\n"));
}

TEST_CASE("FindScriptVariablesListsTopLevelPlainValues")
{
	const std::string source =
		"# Settings shown in the editor\n"
		"set speed to 2.5\n"
		"set lives to 3\n"
		"set offset to -4\n"
		"set title to \"Hero\"\n"
		"set alive to yes\n"
		"set spawn to vector(1, -2, 3)\n"
		"set size to vector(4, 5)\n"
		"set tint to [1, 0.5, 0]\n"
		"set glow to [0, 0, 1, 0.25]\n"
		"set _secret to 1\n"
		"set computed to speed * 2\n"
		"set speed to 99\n"
		"set my_list to [1, 2]\n"
		"when update with delta\n"
		"\tset inside to 5\n"
		"\tif true then\n\t\tset deeper to 1\n\tend\n"
		"end\n"
		"set after to \"still top level\"\n";

	std::vector<ScriptVariable> variables = FindScriptVariables(source);
	REQUIRE_EQ(variables.size(), static_cast<size_t>(10));
	using Kind = ScriptVariable::ValueKind;
	CHECK_EQ(variables[0].Name, std::string("speed"));
	CHECK(variables[0].Kind == Kind::Number);
	CHECK_EQ(variables[0].Number, 2.5);
	CHECK_EQ(variables[0].Line, 2);
	CHECK(variables[1].Kind == Kind::WholeNumber);
	CHECK_EQ(variables[1].Number, 3.0);
	CHECK_EQ(variables[2].Number, -4.0);
	CHECK(variables[3].Kind == Kind::Text);
	CHECK_EQ(variables[3].Text, std::string("Hero"));
	CHECK(variables[4].Kind == Kind::Boolean);
	CHECK(variables[4].Boolean);
	CHECK(variables[5].Kind == Kind::Vector3);
	CHECK_EQ(variables[5].Components[1], -2.0);
	CHECK(variables[6].Kind == Kind::Vector2);
	CHECK_EQ(variables[6].Components[1], 5.0);
	CHECK(variables[7].Kind == Kind::Color);
	CHECK_EQ(variables[7].Components[3], 1.0);
	CHECK(variables[8].Kind == Kind::Color);
	CHECK_EQ(variables[8].Components[3], 0.25);
	CHECK_EQ(variables[9].Name, std::string("after"));
}

TEST_CASE("FindScriptVariablesSeesPastElseIf")
{
	const std::string source =
		"function grade(score)\n"
		"\tif score is above 90 then\n\t\treturn \"A\"\n"
		"\telse if score is above 80 then\n\t\treturn \"B\"\n"
		"\totherwise if score is above 70 then\n\t\treturn \"C\"\n"
		"\tend\n"
		"end\n"
		"set speed to 4\n";
	std::vector<ScriptVariable> variables = FindScriptVariables(source);
	REQUIRE_EQ(variables.size(), static_cast<size_t>(1));
	CHECK_EQ(variables[0].Name, std::string("speed"));
	CHECK_EQ(variables[0].Line, 10);
}

TEST_CASE("FindScriptVariablesIgnoresBrokenAndEmptyScripts")
{
	CHECK(FindScriptVariables("").empty());
	CHECK(FindScriptVariables("set x to\n").empty());
	CHECK(FindScriptVariables("set name to \"unfinished\n").empty());
	CHECK(FindScriptVariables("set greeting to \"Hi {name}\"\n").empty());
	// Words after '.' are property names, not blocks, so the line below still counts.
	std::vector<ScriptVariable> variables = FindScriptVariables("say thing.repeat\nset count to 1\n");
	REQUIRE_EQ(variables.size(), static_cast<size_t>(1));
	CHECK_EQ(variables[0].Name, std::string("count"));
}

TEST_CASE("CompileErrorsInWhenWithAName")
{
	CHECK_CONTAINS(CompileFails("when \"{name}\" is clicked\nend\n").Message, "must be plain, without { }");
	CHECK_CONTAINS(CompileFails("when \"\" is clicked\nend\n").Message, "Write the entity's name in the quotes");
	CHECK_CONTAINS(CompileFails("when \"Start\" is\nend\n").Message, "when \"Start\" is clicked");
	CompilesFine("when \"Start\" is clicked\nend\nwhen \"Player Name\" is submitted with text\n\tsay text\nend\n");
}

TEST_CASE("InlineIfIsNotAPlainScriptVariable")
{
	// The Inspector only shows plain values; a value picked by a test depends on the script running.
	std::vector<ScriptVariable> variables = FindScriptVariables("set size to \"big\" if wide otherwise \"small\"\nset speed to 3\n");
	REQUIRE(variables.size() == 1);
	CHECK(variables[0].Name == "speed");
}

TEST_CASE("CompileErrorsInInlineIf")
{
	CHECK_CONTAINS(CompileFails("say 1 if true\n").Message, "Say which value to use when it isn't true");
	CHECK_CONTAINS(CompileFails("say 1 if true otherwise\n").Message, "Expected a value");
	CHECK_CONTAINS(CompileFails("say 1 if otherwise 2\n").Message, "Expected a value");
	// 'start' takes one call, so it can't pick between two.
	CHECK_CONTAINS(CompileFails("function a()\nend\nstart a() if yes otherwise a()\n").Message, "found 'if'");
	CompilesFine("say 1 if true otherwise 2\nsay 1 if true else 2\nif true then say 1 if false otherwise 2 end\n");
}

TEST_CASE("InlineIfKeepsBlockHeadersWorking")
{
	// Everything here compiled before the inline 'if' existed and must mean the same thing now.
	CompilesFine("for each i in [1] if true then say i end end\n");
	CompilesFine("for i from 1 to 2 if true then say i end end\n");
	CompilesFine("while false if true then say 1 end end\n");
	CompilesFine("until true if true then say 1 end end\n");
	CompilesFine("repeat 2 times if true then say 1 end end\n");
	CompilesFine("try\n\tfail \"x\"\ncatch problem if true then say problem end end\n");
	CompilesFine("if true if true then say 1 end end\n");
	CompilesFine("function f(a)\n\tif a then return 1 end\n\treturn 2\nend\n");
}

TEST_CASE("InlineIfDoesNotHideLaterScriptVariables")
{
	// A block header followed by an 'if' block still nests what follows until its 'end'.
	std::vector<ScriptVariable> variables = FindScriptVariables(
		"for each i in [1] if true then\n\tset inner to 1\nend end\n"
		"set picked to [1 if true otherwise 2]\n"
		"set label to \"{1 if true otherwise 2}\"\n"
		"set speed to 3\n");
	REQUIRE(variables.size() == 1);
	CHECK(variables[0].Name == "speed");
}

TEST_CASE("CompileErrorsInBetween")
{
	CHECK_CONTAINS(CompileFails("say 5 is between 1\n").Message, "Expected 'and' between the two ends");
	CHECK_CONTAINS(CompileFails("say 5 is between 1 or 2\n").Message, "Expected 'and' between the two ends");
	CHECK_CONTAINS(CompileFails("say 5 is between 1 and\n").Message, "Expected a value");
}

TEST_CASE("BetweenKeepsItsOldMeaningForAVariableNamedBetween")
{
	// Before the range form, 'x is between ...' compared x with something named 'between'.
	// Where that may be what the script means, it still does.
	CHECK_EQ(Output("set between to 3\nset x to 3\nsay x is between\nsay x is between and yes\n"), std::string("true\ntrue\n"));
	CHECK_EQ(Output("set between to 3\nset x to 2\nsay x is between - 1 and yes\n"), std::string("true\n"));
	CHECK_EQ(Output("function between(a)\n\treturn a\nend\nsay 4 is between(4) and yes\n"), std::string("true\n"));
	CHECK_EQ(Output("set between to [1, 2]\nsay 2 is between[1] and yes\n"), std::string("false\n"));
	// A plain number or text after 'between' could never follow a name, so it is always a range.
	CHECK_EQ(Output("set between to 3\nsay 5 is between 1 and 10\n"), std::string("true\n"));
	// With no 'between' anywhere, a low end starting with '-', '(' or '[' is a range too.
	CHECK_EQ(Output("say -3 is between -5 and 0\nsay 5 is between (1 + 1) and [9][1]\n"), std::string("true\ntrue\n"));
}

TEST_CASE("LongTextWorksWithTheOtherForms")
{
	// The inline if and 'between' look at text tokens; long text with "{ }" must read the same.
	CHECK_EQ(Output("say \"\"\"{\"a\" if yes otherwise \"b\"}\"\"\" if yes otherwise \"c\"\n"), std::string("a\n"));
	CHECK_EQ(Output("say \"\"\"{5}\"\"\".length is between 1 and 2\n"), std::string("true\n"));
	// The Inspector's scan still finds variables after long text, and long text counts as plain text.
	std::vector<ScriptVariable> variables = FindScriptVariables("set intro to \"\"\"\n\tHello\n\t\"\"\"\nset speed to 3\n");
	REQUIRE(variables.size() == 2);
	CHECK(variables[0].Name == "intro");
	CHECK_EQ(variables[0].Text, std::string("Hello"));
	CHECK(variables[1].Name == "speed");
	CHECK_CONTAINS(CompileFails("say \"\"\"open\n").Message, "missing its closing \"\"\" quotes");
}

TEST_CASE("FinallyKeepsItsOldMeaningForANameCalledFinally")
{
	// Before 'finally' existed, a line starting with it used something named 'finally'. Where a script
	// has such a name, it still does.
	CHECK_EQ(Output("set finally to 3\ntry\n\tsay 1\n\tfinally\ncatch p\n\tsay p\nend\nsay \"done\"\n"), std::string("1\ndone\n"));
	CHECK_EQ(Output("function finally()\n\tsay \"called\"\nend\ntry\n\tfinally()\nend\n"), std::string("called\n"));
	// Without such a name, it starts the cleanup code.
	CHECK_EQ(Output("try\n\tsay 1\nfinally\n\tsay 2\nend\n"), std::string("1\n2\n"));
}

TEST_CASE("CompileErrorsInFinally")
{
	CHECK_CONTAINS(CompileFails("try\n\tsay 1\nfinally\n\tsay 2\nfinally\n\tsay 3\nend\n").Message, "only one 'finally'");
	CHECK_CONTAINS(CompileFails("try\n\tsay 1\nfinally\n\tsay 2\ncatch p\n\tsay 3\nend\n").Message, "Put 'catch' before 'finally'");
	CHECK_CONTAINS(CompileFails("try\n\tsay 1\nfinally\n\tsay 2\n").Message, "missing its 'end'");

	// The finally code is compiled once for each way out, but its mistakes are reported once.
	VM vm;
	const std::vector<CompileError> mistakes = vm.FindMistakes(
		"function f(x)\n\ttry\n\t\tif x then return 1 end\n\t\tfail \"no\"\n\tcatch p\n\t\treturn 2\n\tfinally\n\t\tsay oops oops\n\tend\nend\n", "test.forge");
	REQUIRE(mistakes.size() == 1);
	CHECK_EQ(mistakes[0].Line, 8);
}

TEST_CASE("AnErrorThroughFinallyIsReportedWhereItHappened")
{
	VM vm;
	ScriptError error;
	std::string output;
	vm.SetPrintHandler([&output](const std::string& text) { output += text + "\n"; });
	vm.SetErrorHandler([&error](const ScriptError& e) { error = e; });
	vm.RunInModule(vm.CreateModule("test"),
		"function deep()\n\tsay [1][5]\nend\ntry\n\tdeep()\nfinally\n\tsay \"cleanup\"\nend\n", "test.forge");
	CHECK_EQ(output, std::string("cleanup\n"));
	CHECK_CONTAINS(error.Message, "outside the list");
	CHECK_EQ(error.Line, 2);
	REQUIRE(error.Trace.size() == 2);
	CHECK_CONTAINS(error.Trace[0], "'deep'");
}

TEST_CASE("CompileErrorsInDefaultValues")
{
	CHECK_CONTAINS(CompileFails("function f(a is 1, b)\nend\n").Message, "Give 'b' a default value too, or put it before 'a'");
	CHECK_CONTAINS(CompileFails("function f(a = 1)\nend\n").Message, "Use 'is' for a default value");
	CHECK_CONTAINS(CompileFails("function f(a is)\nend\n").Message, "Write the default value after 'is'");
	CHECK_CONTAINS(CompileFails("function f(a is 1 2)\nend\n").Message, "Expected ',' or ')' after the default value.");
	// Events and phrases name their values differently, so they have no defaults.
	CompilesFine("function f(a, b is a + 1, c is [b])\n\treturn c\nend\nset g to function(x is 1) return x end\n");
}

TEST_CASE("DefaultValuesWorkOutAtEachCall")
{
	CHECK_EQ(Output("function f(a, b is a * 2)\n\treturn \"{a} {b}\"\nend\nsay f(1)\nsay f(1, 5)\n"), std::string("1 2\n1 5\n"));
	// A new list each time, unlike Python, where the same list is shared between calls.
	CHECK_EQ(Output("function f(x, bag is [])\n\tadd x to bag\n\treturn bag\nend\nsay f(1)\nsay f(2)\n"), std::string("[1]\n[2]\n"));
	// Giving 'nothing' is giving a value: the default is only for values left out.
	CHECK_EQ(Output("function f(a is 5)\n\treturn a\nend\nsay f(nothing)\n"), std::string("nothing\n"));
	CHECK_CONTAINS(Output("function f(a, b is 1)\nend\ntry\n\tf()\ncatch p\n\tsay p\nend\n"), "needs 1 to 2 values, but was given 0 values");
}

namespace {

	// The structured stack of the first error a script reports.
	std::vector<StackFrame> FramesOf(const std::string& source)
	{
		VM vm;
		std::vector<StackFrame> frames;
		bool seen = false;
		vm.SetPrintHandler([](const std::string&) {});
		vm.SetErrorHandler([&](const ScriptError& error)
		{
			if (!seen)
				frames = error.Frames;
			seen = true;
		});
		vm.RunInModule(vm.CreateModule("game.forge"), source, "game.forge");
		return frames;
	}

	std::string Describe(const std::vector<StackFrame>& frames)
	{
		std::string text;
		for (const StackFrame& frame : frames)
			text += frame.File + ":" + std::to_string(frame.Line) + (frame.Function.empty() ? "" : " in " + frame.Function) +
				(frame.Repeats > 1 ? " x" + std::to_string(frame.Repeats) : "") + "\n";
		return text;
	}

}

TEST_CASE("ErrorsCarryAStructuredStack")
{
	// Innermost first, with the function's name, and "" for code at the top of a script.
	CHECK_EQ(Describe(FramesOf("function heal(x)\n\tsay [1][x]\nend\nfunction turn()\n\theal(5)\nend\nturn()\n")),
		std::string("game.forge:2 in heal\ngame.forge:5 in turn\ngame.forge:7\n"));
	// A function calling itself shows once, with how many times in a row.
	CHECK_EQ(Describe(FramesOf("function down(n)\n\tif n is 0 then fail \"bottom\" end\n\tdown(n - 1)\nend\ndown(3)\n")),
		std::string("game.forge:2 in down\ngame.forge:3 in down x3\ngame.forge:5\n"));
	// An error that goes on after finally code keeps the stack from where it happened.
	CHECK_EQ(Describe(FramesOf("function deep()\n\tfail \"no\"\nend\ntry\n\tdeep()\nfinally\n\tsay 1\nend\n")),
		std::string("game.forge:2 in deep\ngame.forge:5\n"));
}
