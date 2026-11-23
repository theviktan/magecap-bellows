#include "TestFramework.h"

#include "ForgeScript/Compiler/ScriptVariables.h"
#include "ForgeScript/Core/Utf8.h"

#include <algorithm>

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

TEST_CASE("FindHandlersThatWaitFollowsTheScriptsOwnFunctions")
{
	const char* script =
		"function fade_out()\n"
		"\twait 0.5 seconds\n"
		"end\n"
		"function close_menu()\n"
		"\tfade_out()\n"
		"end\n"
		"function quick()\n"
		"\tsay \"no wait\"\n"
		"end\n"
		"when clicked\n"                       // 10: waits itself, in a nested block
		"\tif true then\n"
		"\t\twait 1 frame\n"
		"\tend\n"
		"end\n"
		"when \"Resume\" is clicked\n"         // 15: through two functions
		"\tclose_menu()\n"
		"end\n"
		"when \"Quit\" is clicked\n"           // 18: calls only a function that doesn't wait
		"\tquick()\n"
		"\tsay text.repeat(2)\n"
		"end\n"
		"when changed with value\n"            // 22: starts one that waits
		"\tstart fade_out()\n"
		"end\n"
		"when update\n"                        // 25: not a UI event
		"\twait 1 second\n"
		"end\n";
	const std::vector<WaitingHandler> found = FindHandlersThatWait(script, { "clicked", "changed", "submitted" });
	REQUIRE_EQ(found.size(), static_cast<size_t>(3));
	CHECK_EQ(found[0].Event, std::string("clicked"));
	CHECK_EQ(found[0].Line, 10);
	CHECK_EQ(found[1].Event, std::string("\"Resume\" is clicked"));
	CHECK_EQ(found[1].Line, 15);
	CHECK_EQ(found[2].Event, std::string("changed"));
	CHECK_EQ(found[2].Line, 22);
	CHECK(FindHandlersThatWait("", { "clicked" }).empty());
	CHECK(FindHandlersThatWait("when clicked\n\twait\n", { "clicked" }).size() == 1);
}

TEST_CASE("CompileErrorsInWhenWithAName")
{
	CHECK_CONTAINS(CompileFails("when \"{name}\" is clicked\nend\n").Message, "must be plain, without { }");
	CHECK_CONTAINS(CompileFails("when \"\" is clicked\nend\n").Message, "Write the entity's name in the quotes");
	CHECK_CONTAINS(CompileFails("when \"Start\" is\nend\n").Message, "when \"Start\" is clicked");
	CompilesFine("when \"Start\" is clicked\nend\nwhen \"Player Name\" is submitted with text\n\tsay text\nend\n");
}

TEST_CASE("CompileErrorsInWaitForAndTimers")
{
	CHECK_CONTAINS(CompileFails("wait for\n").Message, "Expected an event name, like: wait for door opened");
	CHECK_CONTAINS(CompileFails("wait for hit with\n").Message, "Expected a name for the event value");
	CHECK_CONTAINS(CompileFails("wait for \"{name}\" is clicked\n").Message, "must be plain, without { }");
	CHECK_CONTAINS(CompileFails("wait for \"\" is clicked\n").Message, "Write the entity's name in the quotes");
	CHECK_CONTAINS(CompileFails("every 3 seconds\n\tsay 1\n").Message, "This 'every' timer is missing its 'end'");
	CHECK_CONTAINS(CompileFails("after 1 second\n\tsay 1\n").Message, "This 'after' timer is missing its 'end'");
	CHECK_CONTAINS(CompileFails("after 1 second\n\tstop\nend\n").Message, "stop");
	CompilesFine("when ready\n\twait for \"Start\" is clicked\n\twait for hit by damage and source\nend\n");
	CompilesFine("if true then every 2 frames\n\tsay frame()\nend end\n");
}

TEST_CASE("EveryAndAfterKeepTheirOldMeanings")
{
	// Names, calls and a script's own phrases that start with the words work as they did before 1.6.
	CHECK_EQ(Output("set every to 2\nset after to every + 1\nsay after\n"), std::string("3\n"));
	CHECK_EQ(Output("function every(x)\n\treturn x * 2\nend\nsay every(4)\n"), std::string("8\n"));
	CHECK_EQ(Output("phrase every (n) seconds\n\tsay \"phrase {n}\"\nend\nevery 3 seconds\n"), std::string("phrase 3\n"));
	CHECK_EQ(Output("phrase after (n) seconds\n\tsay \"later {n}\"\nend\nafter 2 seconds\n"), std::string("later 2\n"));
	CHECK_EQ(Output("set m to {every: 1}\nsay m.every\n"), std::string("1\n"));
}

TEST_CASE("ToolsSeeWaitForAndTimers")
{
	// 'wait for' is not a 'for' loop, and a timer opens a block: the tools pair every 'end' as the compiler does.
	const char* script =
		"set speed to 3\n"
		"every 1 second\n"
		"\tset inner to 1\n"
		"end\n"
		"when clicked\n"
		"\twait for hit with damage\n"
		"end\n"
		"when submitted\n"
		"\tafter 2 seconds\n"
		"\t\tsay 1\n"
		"\tend\n"
		"end\n"
		"set limit to 10\n";
	std::vector<ScriptVariable> variables = FindScriptVariables(script);
	REQUIRE(variables.size() == 2);
	CHECK(variables[0].Name == "speed");
	CHECK(variables[1].Name == "limit");
	const std::vector<WaitingHandler> waiting = FindHandlersThatWait(script, { "clicked", "submitted" });
	REQUIRE(waiting.size() == 2);
	CHECK_EQ(waiting[0].Event, std::string("clicked"));
	CHECK_EQ(waiting[1].Event, std::string("submitted"));
	CompilesFine(script);
	// Inside brackets too: an anonymous function that waits for an event still ends at its own 'end'.
	CHECK_EQ(Output("set run to [function()\n\twait for go\n\tsay 1\nend, 2]\nsay run.length\n"), std::string("2\n"));
	CHECK_EQ(Output("set l to [2, 1]\nl.sort_by(function(x)\n\tafter 1 second\n\t\tsay x\n\tend\n\treturn x\nend)\nsay l\n"), std::string("[1, 2]\n"));
}

TEST_CASE("MistakesFromOtherLanguagesTeachTheForgeScript")
{
	// Each keeps its old message and adds the ForgeScript that does what was meant.
	CHECK_CONTAINS(CompileFails("set x to 1\nincrease x by 2\n").Message, "Expected the end of the line, but found 'x'. To make a number bigger, write: add 2 to x");
	CHECK_CONTAINS(CompileFails("set x to 1\ndecrease x by 3\n").Message, "To make a number smaller, write: take 3 from x");
	CHECK_CONTAINS(CompileFails("def jump():\n\treturn 1\n").Message, "To make a function, write: function jump() ... end");
	CHECK_CONTAINS(CompileFails("var score = 0\n").Message, "To make a variable, write: set score to ...");
	CHECK_CONTAINS(CompileFails("const MAX = 3\n").Message, "To make a constant, write: constant MAX is ...");
	CHECK_CONTAINS(CompileFails("if true then\n\tsay 1\nelif x is 2 then\n\tsay 2\nend\n").Message, "else if x is 2 then");
	CHECK_CONTAINS(CompileFails("set x to 1\nx += 2\n").Message, "Expected a value here, but found '='. To add to a value, write: add 2 to x");
	CHECK_CONTAINS(CompileFails("set x to 1\nx -= 2\n").Message, "write: take 2 from x");
	CHECK_CONTAINS(CompileFails("set x to 1\nx *= 2\n").Message, "write: multiply x by 2");
	CHECK_CONTAINS(CompileFails("set x to 1\nx /= 2\n").Message, "write: divide x by 2");
	CHECK_CONTAINS(CompileFails("set x to 1\nx++\n").Message, "ForgeScript has no ++. Write: add 1 to x");
	CHECK_CONTAINS(CompileFails("set x to 1\nx--\n").Message, "ForgeScript has no --. Write: take 1 from x");
	CHECK_CONTAINS(CompileFails("say 7 // 2\n").Message, "ForgeScript has no //. For a whole-number result, write: floor(a / b)");
	CHECK_CONTAINS(CompileFails("set x to 1\nif x > 0:\n\tsay 1\nend\n").Message, "ForgeScript doesn't end a line with ':'");
	CHECK_CONTAINS(CompileFails("set p to 1\nsay \"{p:.2}\"\n").Message, "write: {format_number(p, 2)}");
	CHECK_CONTAINS(CompileFails("if 1 > 0 && 2 > 1 then\nend\n").Message, "ForgeScript writes 'and'");
	CHECK_CONTAINS(CompileFails("if 1 > 0 || 2 > 1 then\nend\n").Message, "ForgeScript writes 'or'");
	// The same words are still names: nothing that worked changes.
	CompilesFine("set increase to 2\nset var to increase + 1\nfunction def(x)\n\treturn x\nend\nsay def(var)\n");

	CHECK_CONTAINS(Output("try\n\tsay True\ncatch problem\n\tsay problem\nend\n"), "'True' has not been given a value yet. In ForgeScript, write: true");
	CHECK_CONTAINS(Output("try\n\tsay len([1])\ncatch problem\n\tsay problem\nend\n"), "In ForgeScript, write: length(list)");
	CHECK_CONTAINS(Output("try\n\t[1].append(2)\ncatch problem\n\tsay problem\nend\n"), "A list has no function called 'append'. In ForgeScript, write: add x to list");
	CHECK_CONTAINS(Output("try\n\tsay \"a\".toUpperCase()\ncatch problem\n\tsay problem\nend\n"), "In ForgeScript, write: text.upper()");
	CHECK_CONTAINS(Output("try\n\tsay (5).toString()\ncatch problem\n\tsay problem\nend\n"), "Cannot use 'toString' on a number. In ForgeScript, write: text(x)");
	// A typo still gets its 'Did you mean', and a name nobody knows its 'Use: set'.
	CHECK_CONTAINS(Output("set health to 1\ntry\n\tsay helth\ncatch problem\n\tsay problem\nend\n"), "Did you mean 'health'?");
	CHECK_CONTAINS(Output("try\n\tsay zork\ncatch problem\n\tsay problem\nend\n"), "Use: set zork to ...");
}

