#include "TestFramework.h"

#include "ForgeScript/Compiler/ScriptVariables.h"

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
