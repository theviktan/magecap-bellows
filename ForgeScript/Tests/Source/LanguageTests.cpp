#include "TestFramework.h"

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