TEST_CASE("ListVerbsKeepOldMeaningsAndExplainMistakes")
{
	CHECK_CONTAINS(CompileFails("set e to []\nsort e by e.health\n").Message, "After 'by', write a field name or a function");
	CHECK_CONTAINS(CompileFails("set e to []\nsort e by\n").Message, "After 'by', write a field name or a function");
	// A script's own phrase starting with the word, and a call, keep their meaning.
	CHECK_EQ(Output("phrase sort (x)\n\tsay \"phrase sorts {x}\"\nend\nset items to [2, 1]\nsort items\n"), std::string("phrase sorts [2, 1]\n"));
	CHECK_EQ(Output("function sort(x)\n\treturn \"called\"\nend\nsay sort([2, 1])\n"), std::string("called\n"));
	CHECK_EQ(Output("set m to {reverse: 1}\nsay m.reverse\n"), std::string("1\n"));
	// 'sort' of something that isn't a list says so, as items.sort() would.
	CHECK_CONTAINS(Output("try\n\tset n to 5\n\tsort n\ncatch problem\n\tsay problem\nend\n"), "sort");
}

TEST_CASE("ChangeHandlersRunAfterChanges")
{
	// A handler that waits lets the writer go on; its values were taken when the change happened.
	VM vm;
	std::vector<std::string> printed;
	vm.SetPrintHandler([&printed](const std::string& text) { printed.push_back(text); });
	LoadResult loaded = vm.Load(
		"kind W\n\tset hp to 3\n\twhen my hp changes with now\n\t\twait 1 second\n\t\tsay \"was {now}, is {my hp}\"\n\tend\nend\n"
		"set w to new W()\nset w.hp to 2\nset w.hp to 1\nsay \"writer goes on\"\n", "w.forge");
	REQUIRE(loaded.Success);
	CHECK(printed == std::vector<std::string>{ "writer goes on" });
	vm.Update(1.5);
	CHECK(printed == std::vector<std::string>{ "writer goes on", "was 2, is 1", "was 1, is 1" });

	// Handlers that change each other's fields stop with an error, and the writer goes on.
	std::vector<std::string> errors;
	VM loops;
	loops.SetPrintHandler([&printed](const std::string& text) { printed.push_back(text); });
	loops.SetErrorHandler([&errors](const ScriptError& error) { errors.push_back(error.Message); });
	printed.clear();
	REQUIRE(loops.Load("kind P\n\tset a to 0\n\tset b to 0\n\twhen my a changes with now\n\t\tset my b to now + 1\n\tend\n"
		"\twhen my b changes with now\n\t\tset my a to now + 1\n\tend\nend\nset p to new P()\nset p.a to 1\nsay \"still here\"\n", "p.forge").Success);
	REQUIRE_FALSE(errors.empty());
	CHECK_CONTAINS(errors[0], "was changed from inside 64 other changes and events");
	CHECK(printed == std::vector<std::string>{ "still here" });

	// Each object has its own changes: one object's handler sees only its own.
	CHECK_EQ(Output("kind K\n\tset x to 0\n\twhen my x changes with now\n\t\tsay \"{my name}: {now}\"\n\tend\n\tfunction setup(n)\n\t\tset my name to n\n\tend\nend\n"
		"set a to new K(\"a\")\nset b to new K(\"b\")\nset b.x to 2\nset a.x to 1\n"), std::string("b: 2\na: 1\n"));
}

TEST_CASE("ChangeHandlersKeepOldMeaningsAndExplainMistakes")
{
	// At the top level it is still an event, also with values.
	CHECK_EQ(Output("when my health changes with by_how_much\n\tsay by_how_much\nend\ntrigger my health changes with 5\n"), std::string("5\n"));
	CHECK_CONTAINS(CompileFails("kind P\n\twhen my hp changes with a, b, c\n\tend\nend\n").Message,
		"'when my hp changes' gives two values: the new one and the old one, like: with now, before");
	CHECK_CONTAINS(CompileFails("kind P\n\twhen hp changes\n\tend\nend\n").Message, "or 'when my name changes' to react to a field.");
}

TEST_CASE("BigChoiceSetsStayQuick")
{
	// Audit S-5: 20,000 choices, each used once and all but one in a match, check and run without
	// quadratic work (the constant table, the duplicate check and the lookups use hash sets).
	std::string choices = "choices Big are ";
	std::string uses;
	std::string match = "set s to Big.c0\nmatch s\n";
	for (int i = 0; i < 20000; i++)
	{
		choices += (i ? ", c" : "c") + std::to_string(i);
		uses += "set x to Big.c" + std::to_string(i) + "\n";
	}
	VM vm;
	CHECK(vm.FindMistakes(choices + "\n" + uses + "say x\n", "big.forge").empty());
	CHECK_EQ(Output(choices + "\nsay Big.c19999\nsay Big.length\n"), std::string("c19999\n20000\n"));
	CHECK_CONTAINS(CompileFails(choices + ", c5\n").Message, "'c5' is in Big's choices twice.");
}

TEST_CASE("NegativeTimerTimesWarn")
{
	VM vm;
	std::vector<CompileError> found = vm.FindWarnings("every -1 seconds\n\tstop\nend\nafter -2 seconds\nend\nevery 0 seconds\n\tstop\nend\n", "t.forge", {});
	REQUIRE(found.size() == 2);
	CHECK_EQ(found[0].Message, std::string("A negative time works like 0, so this timer runs as often as it can. Use a time above 0, like: every 1 second"));
	CHECK(found[1].Line == 4);
	CHECK_CONTAINS(found[1].Message, "runs as soon as it can. Use a time above 0, like: after 1 second");
}

TEST_CASE("MistakesThatNameTheirFixCarryIt")
{
	// Applying each fix must leave a script with no mistakes: the fix does what the message says.
	auto apply = [](const std::string& source, const CompileError& mistake)
	{
		auto offset = [&source](int line, int column)
		{
			size_t at = 0;
			for (int l = 1; l < line; l++)
				at = source.find('\n', at) + 1;
			return at + static_cast<size_t>(column - 1);
		};
		const size_t from = offset(mistake.FixLine, mistake.FixColumn);
		const size_t to = offset(mistake.FixEndLine, mistake.FixEndColumn);
		return source.substr(0, from) + mistake.FixText + source.substr(to);
	};
	struct Case { const char* Source; const char* Title; const char* Fixed; };
	const Case cases[] = {
		{ "set x = 5\nsay x\n", "Change '=' to 'to'", "set x to 5\nsay x\n" },
		{ "set h to 1\nif h = 1 then\n\tsay 1\nend\n", "Change '=' to 'is'", "set h to 1\nif h is 1 then\n\tsay 1\nend\n" },
		{ "function hit(t, damage = 10)\n\treturn damage\nend\n", "Change '=' to 'is'", "function hit(t, damage is 10)\n\treturn damage\nend\n" },
		{ "set n to 0\nrepeat while n < 3 do\n\tadd 1 to n\nend\n", "Remove 'repeat'", "set n to 0\nwhile n < 3 do\n\tadd 1 to n\nend\n" },
		{ "function f(a as numbr)\n\treturn a\nend\n", "Change to 'number'", "function f(a as number)\n\treturn a\nend\n" },
		{ "set x to 1\nx += 5\n", "Write: add 5 to x", "set x to 1\nadd 5 to x\n" },
		{ "set x to 1\nx *= 2\n", "Write: multiply x by 2", "set x to 1\nmultiply x by 2\n" },
		{ "set x to 1\nx++\n", "Write: add 1 to x", "set x to 1\nadd 1 to x\n" },
		{ "set x to 1\nx--\n", "Write: take 1 from x", "set x to 1\ntake 1 from x\n" },
		{ "set x to 1\nif x > 0 then\n\tsay 1\n\tsay 2\nsay 3\n", "Add the missing 'end'", "set x to 1\nif x > 0 then\n\tsay 1\n\tsay 2\nend\nsay 3\n" },
		{ "function f()\n\trepeat 2 times\n\t\tsay 1\n\tsay 2\nend\n", "Add the missing 'end'", "function f()\n\trepeat 2 times\n\t\tsay 1\n\tend\n\tsay 2\nend\n" },
		{ "kind Box\n\tset w to 1\n", "Add the missing 'end'", "kind Box\n\tset w to 1\nend\n" },
		{ "choices Color are red, green\nsay Color.gren\n", "Change to 'green'", "choices Color are red, green\nsay Color.green\n" },
	};
	for (const Case& test : cases)
	{
		CAPTURE(test.Source);
		VM vm;
		const std::vector<CompileError> mistakes = vm.FindMistakes(test.Source, "t.forge");
		REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
		REQUIRE(mistakes[0].HasFix());
		CHECK_EQ(mistakes[0].FixTitle, std::string(test.Title));
		const std::string fixed = apply(test.Source, mistakes[0]);
		CHECK_EQ(fixed, std::string(test.Fixed));
		CHECK(vm.FindMistakes(fixed, "t.forge").empty());
	}

	// A script with Windows line ends (CRLF) gets a CRLF before its new 'end' (audit S-13).
	{
		VM vm;
		const std::string crlf = "if true then\r\n\tsay 1\r\nsay 2\r\n";
		const std::vector<CompileError> mistakes = vm.FindMistakes(crlf, "t.forge");
		REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
		CHECK_EQ(mistakes[0].FixText, std::string("\r\nend"));
		CHECK_EQ(mistakes[0].FixLine, 2);
		CHECK_EQ(mistakes[0].FixColumn, 7);
		const std::string fixed = apply(crlf, mistakes[0]);
		CHECK_EQ(fixed, std::string("if true then\r\n\tsay 1\r\nend\r\nsay 2\r\n"));
		CHECK(vm.FindMistakes(fixed, "t.forge").empty());
	}

	// A message that doesn't say exactly what to write has no fix.
	VM vm;
	const std::vector<CompileError> mistakes = vm.FindMistakes("say )\n", "t.forge");
	REQUIRE_FALSE(mistakes.empty());
	CHECK_FALSE(mistakes[0].HasFix());
	// 'x += 5' inside a longer line is only a hint: replacing the line could lose the rest of it.
	CHECK_FALSE(vm.FindMistakes("set x to 1\nsay x += 5\n", "t.forge")[0].HasFix());
}

TEST_CASE("ALetterOutsideTextIsReportedOnce")
{
	// 'é' is two bytes of UTF-8. It used to give two mistakes, each with one byte of it, which isn't
	// valid UTF-8 in the checkers' JSON (audit S-12).
	VM vm;
	std::vector<CompileError> mistakes = vm.FindMistakes("set caf\xC3\xA9 = 5\n", "t.forge");
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Message, std::string("'\xC3\xA9' can only be used inside text, like \"caf\xC3\xA9\". Names use the letters a to z, "
		"the digits 0 to 9 and _, like: set cafe to 5"));
	CHECK_EQ(mistakes[0].Column, 8);
	// A three-byte dash, and a byte that isn't UTF-8 at all.
	mistakes = vm.FindMistakes("set x to 1 \xE2\x80\x94 2\n", "t.forge");
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Message.substr(0, 5), std::string("'\xE2\x80\x94'"));
	mistakes = vm.FindMistakes("say 1 \xFF\n", "t.forge");
	REQUIRE_EQ(mistakes.size(), static_cast<size_t>(1));
	CHECK_EQ(mistakes[0].Message, std::string("This byte isn't readable text. Save the script as UTF-8."));
	// Inside text it's fine, as always.
	CHECK(vm.FindMistakes("say \"caf\xC3\xA9\"\n", "t.forge").empty());
}

TEST_CASE("AFunctionOrKindThatTakesABuiltinNameWarns")
{
	// The script's own name still wins (it always has), but a new Bellows name can take over a game's
	// helper, so it is said (audit S-15).
	VM vm;
	auto taking = [&vm](const std::string& source, std::vector<std::string> engine = {})
	{
		std::vector<CompileError> found;
		for (CompileError& warning : vm.FindWarnings(source, "t.forge", engine))
		{
			if (warning.Message.find("takes the name of the built-in") != std::string::npos)
				found.push_back(std::move(warning));
		}
		return found;
	};
	std::vector<CompileError> found = taking("function ease(t)\n\treturn t\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("Your function 'ease' takes the name of the built-in 'ease', so 'ease' in this script now "
		"means yours. To keep using the built-in too, rename yours, like 'my_ease'."));
	CHECK_EQ(found[0].Line, 1);
	CHECK_EQ(found[0].Column, 10);
	// An engine's names, as the checker is told them; a kind gets a type-like suggestion.
	found = taking("function tween(a, b)\n\treturn a\nend\nkind Material\nend\n", { "tween", "Material" });
	REQUIRE_EQ(found.size(), static_cast<size_t>(2));
	CHECK_NE(found[0].Message.find("'my_tween'"), std::string::npos);
	CHECK_NE(found[1].Message.find("Your kind 'Material'"), std::string::npos);
	CHECK_NE(found[1].Message.find("'MyMaterial'"), std::string::npos);
	CHECK_EQ(taking("choices color are red, green\n").size(), static_cast<size_t>(1));
	// A function inside a function too; names that aren't built in stay quiet.
	CHECK_EQ(taking("function outer()\n\tfunction ease(t)\n\t\treturn t\n\tend\n\treturn ease(1)\nend\n").size(), static_cast<size_t>(1));
	CHECK(taking("function my_ease(t)\n\treturn t\nend\nkind Enemy\nend\n").empty());
	CHECK(taking("function tween(a, b)\n\treturn a\nend\n").empty());   // no engine told: not a built-in here

	// It still works as before: the script's own function is the one called.
	CHECK_EQ(Output("function ease(t)\n\treturn \"mine\"\nend\nsay ease(0.5)\n"), std::string("mine\n"));
}

TEST_CASE("ANameThatIsNeverSetWarns")
{
	VM vm;
	auto warnings = [&vm](const std::string& source, std::vector<std::string> engine = {})
	{
		std::vector<CompileError> all = vm.FindWarnings(source, "t.forge", engine);
		std::vector<CompileError> found;
		for (CompileError& warning : all)
		{
			if (warning.Message.find("isn't set anywhere") != std::string::npos)
				found.push_back(std::move(warning));
		}
		return found;
	};

	// The misspelling, with the one close name and its fix; inside text and inside a function too.
	std::vector<CompileError> found = warnings("set health to 10\nsay helth\nsay \"{helth} left\"\nfunction f()\n\tsay helth\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(3));
	CHECK_EQ(found[0].Message, std::string("'helth' isn't set anywhere, so this line will stop with a mistake when it runs. Did you mean 'health'?"));
	CHECK_EQ(found[0].Line, 2);
	CHECK_EQ(found[0].Column, 5);
	CHECK_EQ(found[0].FixTitle, std::string("Change to 'health'"));
	CHECK_EQ(found[0].FixText, std::string("health"));
	CHECK_EQ(found[0].FixColumn, 5);
	CHECK_EQ(found[0].FixEndColumn, 10);
	CHECK_EQ(found[1].Line, 3);
	CHECK_EQ(found[2].Line, 5);

	// No close name, or two of them: no guess.
	found = warnings("say totally_unknown\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("'totally_unknown' isn't set anywhere, so this line will stop with a mistake when it runs."));
	CHECK_FALSE(found[0].HasFix());
	found = warnings("set cart to 1\nset card to 2\nsay carx\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message.find("Did you mean"), std::string::npos);
	// A name from another language gets its hint.
	CHECK_NE(warnings("say True\n")[0].Message.find("In ForgeScript, write: true"), std::string::npos);
	// A misspelled kind after 'new'.
	CHECK_NE(warnings("kind Enemy\nend\nset e to new Enmy()\n")[0].Message.find("Did you mean 'Enemy'?"), std::string::npos);

	// Set anywhere counts: later in the script, only inside an 'if', as a function, kind or choices,
	// built in, or the engine's (named by the checker).
	CHECK(warnings("function show_it()\n\tsay later\nend\nset later to 1\n").empty());
	CHECK(warnings("if random(1, 2) is 1 then\n\tset bonus to 2\nend\nsay bonus\n").empty());
	CHECK(warnings("function hit()\n\treturn 1\nend\nkind Box\nend\nchoices Color are red, green\nsay hit\nsay Box\nsay Color.red\n").empty());
	CHECK(warnings("say pi\nsay length\n").empty());
	CHECK(warnings("say Material\n", { "Material" }).empty());
	CHECK(warnings("say Material\n").size() == 1);
	// Calls are left alone: an engine's functions are only known when the checker is told about them.
	CHECK(warnings("find_entity(\"Hero\")\nsay find_entity(\"Hero\")\n").empty());
	// Set only inside one 'when' block, a name is that block's own: another block can't read it.
	// Set at the top too, both share it (the Cookbook's recipes do this).
	found = warnings("when ping\n\tset _target to 1\nend\nwhen pong\n\tsay _target\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Line, 5);
	CHECK(warnings("set _target to nothing\nwhen ping\n\tset _target to 1\nend\nwhen pong\n\tsay _target\nend\n").empty());
	// A function further down that sets a script variable counts (it sets the script's one).
	CHECK(warnings("set count to 0\nfunction show_count()\n\tsay count\nend\nfunction bump()\n\tset count to count + 1\nend\n").empty());

	// Locals and parameters are never warned about.
	CHECK(warnings("function f(a)\n\tset b to a\n\tsay b\nend\nfor each x in [1]\n\tsay x\nend\n").empty());
}

TEST_CASE("ANameFromAUsedScriptIsSet")
{
	VM vm;
	vm.SetModuleLoader([](const std::string& path, std::string& outName, std::string& outSource)
	{
		outName = path + ".forge";
		if (path == "helpers")
		{
			outSource = "use \"deeper\"\nset shared_score to 3\n";
			return true;
		}
		if (path == "deeper")
		{
			outSource = "set deep_value to 1\n";
			return true;
		}
		return false;
	});
	auto neverSet = [&vm](const std::string& source)
	{
		size_t count = 0;
		for (const CompileError& warning : vm.FindWarnings(source, "t.forge", {}))
			count += warning.Message.find("isn't set anywhere") != std::string::npos ? 1 : 0;
		return count;
	};
	// Set in a used script, or in one that script uses.
	CHECK_EQ(neverSet("use \"helpers\"\nsay shared_score\nsay deep_value\n"), static_cast<size_t>(0));
	// A misspelling of it still warns, with the used script's name as the suggestion.
	const std::vector<CompileError> found = vm.FindWarnings("use \"helpers\"\nsay shared_scor\n", "t.forge", {});
	REQUIRE_FALSE(found.empty());
	CHECK_NE(found[0].Message.find("Did you mean 'shared_score'?"), std::string::npos);
	// A used script that can't be found keeps the warning quiet: its own mistake says so.
	CHECK_EQ(neverSet("use \"missing\"\nsay anything\n"), static_cast<size_t>(0));
}

TEST_CASE("RepeatForeverWithoutAWaitWarns")
{
	VM vm;
	auto warnings = [&vm](const std::string& source) { return vm.FindWarnings(source, "t.forge", {}); };

	// Nothing in it waits or leaves: the game can't run while it does.
	std::vector<CompileError> found = warnings("set n to 0\nrepeat forever\n\tadd 1 to n\n\tsay n\n\tset x to random(1, 6)\nend\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 2);
	CHECK_EQ(found[0].Message, std::string("This 'repeat forever' loop never waits, so nothing else in the game can run while it does. "
		"Add a 'wait' inside it, like: wait 1 frame"));

	// An inner loop that never waits warns even when the outer one does.
	found = warnings("repeat forever\n\twait 1 frame\n\trepeat forever\n\t\tsay 1\n\tend\nend\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 3);

	// A wait, a way out, or a call that might wait: no warning.
	CHECK(warnings("repeat forever\n\twait 1 frame\nend\n").empty());
	CHECK(warnings("set n to 0\nrepeat forever\n\tadd 1 to n\n\tif n > 3 then stop end\nend\n").empty());
	CHECK(warnings("function run()\n\trepeat forever\n\t\treturn 1\n\tend\nend\n").empty());
	CHECK(warnings("repeat forever\n\tfail \"no\"\nend\n").empty());
	CHECK(warnings("function step()\n\twait 1 frame\nend\nrepeat forever\n\tstep()\nend\n").empty());
	CHECK(warnings("phrase rest a bit\n\twait 1 frame\nend\nrepeat forever\n\trest a bit\nend\n").empty());
	CHECK(warnings("set things to []\nrepeat forever\n\tthings.first()\nend\n").empty());
	// 'while true' and 'until false' never stop by themselves either.
	found = warnings("while true do\n\tsay 1\nend\n");
	REQUIRE(found.size() == 1);
	CHECK_EQ(found[0].Message, std::string("This 'while true' loop never waits, so nothing else in the game can run while it does. "
		"Add a 'wait' inside it, like: wait 1 frame"));
	found = warnings("until false\n\tsay 1\nend\n");
	REQUIRE(found.size() == 1);
	CHECK_NE(found[0].Message.find("This 'until false' loop"), std::string::npos);
	CHECK_NE(warnings("while yes do\n\tsay 1\nend\n")[0].Message.find("This 'while yes' loop"), std::string::npos);
	CHECK(warnings("while true do\n\twait 1 frame\nend\n").empty());
	CHECK(warnings("set n to 0\nwhile true do\n\tadd 1 to n\n\tif n > 3 then stop end\nend\n").empty());

	// Other loops are left alone: their condition can change.
	CHECK(warnings("repeat 3 times\n\tsay 1\nend\n").empty());
	CHECK(warnings("set n to 0\nwhile n < 3 do\n\tadd 1 to n\nend\n").empty());
	CHECK(warnings("set going to true\nwhile going do\n\tset going to false\nend\n").empty());
	CHECK(warnings("while true and false do\n\tsay 1\nend\n").empty());
}

TEST_CASE("OptionalTypesWarnOnPlainValuesOnly")
{
	VM vm;
	auto warnings = [&vm](const std::string& source) { return vm.FindWarnings(source, "test.forge", {}); };
	const std::string heal = "function heal(amount as number, who as text is \"you\")\nend\n";
	std::vector<CompileError> found = warnings("heal(\"ten\")\n" + heal);
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 1);
	CHECK(found[0].Column == 6);
	CHECK_EQ(found[0].Message, std::string("'heal' takes a number for 'amount' (amount as number), but this is text."));
	CHECK_CONTAINS(warnings(heal + "heal(5, 3)\n").at(0).Message, "'heal' takes text for 'who' (who as text), but this is a number.");
	CHECK_CONTAINS(warnings(heal + "heal(amount: [1])\n").at(0).Message, "but this is a list.");
	CHECK_CONTAINS(warnings(heal + "heal(nothing)\n").at(0).Message, "but this is nothing.");
	CHECK_CONTAINS(warnings("function f(k as Enemy)\nend\nf({a: 1})\n").at(0).Message, "'f' takes an Enemy for 'k' (k as Enemy), but this is a map.");
	CHECK_CONTAINS(warnings("function f(g as function)\nend\nf(3)\n").at(0).Message, "'f' takes a function for 'g'");
	// Right types, values from variables, worked-out values and a local of the same name: no warning.
	CHECK(warnings(heal + "heal(5)\nheal(-2, who: \"Bo\")\nset x to \"a\"\nheal(x)\nheal(1 + 2)\nheal([1][1])\n").empty());
	CHECK(warnings(heal + "function g(heal)\n\theal(\"x\")\nend\n").empty());
	// Types never change what runs.
	CHECK_EQ(Output(heal + "function twice(n as number)\n\treturn n * 2\nend\nsay twice(\"ab\")\n"), std::string("abab\n"));
}

TEST_CASE("OptionalTypesKeepOldMeaningsAndExplainMistakes")
{
	CHECK_EQ(Output("set as to 3\nsay as\n"), std::string("3\n"));
	CHECK_EQ(Output("function f(a as number is 2)\n\treturn a\nend\nsay f()\n"), std::string("2\n"));
	// 'function' and 'kind' after 'as' open no block, so the 'end's pair as written.
	CHECK_EQ(Output("function f(g as function, k as kind)\n\treturn 1\nend\nsay f(2, 3)\nset h to function(x as function) return 4 end\nsay h(1)\n"),
		std::string("1\n4\n"));
	CHECK_CONTAINS(CompileFails("function f(a as nmber)\nend\n").Message, "'nmber' isn't a type. Did you mean 'number'?");
	CHECK_CONTAINS(CompileFails("function f(a as)\nend\n").Message, "After 'as', write a type, like: amount as number");
	CHECK_CONTAINS(CompileFails("function f(a... as number)\nend\n").Message, "'a...' is always a list of the values left over, so it can't have a type.");
	CHECK_CONTAINS(CompileFails("function f(a as number...)\nend\n").Message, "'a...' is always a list of the values left over, so it can't have a type.");
}

TEST_CASE("TestsRunOnlyThroughRunTest")
{
	VM vm;
	std::vector<std::string> printed;
	vm.SetPrintHandler([&printed](const std::string& text) { printed.push_back(text); });
	vm.SetErrorHandler([](const ScriptError&) {});
	LoadResult loaded = vm.Load(
		"set count to 0\n"
		"test \"first\"\n\tadd 1 to count\n\texpect count is 1\nend\n"
		"test \"waits\" do\n\twait 2 seconds\n\texpect count is 1\nend\n"
		"test \"fails\"\n\texpect count is 5\n\tsay \"not reached\"\nend\n"
		"test \"forever\"\n\twait until false\nend\n"
		"say \"ran\"\n", "tests.forge");
	REQUIRE(loaded.Success);
	CHECK(printed == std::vector<std::string>{ "ran" });
	CHECK(vm.GetTestNames(loaded.LoadedModule) == std::vector<std::string>{ "first", "waits", "fails", "forever" });

	std::string failure;
	CHECK(vm.RunTest(loaded.LoadedModule, 0, failure));
	CHECK(vm.RunTest(loaded.LoadedModule, 1, failure));
	CHECK_FALSE(vm.RunTest(loaded.LoadedModule, 2, failure));
	CHECK_CONTAINS(failure, "Expected count is 5, but count is 1.");
	CHECK_CONTAINS(failure, "tests.forge line 11");
	CHECK_FALSE(vm.RunTest(loaded.LoadedModule, 3, failure, 1.0, 5.0));
	CHECK_CONTAINS(failure, "still waiting after 5 seconds of game time");
	CHECK_FALSE(vm.RunTest(loaded.LoadedModule, 9, failure));
	CHECK(printed == std::vector<std::string>{ "ran" });
}

TEST_CASE("TestAndExpectKeepOldMeanings")
{
	CHECK_EQ(Output("set test to 3\nset expect to 4\nsay test + expect\n"), std::string("7\n"));
	CHECK_EQ(Output("function expect(x)\n\treturn x + 1\nend\nsay expect(1)\nsay expect (2)\n"), std::string("2\n3\n"));
	CHECK_EQ(Output("set expect to 5\nsay expect - 1\n"), std::string("4\n"));
	CHECK_EQ(Output("phrase test (x)\n\tsay \"phrase {x}\"\nend\ntest \"a\"\n"), std::string("phrase a\n"));
	CHECK_EQ(Output("phrase expect (x)\n\tsay \"phrase {x}\"\nend\nexpect 3\n"), std::string("phrase 3\n"));
	CHECK_CONTAINS(CompileFails("function f()\n\ttest \"x\"\n\t\tsay 1\n\tend\nend\n").Message, "Write tests at the top level of the script");
	// A test's own names stay inside it, and its 'end' pairs with it.
	CHECK_EQ(Output("test \"t\"\n\tset inside to 1\nend\nsay \"after\"\n"), std::string("after\n"));
	// 'expect' with logic shows the whole condition.
	CHECK_CONTAINS(Output("try\n\texpect 1 is 1 and 2 is 3\ncatch problem\n\tsay problem\nend\n"), "Expected 1 is 1 and 2 is 3, but it is false.");
}

TEST_CASE("SharedValuesKeepOldMeaningsAndExplainMistakes")
{
	// 'shared' is still a name, also as a field.
	CHECK_EQ(Output("set shared to 4\nsay shared\n"), std::string("4\n"));
	CHECK_EQ(Output("kind E\n\tset shared to 2\nend\nsay new E().shared\n"), std::string("2\n"));
	// A bare name in a method is not the shared value.
	CHECK_CONTAINS(Output("kind E\n\tshared c is 1\n\tfunction f()\n\t\treturn c\n\tend\nend\ntry\n\tsay new E().f()\ncatch problem\n\tsay problem\nend\n"),
		"'c' has not been given a value yet");
	// Mistakes say what to write.
	CHECK_CONTAINS(Output("kind E\n\tshared c is 0\nend\ntry\n\tsay new E().c\ncatch problem\n\tsay problem\nend\n"),
		"'E' has no field or function called 'c'. 'c' is shared by every E: write E.c");
	CHECK_CONTAINS(Output("kind E\nend\ntry\n\tset E.x to 1\ncatch problem\n\tsay problem\nend\n"),
		"Cannot change 'x' on a kind. To give every E one value they share, write inside the kind: shared x is 0");
	CHECK_CONTAINS(Output("kind E\n\tshared count is 0\nend\ntry\n\tsay E.cuont\ncatch problem\n\tsay problem\nend\n"),
		"The kind 'E' has no property called 'cuont'. Did you mean 'count'?");
	CHECK_CONTAINS(CompileFails("kind E\n\tshared name is 0\nend\n").Message, "A kind's 'name' is already its name");
	CHECK_CONTAINS(CompileFails("kind E\n\tshared a is 0\n\tshared a is 1\nend\n").Message, "'a' is shared twice in this kind.");
	CHECK_CONTAINS(CompileFails("kind E\n\tsay 1\nend\n").Message, "Inside a kind, use 'set name to value' for fields and 'function' for methods");
}

TEST_CASE("AnyNumberOfValuesExplainsMistakes")
{
	CHECK_CONTAINS(CompileFails("function f(a..., b)\nend\n").Message, "Only the last value can collect the rest, like: function total(first, others...)");
	CHECK_CONTAINS(CompileFails("function f(a... is 3)\nend\n").Message, "'a...' gets the values left over, so it can't have a default.");
	CHECK_CONTAINS(Output("function f(a, b...)\nend\ntry\n\tf()\ncatch problem\n\tsay problem\nend\n"), "'f' needs at least 1 value, but was given 0 values.");
	CHECK_CONTAINS(Output("function f(a...)\nend\ntry\n\tf(a: [1])\ncatch problem\n\tsay problem\nend\n"),
		"'f' collects the values left over in 'a', so it can't be named.");
	// Each call gets a new list, and the values are the ones given, in order.
	CHECK_EQ(Output("function f(xs...)\n\tadd 9 to xs\n\treturn xs\nend\nsay f(1)\nsay f()\n"), std::string("[1, 9]\n[9]\n"));
	// A parameter list without '...' still says what it said.
	CHECK_CONTAINS(CompileFails("function f(a b)\nend\n").Message, "Expected ')' after the parameters.");
}

TEST_CASE("ForEachOverAnObject")
{
	// Changing a field's value while going through them is seen; the loop ends after the last field.
	CHECK_EQ(Output("kind K\n\tset a to 1\n\tset b to 2\nend\nset k to new K()\nfor each f, v in k do\n\tset k.b to 9\n\tsay v\nend\n"),
		std::string("1\n9\n"));
	CHECK_EQ(Output("kind K\nend\nfor each f in new K() do\n\tsay f\nend\nsay \"none\"\n"), std::string("none\n"));
	// Other values are still an error, which now names objects.
	CHECK_CONTAINS(Output("try\n\tfor each x in 5 do\n\tend\ncatch problem\n\tsay problem\nend\n"),
		"'for each' needs a list, a map, text or an object made from a kind, but got number.");
}

TEST_CASE("ChoicesKeepOldMeaningsAndExplainMistakes")
{
	// 'choices' and 'are' are still names, and a phrase starting with 'choices' still wins.
	CHECK_EQ(Output("set choices to 2\nset are to 3\nsay choices + are\n"), std::string("5\n"));
	CHECK_EQ(Output("phrase choices (x) are (y)\n\tsay \"phrase {x} {y}\"\nend\nchoices 1 are 2\n"), std::string("phrase 1 2\n"));
	// A choice set is a constant whose choices are their names as text.
	CHECK_EQ(Output("choices State are idle, running\nsay State.running is \"running\"\nsay State.length\n"), std::string("true\n2\n"));
	CHECK_CONTAINS(Output("choices State are idle\ntry\n\tset State to 3\ncatch problem\n\tsay problem\nend\n"), "'State' is a constant");
	// Mistakes, also above the line that makes the set.
	CHECK_CONTAINS(CompileFails("say State.idel\nchoices State are idle, running\n").Message,
		"State has no choice 'idel'. Did you mean 'idle'? Its choices are: idle, running.");
	CHECK_CONTAINS(CompileFails("function f()\n\tchoices S are a\nend\n").Message, "Write choices at the top level of the script");
	CHECK_CONTAINS(CompileFails("choices S are a, 3\n").Message, "Each choice is a name, like: choices S are idle, running");
	CHECK_CONTAINS(CompileFails("choices S are a, a\n").Message, "'a' is in S's choices twice.");
	// A local named like the set is the local, so its members aren't checked.
	CHECK_EQ(Output("choices S are a\nfunction f(S)\n\treturn S.b\nend\nsay f({b: 1})\n"), std::string("1\n"));

	// A match over one set's choices, with no otherwise, warns about each choice it leaves out.
	VM vm;
	std::vector<CompileError> found = vm.FindWarnings(
		"choices State are idle, running, jumping\nset s to State.idle\nmatch s\n\tis State.idle then say 1\n\tis State.running then say 2\nend\n", "test.forge", {});
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 3);
	CHECK_CONTAINS(found[0].Message, "This match has no case for State.jumping, and no otherwise.");
	CHECK(vm.FindWarnings("choices State are idle, running\nset s to State.idle\nmatch s\n\tis State.idle then say 1\n\totherwise say 2\nend\n", "test.forge", {}).empty());
	CHECK(vm.FindWarnings("choices State are idle, running\nset s to State.idle\nmatch s\n\tis State.idle, State.running then say 1\nend\n", "test.forge", {}).empty());
	CHECK(vm.FindWarnings("choices State are idle, running\nset s to State.idle\nmatch s\n\tis State.idle then say 1\n\tis \"x\" then say 2\nend\n", "test.forge", {}).empty());
}

TEST_CASE("EveryAndAnyKeepOldMeanings")
{
	// A script's own 'every' or 'any' keeps its meaning; so does a timer and a phrase.
	CHECK_EQ(Output("set any to 3\nsay any\n"), std::string("3\n"));
	CHECK_EQ(Output("function every(x)\n\treturn x\nend\nsay every(4)\n"), std::string("4\n"));
	CHECK_EQ(Output("phrase every (x) in (y)\n\tsay \"phrase {x} {y}\"\nend\nevery 1 in 2\n"), std::string("phrase 1 2\n"));
	// A script's own 'defeated' makes 'is defeated' a comparison with it.
	CHECK_EQ(Output("set defeated to true\nsay any e in [false, true] is defeated\n"), std::string("true\n"));
	// Something must say what each item is.
	CHECK_CONTAINS(CompileFails("say every x in [1] 5\n").Message, "After 'every x in ...', say what each one is");
	CHECK_CONTAINS(CompileFails("say any x in [1]\n").Message, "After 'any x in ...', say what each one is");
}

TEST_CASE("ListWordsKeepOldMeanings")
{
	// A script's own word, of any sort, keeps its meaning.
	CHECK_EQ(Output("set the to 5\nsay the\n"), std::string("5\n"));
	CHECK_EQ(Output("function first(x)\n\treturn \"mine\"\nend\nsay first of [1]\n"), std::string("mine\n"));
	CHECK_EQ(Output("set item to 2\nsay item\n"), std::string("2\n"));
	CHECK_EQ(Output("function item(n)\n\treturn n * 2\nend\nsay item(3)\n"), std::string("6\n"));
	CHECK_EQ(Output("for each last in [1, 2] do\n\tsay last\nend\n"), std::string("1\n2\n"));
	CHECK_EQ(Output("set m to {first: 1, the: 2}\nsay m.first + m.the\n"), std::string("3\n"));
	// A worked-out position needs brackets; 'item' with no 'of' after it is the old error.
	CHECK_CONTAINS(CompileFails("set items to [1]\nset i to 1\nsay item i + 1 of items\n").Message, "Put brackets around a worked-out position");
	CHECK_CONTAINS(CompileFails("say item 2\n").Message, "Expected the end of the line");
	// The same errors as the property and index forms.
	CHECK_CONTAINS(Output("try\n\tsay item 5 of [1]\ncatch problem\n\tsay problem\nend\n"), "5");
	CHECK_CONTAINS(Output("try\n\tsay first of 3\ncatch problem\n\tsay problem\nend\n"), "first");
}

TEST_CASE("NumberWordsKeepOldMeanings")
{
	// A script's own 'even' or 'odd', anywhere in reach, keeps 'x is even' a comparison.
	CHECK_EQ(Output("set even to 4\nsay 4 is even\nsay 6 is even\n"), std::string("true\nfalse\n"));
	CHECK_CONTAINS(Output("try\n\tsay 3 is even\ncatch problem\n\tsay problem\nend\nset even to 3\n"), "'even' has not been given a value");
	CHECK_EQ(Output("function f(odd)\n\treturn 3 is odd\nend\nsay f(3)\nsay f(2)\n"), std::string("true\nfalse\n"));
	CHECK_EQ(Output("function odd()\n\treturn 1\nend\nsay 1 is odd\n"), std::string("false\n"));
	CHECK_EQ(Output("set m to {even: 2}\nsay m.even\n"), std::string("2\n"));
	// A value right after the word means it was a name being worked with, so the old error stays.
	CHECK_CONTAINS(Output("try\n\tsay 4 is even + 1\ncatch problem\n\tsay problem\nend\n"), "'even' has not been given a value");
	// 'even' and 'odd' need a number; 'a whole number' is just false.
	CHECK_CONTAINS(Output("try\n\tsay \"a\" is even\ncatch problem\n\tsay problem\nend\n"), "'is even' needs a number, but this is a text");
	CHECK_CONTAINS(Output("try\n\tsay nothing is odd\ncatch problem\n\tsay problem\nend\n"), "'is odd' needs a number, but this is nothing");
	CHECK_EQ(Output("say [] is a whole number\n"), std::string("false\n"));
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

TEST_CASE("CompileErrorsInNamedValues")
{
	CHECK_CONTAINS(CompileFails("function f(a, b)\nend\nf(a: 1, 2)\n").Message, "Put the named values after the others");
	CHECK_CONTAINS(CompileFails("function f(a, b)\nend\nf(a: 1, a: 2)\n").Message, "'a' is named twice in this call.");
	// A map in a call is still a map.
	CHECK_EQ(Output("function f(m)\n\treturn m.size\nend\nsay f({a: 1, b: 2})\n"), std::string("2\n"));
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

TEST_CASE("CompileErrorsInMatch")
{
	CHECK_CONTAINS(CompileFails("match x\nis 1 then say 1\n").Message, "This 'match' is missing its 'end'.");
	CHECK_CONTAINS(CompileFails("match x\nis 1 then say 1\notherwise\nsay 2\nis 3 then say 3\nend\n").Message,
		"Put 'otherwise' last in a 'match', after every 'is'.");
	CHECK_CONTAINS(CompileFails("is 4 then say 1\n").Message, "Each 'is' choice goes inside a 'match', like: match weapon");
	// A match with no 'is' choice says so, whether 'otherwise' or 'end' comes next.
	CHECK_CONTAINS(CompileFails("match score\notherwise\nsay 1\nend\n").Message,
		"A 'match' needs at least one 'is' choice before 'otherwise' or 'end'");
	CHECK(CompileFails("match score\notherwise\nsay 1\nend\n").Line == 1);
	CHECK_CONTAINS(CompileFails("match \"rain\"\nend\n").Message, "A 'match' needs at least one 'is' choice");
	// The checker reports it once and goes on after its 'end', inside a function too.
	{
		VM vm;
		const std::vector<CompileError> mistakes = vm.FindMistakes(
			"function f()\n\tmatch x\n\totherwise\n\t\tif yes then say 1 end\n\tend\n\tsay 2\nend\nset to 1\n", "test.forge");
		REQUIRE(mistakes.size() == 2);
		CHECK(mistakes[0].Line == 2);
		CHECK(mistakes[1].Line == 8);
	}
	// 'match' as a name or a script's own phrase before 'otherwise' still means what it did.
	{
		VM vm;
		CompileError error;
		CHECK(vm.Check("if yes\nmatch(1)\notherwise\nsay 2\nend\n", "test.forge", error));
	}
	CHECK(Output("phrase match (x)\n\tsay \"phrase {x}\"\nend\nif yes\n\tmatch 5\notherwise\n\tsay 0\nend\n") == "phrase 5\n");
	// An 'if' left open inside a choice is named, not the match.
	CHECK_CONTAINS(CompileFails("match x\nis 1 then\nif yes then say 1\nis 2 then say 2\nend\n").Message,
		"This 'if' is missing its 'end'.");
	// 'match' opens a block only when the next line starts with 'is': a script's own phrase keeps working.
	std::vector<ScriptVariable> variables = FindScriptVariables(
		"function f(x)\n\tmatch x\n\tis 1 then say 1\n\tend\n\tset inside to 2\nend\nset speed to 3\n");
	REQUIRE(variables.size() == 1);
	CHECK(variables[0].Name == "speed");
}

TEST_CASE("CompileErrorsInTakingApart")
{
	CHECK_CONTAINS(CompileFails("set a, a to [1, 2]\n").Message, "'a' is named twice");
	CHECK_CONTAINS(CompileFails("set a, b [1, 2]\n").Message, "Expected 'to' after the names, like: set x, y to position");
	// A single name is an ordinary 'set', as before.
	CHECK_CONTAINS(CompileFails("set a 5\n").Message, "Expected 'to' after the name, like: set health to 100");
	// The Inspector only lists plain values, and still finds the ones after it.
	std::vector<ScriptVariable> variables = FindScriptVariables("set x, y to [1, 2]\nset speed to 3\n");
	REQUIRE(variables.size() == 1);
	CHECK(variables[0].Name == "speed");
}

TEST_CASE("WarningsForValuesThatHideBuiltins")
{
	auto warnings = [](const std::string& source, const std::vector<std::string>& engine = {})
	{
		VM vm;
		return vm.FindWarnings(source, "test.forge", engine);
	};

	// A script variable hides the built-in everywhere, even in a function defined before it.
	std::vector<CompileError> found = warnings("set max to 10\nsay max(3, 4)\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 1);
	CHECK(found[0].Column == 5);
	CHECK_CONTAINS(found[0].Message, "hides the built-in function 'max', so max(...) on line 2 won't call it.");
	found = warnings("function f()\n\tsay max(1, 2)\nend\nset max to 3\nf()\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 4);

	// A parameter or a function's own value hides it only where the call can find it.
	found = warnings("function f(round)\n\treturn round(2.5)\nend\n");
	REQUIRE(found.size() == 1);
	CHECK(found[0].Line == 1);
	CHECK(found[0].Column == 12);
	CHECK(warnings("function attack(x)\n\tset length to 5\n\treturn length\nend\nsay length([1, 2])\n").empty());
	CHECK(warnings("for each random in [1]\n\tsay random\nend\nsay random(1, 1)\n").empty());

	// Naming a value like a built-in that the script never calls is fine: a game's round counter.
	CHECK(warnings("set round to 1\nadd 1 to round\nsay round\n").empty());
	// Engine functions count when the checker is told about them.
	CHECK(warnings("set find_entity to 1\nsay find_entity(\"x\")\n").empty());
	CHECK(warnings("set find_entity to 1\nsay find_entity(\"x\")\n", { "find_entity" }).size() == 1);

	// Warnings never stop or change a script: it still runs (and fails) exactly as before.
	{
		VM vm;
		CompileError error;
		CHECK(vm.Check("set max to 10\nsay max(3, 4)\n", "test.forge", error));
		CHECK(vm.FindMistakes("set max to 10\nsay max(3, 4)\n", "test.forge").empty());
	}
	CHECK(Output("set max to 10\nsay max\n") == "10\n");
}

TEST_CASE("NestingTooDeepIsAMistakeNotACrash")
{
	// Audit S-1 and S-2: the parser goes one level down the C++ stack per bracket or block, so deep
	// nesting must stop with a mistake (and quickly), never overflow the stack.
	auto repeated = [](const std::string& text, size_t count)
	{
		std::string out;
		out.reserve(text.size() * count);
		for (size_t i = 0; i < count; i++)
			out += text;
		return out;
	};
	const char* tooDeep = "This is nested too deeply (more than 256 levels of brackets or blocks).";

	const std::vector<std::string> hostile = {
		"set x to " + repeated("(", 100000) + "1" + repeated(")", 100000) + "\n",
		"set x to " + repeated("[", 100000) + "1" + repeated("]", 100000) + "\n",
		"set x to " + repeated("{a: ", 100000) + "1" + repeated("}", 100000) + "\n",
		"say " + repeated("f(", 100000) + "1" + repeated(")", 100000) + "\n",
		"say " + repeated("- ", 100000) + "1\n",
		"say " + repeated("not ", 100000) + "yes\n",
		"say " + repeated("2 ^ ", 100000) + "1\n",
		repeated("if yes then\n", 100000) + "say 1\n" + repeated("end\n", 100000),
		// Audit S-4: the 1.6.0 sentence forms read a value inside themselves, so each is a level too.
		"say " + repeated("item 1 of ", 30000) + "x\n",
		"say " + repeated("first of ", 30000) + "x\n",
		"say " + repeated("the ", 30000) + "x\n",
		"say " + repeated("every a in ", 30000) + "x is 1\n",
	};
	for (const std::string& source : hostile)
	{
		VM vm;
		const std::vector<CompileError> mistakes = vm.FindMistakes(source, "deep.forge");
		REQUIRE(mistakes.size() == 1);
		CHECK_CONTAINS(mistakes[0].Message, tooDeep);
		CompileError error;
		CHECK_FALSE(vm.Check(source, "deep.forge", error));
		CHECK_CONTAINS(error.Message, tooDeep);
	}

	// Real code never comes near the limit: 150 levels of each still work. (Not 255: the stack budget may
	// stop sooner in builds with big frames, such as sanitizer builds.)
	CHECK(Output("set x to " + repeated("(", 150) + "1" + repeated(")", 150) + "\nsay x\n") == "1\n");
	CHECK(Output("set x to " + repeated("[", 150) + "1" + repeated("]", 150) + "\nsay length(x)\n") == "1\n");
	CHECK(Output(repeated("if yes then\n", 150) + "say 1\n" + repeated("end\n", 150)) == "1\n");

	// The rest of the script is still checked after a line that is too deep.
	VM vm;
	const std::vector<CompileError> mistakes = vm.FindMistakes("set x to " + repeated("(", 1000) + "1" + repeated(")", 1000) + "\nset to 5\n", "deep.forge");
	REQUIRE(mistakes.size() == 2);
	CHECK(mistakes[1].Line == 2);
}

TEST_CASE("UseOnlyReachesScriptsInItsPlace")
{
	// Audit S-3: 'use' can't reach files outside the project (or the script's folder), or files that
	// aren't scripts. It is a mistake in every compile, before any loader sees the path.
	const char* inside = "'use' only reaches scripts inside this script's place: the project's Assets folder, or the folder this script is in";
	for (const char* path : { "/etc/hosts", "C:/Games/x", "c:secret", "../Shared/x", "Scripts/../../x", "a/../b" })
	{
		INFO(path);
		CHECK_CONTAINS(CompileFails(std::string("use \"") + path + "\"\n").Message, inside);
	}
	CHECK_CONTAINS(CompileFails("use \"notes.txt\"\n").Message, "'use' only loads ForgeScript scripts: the name must end in .forge or have no extension");
	CHECK_CONTAINS(CompileFails("use \"\"\n").Message, "Write the name of the script to use in the quotes");
	// Backslashes are escapes in ForgeScript text, so these are checked as the paths they become.
	CHECK_FALSE(CheckUsePath("\\\\server\\share\\x").empty());
	CHECK_FALSE(CheckUsePath("Scripts\\..\\..\\x").empty());
	for (const char* path : { "helpers", "Scripts/helpers", "Scripts\\helpers", "helpers.forge", "Lib.FORGE", "v1.2/helpers" })
		CHECK(CheckUsePath(path).empty());

	// A host's loader is never asked for a refused path, even by the phrase prescan.
	VM vm;
	std::vector<std::string> asked;
	vm.SetModuleLoader([&asked](const std::string& path, std::string&, std::string&) { asked.push_back(path); return false; });
	vm.FindMistakes("use \"../secret\"\nuse \"ok\"\n", "test.forge");
	CHECK(std::find(asked.begin(), asked.end(), "../secret") == asked.end());
	CHECK(std::find(asked.begin(), asked.end(), "ok") != asked.end());
}

TEST_CASE("TextWithBytesThatArentUtf8Warns")
{
	// A script saved as Windows-1252 still runs, but its text shows up garbled: the checker says so,
	// once per piece of text, at the first such byte.
	VM vm;
	auto unreadable = [&vm](const std::string& source)
	{
		std::vector<CompileError> found;
		for (CompileError& warning : vm.FindWarnings(source, "t.forge", {}))
		{
			if (warning.Message.find("isn't readable text") != std::string::npos)
				found.push_back(std::move(warning));
		}
		return found;
	};
	std::vector<CompileError> found = unreadable("say \"caf\xE9\"\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("This text has a byte that isn't readable text, so it will show up garbled in the game. "
		"Save the script as UTF-8 (in most editors: Save As, then Encoding: UTF-8)."));
	CHECK_EQ(found[0].Line, 1);
	CHECK_EQ(found[0].Column, 9);
	// After a filled-in value, inside long text, two bad bytes in one text, and an encoded surrogate.
	found = unreadable("set n to 1\nsay \"a {n} \xFF\"\nset t to \"\"\"\n\tok\n\tno \xC0\xAF\n\t\"\"\"\nsay \"\xFF\xFE\"\nsay \"\xED\xA0\x80\"\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(4));
	CHECK_EQ(found[0].Line, 2);
	CHECK_EQ(found[0].Column, 12);
	CHECK_EQ(found[1].Line, 5);
	CHECK_EQ(found[1].Column, 5);
	CHECK_EQ(found[2].Line, 7);
	CHECK_EQ(found[3].Line, 8);
	// Real UTF-8 (two, three and four bytes) stays quiet, and the script still runs as before.
	CHECK(unreadable("say \"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8E\xAE\"\n").empty());
	CHECK_EQ(Output("say \"caf\xE9\"\n"), std::string("caf\xE9\n"));
}

TEST_CASE("Utf8FirstInvalidFindsTheFirstBadByte")
{
	CHECK_EQ(Utf8::FirstInvalid("plain"), std::string_view::npos);
	CHECK_EQ(Utf8::FirstInvalid("caf\xC3\xA9"), std::string_view::npos);
	CHECK_EQ(Utf8::FirstInvalid("\xF4\x8F\xBF\xBF"), std::string_view::npos); // U+10FFFF
	CHECK_EQ(Utf8::FirstInvalid("ab\xE9"), static_cast<size_t>(2));          // Latin-1 é
	CHECK_EQ(Utf8::FirstInvalid("a\xC3"), static_cast<size_t>(1));           // cut short
	CHECK_EQ(Utf8::FirstInvalid("\xC0\xAF"), static_cast<size_t>(0));        // overlong
	CHECK_EQ(Utf8::FirstInvalid("\xED\xA0\x80"), static_cast<size_t>(0));    // surrogate
	CHECK_EQ(Utf8::FirstInvalid("\xF4\x90\x80\x80"), static_cast<size_t>(0)); // past U+10FFFF
	CHECK_EQ(Utf8::FirstInvalid("\x80"), static_cast<size_t>(0));            // lone continuation
}

TEST_CASE("ReturnTypesAreForCheckersOnly")
{
	// 'as number' after a function's ')' (1.7.0) says what it gives back. It never changes what runs.
	CHECK_EQ(Output("function heal(amount as number) as number\n\treturn amount + 1\nend\nsay heal(2)\n"), std::string("3\n"));
	CHECK_EQ(Output("function label() as number\n\treturn \"x\"\nend\nsay label()\n"), std::string("x\n"));
	CHECK_EQ(Output("kind Enemy\n\tfunction hp() as number\n\t\treturn 7\n\tend\nend\nsay Enemy().hp()\n"), std::string("7\n"));

	VM vm;
	auto returns = [&vm](const std::string& source)
	{
		std::vector<CompileError> found;
		for (CompileError& warning : vm.FindWarnings(source, "t.forge", {}))
		{
			if (warning.Message.find("says it gives back") != std::string::npos)
				found.push_back(std::move(warning));
		}
		return found;
	};
	std::vector<CompileError> found = returns("function status() as text\n\treturn 5\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("'status' says it gives back text (as text), but this gives back a number."));
	CHECK_EQ(found[0].Line, 2);
	CHECK_EQ(found[0].Column, 9);

	found = returns("function status() as text\n\tsay \"hi\"\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("'status' says it gives back text (as text), but it never returns a value, so it always "
		"gives back nothing. Add 'return' with the text it should give back."));
	CHECK_EQ(found[0].Line, 1);
	CHECK_EQ(found[0].Column, 22);
	CHECK_FALSE(found[0].FixTitle.size() > 0); // no quick fix: 'return nothing' would hide the mistake

	// Methods too; plain values only, so a value worked out is never warned about.
	CHECK_EQ(returns("kind Enemy\n\tfunction hp() as number\n\t\treturn [1]\n\tend\nend\n").size(), static_cast<size_t>(1));
	CHECK(returns("function f(x) as number\n\treturn x * 2\nend\n").empty());
	// 'nothing' is how a function says "none found", and a function that only fails never ends.
	CHECK(returns("function nearest() as Enemy\n\treturn nothing\nend\n").empty());
	CHECK(returns("function nearest() as number\n\tif 1 > 2 then\n\t\treturn 3\n\tend\n\treturn\nend\n").empty());
	CHECK(returns("function broken() as number\n\tfail \"not yet\"\nend\n").empty());
	// An inner function's returns are its own.
	CHECK_EQ(returns("function outer() as number\n\tfunction inner()\n\t\treturn \"x\"\n\tend\n\tsay inner()\nend\n").size(), static_cast<size_t>(1));
	CHECK(returns("function f()\n\treturn 5\nend\n").empty());

	// A misspelled type word is a mistake, as for parameters.
	ForgeTest::ScriptRun run = ForgeTest::RunScript("function f() as nmber\n\treturn 1\nend\n");
	CHECK_CONTAINS(run.AllErrors(), "'nmber' isn't a type. Did you mean 'number'?");
}

TEST_CASE("TypesFollowAFunctionsOwnVariables")
{
	// A variable of a function that is only ever set to one type of plain value, or a typed parameter it
	// never sets, keeps that type for the call check. Anything less certain stays quiet.
	VM vm;
	auto takes = [&vm](const std::string& source)
	{
		std::vector<CompileError> found;
		for (CompileError& warning : vm.FindWarnings("function heal(amount as number)\n\tsay amount\nend\n" + source, "t.forge", {}))
		{
			if (warning.Message.find("takes a number for 'amount'") != std::string::npos)
				found.push_back(std::move(warning));
		}
		return found;
	};
	std::vector<CompileError> found = takes("function f()\n\tset name to \"Ana\"\n\theal(name)\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("'heal' takes a number for 'amount' (amount as number), but 'name' is text (it is only ever set to text)."));
	CHECK_EQ(found[0].Line, 6);
	CHECK_EQ(found[0].Column, 7);
	found = takes("function f(who as text)\n\theal(amount: who)\nend\n");
	REQUIRE_EQ(found.size(), static_cast<size_t>(1));
	CHECK_EQ(found[0].Message, std::string("'heal' takes a number for 'amount' (amount as number), but 'who' is text (who as text)."));

	// Quiet: the right type, mixed types, a worked-out value, loops, name lists, a re-set parameter,
	// handlers' values, script variables (other scripts may set them), and variables of an outer function.
	CHECK(takes("function f()\n\tset n to 5\n\theal(n)\nend\n").empty());
	CHECK(takes("function f()\n\tset n to \"a\"\n\tset n to 5\n\theal(n)\nend\n").empty());
	CHECK(takes("function f(x)\n\tset n to x\n\theal(n)\nend\n").empty());
	CHECK(takes("function f()\n\tset n to \"a\"\n\tfor each n in [1, 2] do\n\t\theal(n)\n\tend\nend\n").empty());
	CHECK(takes("function f()\n\tset a, n to [1, \"x\"]\n\theal(n)\nend\n").empty());
	CHECK(takes("function f(who as text)\n\tset who to 4\n\theal(who)\nend\n").empty());
	CHECK(takes("function f(who)\n\theal(who)\nend\n").empty());
	CHECK(takes("when hit with who\n\theal(who)\nend\n").empty());
	CHECK(takes("set name to \"x\"\nheal(name)\n").empty());
	CHECK(takes("function f()\n\tset n to \"a\"\n\tfunction g()\n\t\tset n to 5\n\tend\n\theal(n)\nend\n").empty());
	// A parameter of a kind's type is never compared: 'object' or a kind may hold many things.
	CHECK(vm.FindWarnings("function g(e as Enemy)\nend\nfunction f()\n\tset n to \"x\"\n\tg(n)\nend\n", "t.forge", {}).empty());
}
