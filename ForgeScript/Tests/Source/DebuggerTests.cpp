#include "TestFramework.h"

#include <string>
#include <vector>

// The debugger API (step 1.9): breakpoints, stepping, pausing, the call stack and variables, used
// the way an editor uses it.

using namespace ForgeScript;

namespace {

	// A VM with a debugger attached that records every stop and everything the scripts print.
	struct Debugged
	{
		VM Machine;
		std::vector<DebugStop> Stops;
		std::vector<std::string> Printed;
		// Run inside the handler at each stop (for example a step), like an editor that steps on its own.
		std::function<void(VM&)> OnStop;

		Debugged()
		{
			Machine.SetPrintHandler([this](const std::string& text) { Printed.push_back(text); });
			Machine.SetDebugHandler([this](const DebugStop& stop)
			{
				Stops.push_back(stop);
				if (OnStop)
					OnStop(Machine);
			});
		}

		Module* Load(const std::string& source, const std::string& file = "game.forge")
		{
			Module* module = Machine.CreateModule(file);
			LoadResult result = Machine.RunInModule(module, source, file);
			REQUIRE_MESSAGE(result.Success, result.ErrorMessage);
			return module;
		}

		int StoppedLine() const { return Machine.IsStopped() ? Machine.GetStop().Line : 0; }
	};

	std::string FindText(const std::vector<DebugVariable>& variables, const std::string& name)
	{
		for (const DebugVariable& variable : variables)
		{
			if (variable.Name == name)
				return variable.Text;
		}
		return "<missing>";
	}

	std::vector<std::string> Names(const std::vector<DebugVariable>& variables)
	{
		std::vector<std::string> names;
		for (const DebugVariable& variable : variables)
			names.push_back(variable.Name);
		return names;
	}

}

TEST_CASE("DebuggerStopsAtBreakpointsAndContinues")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 2, 4 });
	debug.Load(
		"say \"one\"\n"      // 1
		"say \"two\"\n"      // 2 breakpoint
		"say \"three\"\n"    // 3
		"say \"four\"\n");   // 4 breakpoint

	REQUIRE(debug.Machine.IsStopped());
	REQUIRE_EQ(debug.Stops.size(), static_cast<size_t>(1));
	CHECK(debug.Stops[0].Reason == DebugStopReason::Breakpoint);
	CHECK_EQ(debug.Stops[0].File, std::string("game.forge"));
	CHECK_EQ(debug.Stops[0].Line, 2);
	// Stopped before line 2 runs.
	CHECK_EQ(debug.Printed, std::vector<std::string> { "one" });

	// While stopped, time stands still and nothing else runs.
	debug.Machine.Update(1.0);
	CHECK_EQ(debug.Machine.GetTime(), 0.0);

	CHECK(debug.Machine.Continue());
	CHECK_EQ(debug.StoppedLine(), 4);
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "one", "two", "three" }));
	CHECK(debug.Machine.Continue());
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Printed.size(), static_cast<size_t>(4));
	CHECK_FALSE(debug.Machine.Continue());
	CHECK_FALSE(debug.Machine.StepOver());
	CHECK(debug.Machine.GetCallStack().empty());
	CHECK(debug.Machine.GetVariables(0).empty());
}

TEST_CASE("DebuggerStopsOnceOnALineThatCallsAFunction")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 5 });
	debug.Load(
		"function double(n)\n"     // 1
		"\treturn n * 2\n"         // 2
		"end\n"                    // 3
		"set total to 0\n"         // 4
		"set total to double(4)\n" // 5 breakpoint: before and not again after the call returns
		"say total\n");            // 6
	CHECK_EQ(debug.StoppedLine(), 5);
	debug.Machine.Continue();
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(1));
	CHECK_EQ(debug.Printed, std::vector<std::string> { "8" });
}

TEST_CASE("DebuggerStepsOverIntoAndOutOfFunctions")
{
	const std::string source =
		"function add_one(n)\n"     // 1
		"\tset bigger to n + 1\n"   // 2
		"\treturn bigger\n"         // 3
		"end\n"                     // 4
		"set a to 1\n"              // 5 breakpoint
		"set b to add_one(a)\n"     // 6
		"say b\n";                  // 7

	SUBCASE("over")
	{
		Debugged debug;
		debug.Machine.SetBreakpoints("game.forge", { 5 });
		debug.Load(source);
		CHECK_EQ(debug.StoppedLine(), 5);
		debug.Machine.StepOver();
		CHECK_EQ(debug.StoppedLine(), 6);
		CHECK(debug.Machine.GetStop().Reason == DebugStopReason::Step);
		debug.Machine.StepOver();
		CHECK_EQ(debug.StoppedLine(), 7);
		debug.Machine.StepOver();
		CHECK_FALSE(debug.Machine.IsStopped());
		CHECK_EQ(debug.Printed, std::vector<std::string> { "2" });
	}
	SUBCASE("in and out")
	{
		Debugged debug;
		debug.Machine.SetBreakpoints("game.forge", { 6 });
		debug.Load(source);
		debug.Machine.StepIn();
		CHECK_EQ(debug.StoppedLine(), 2);
		std::vector<DebugFrame> stack = debug.Machine.GetCallStack();
		REQUIRE_EQ(stack.size(), static_cast<size_t>(2));
		CHECK_EQ(stack[0].Function, std::string("add_one"));
		CHECK_EQ(stack[0].Line, 2);
		CHECK_EQ(stack[0].File, std::string("game.forge"));
		CHECK(stack[1].Function.empty());
		CHECK_EQ(stack[1].Line, 6);
		CHECK(stack[1].Owner != nullptr);

		debug.Machine.StepIn();
		CHECK_EQ(debug.StoppedLine(), 3);
		debug.Machine.StepOut();
		// Back in the caller: the rest of line 6 runs, then the next line stops.
		CHECK_EQ(debug.StoppedLine(), 7);
		CHECK_EQ(debug.Machine.GetCallStack().size(), static_cast<size_t>(1));
		CHECK(debug.Printed.empty());
	}
}

TEST_CASE("DebuggerStepsOverRecursionAndThroughLoops")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 7 });
	debug.Load(
		"function count_down(n)\n"            // 1
		"\tif n is 0 then\n"                  // 2
		"\t\treturn 0\n"                      // 3
		"\tend\n"                             // 4
		"\treturn 1 + count_down(n - 1)\n"    // 5
		"end\n"                               // 6
		"for each i in [1, 2]\n"              // 7 breakpoint
		"\tsay count_down(i)\n"               // 8
		"end\n"                               // 9
		"say \"done\"\n");                    // 10
	std::vector<int> lines;
	debug.OnStop = [&lines](VM& vm)
	{
		lines.push_back(vm.GetStop().Line);
		vm.StepOver();
	};
	debug.Machine.StepOver();
	CHECK_FALSE(debug.Machine.IsStopped());
	// The loop's line is reached once per round; the recursion is stepped over.
	CHECK_EQ(lines, (std::vector<int> { 8, 7, 8, 7, 10 }));
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "1", "2", "done" }));
}

TEST_CASE("DebuggerHandlerCanStepWithoutGrowingTheStack")
{
	// A handler that steps at every stop walks the whole script from inside the handler.
	Debugged debug;
	std::string source;
	for (int i = 0; i < 2000; i++)
		source += "set x to " + std::to_string(i) + "\n";
	source += "say x\n";
	debug.OnStop = [](VM& vm) { vm.StepIn(); };
	debug.Machine.SetBreakpoints("game.forge", { 1 });
	debug.Load(source);
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(2001));
	CHECK_EQ(debug.Printed, std::vector<std::string> { "1999" });
}

TEST_CASE("DebuggerShowsVariables")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 10, 17 });
	Module* module = debug.Load(
		"set speed to 2.5\n"                         // 1
		"set name to \"Hero\"\n"                     // 2
		"set bag to [1, \"key\"]\n"                  // 3
		"function helper()\n"                        // 4
		"end\n"                                      // 5
		"function walk(steps)\n"                     // 6
		"\tset far to steps * speed\n"               // 7
		"\tfor each step in [1]\n"                   // 8
		"\t\tset far to far + step\n"                // 9
		"\t\tsay far\n"                              // 10 breakpoint
		"\tend\n"                                    // 11
		"\treturn far\n"                             // 12
		"end\n"                                      // 13
		"function make_counter()\n"                  // 14
		"\tset count to 7\n"                         // 15
		"\treturn function()\n"                      // 16
		"\t\treturn count\n"                         // 17 breakpoint
		"\tend\n"                                    // 18
		"end\n"                                      // 19
		"walk(2)\n"                                  // 20
		"say make_counter()()\n");                   // 21

	REQUIRE_EQ(debug.StoppedLine(), 10);
	const std::vector<DebugVariable> locals = debug.Machine.GetVariables(0);
	CHECK_EQ(Names(locals), (std::vector<std::string> { "steps", "far", "step" }));
	CHECK_EQ(FindText(locals, "steps"), std::string("2"));
	CHECK_EQ(FindText(locals, "far"), std::string("6"));
	CHECK_EQ(FindText(locals, "step"), std::string("1"));
	CHECK_EQ(locals[0].Type, std::string("number"));
	// The top level's own frame has no locals here; its variables are the script's.
	CHECK(debug.Machine.GetVariables(1).empty());
	CHECK(debug.Machine.GetVariables(2).empty());

	const std::vector<DebugVariable> globals = debug.Machine.GetGlobals(module);
	CHECK_EQ(Names(globals), (std::vector<std::string> { "speed", "name", "bag" }));
	CHECK_EQ(FindText(globals, "name"), std::string("\"Hero\""));
	CHECK_EQ(FindText(globals, "bag"), std::string("[1, \"key\"]"));
	CHECK(debug.Machine.GetGlobals(nullptr).empty());

	// Back to the loop's line for the next round, then out of the loop, where its variable is gone.
	debug.Machine.StepOver();
	CHECK_EQ(debug.StoppedLine(), 8);
	debug.Machine.StepOver();
	REQUIRE_EQ(debug.StoppedLine(), 12);
	CHECK_EQ(Names(debug.Machine.GetVariables(0)), (std::vector<std::string> { "steps", "far" }));

	// A function made inside another sees the outer variable it uses.
	debug.Machine.Continue();
	REQUIRE_EQ(debug.StoppedLine(), 17);
	CHECK_EQ(FindText(debug.Machine.GetVariables(0), "count"), std::string("7"));
	debug.Machine.Continue();
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "6", "7" }));
}

TEST_CASE("DebuggerShowsKindsWithoutRunningTheirCode")
{
	Debugged second;
	second.Machine.SetBreakpoints("game.forge", { 9 });
	Module* module = second.Load(
		"set described to 0\n"
		"kind Door\n"
		"\tset open to no\n"
		"\tfunction to_text()\n"
		"\t\tadd 1 to described\n"
		"\t\treturn \"a door\"\n"
		"\tend\n"
		"\tfunction toggle()\n"
		"\t\tset my open to not my open\n"
		"\tend\n"
		"end\n"
		"set front to new Door()\n"
		"front.toggle()\n"
		"say described\n");
	REQUIRE_EQ(second.StoppedLine(), 9);
	const std::vector<DebugVariable> locals = second.Machine.GetVariables(0);
	REQUIRE_EQ(Names(locals), std::vector<std::string> { "self" });
	CHECK_EQ(locals[0].Type, std::string("Door"));
	CHECK_EQ(locals[0].Text, std::string("Door {open: false}"));
	CHECK_EQ(FindText(second.Machine.GetGlobals(module), "front"), std::string("Door {open: false}"));
	CHECK_EQ(second.Machine.GetCallStack()[0].Function, std::string("toggle"));
	second.Machine.Continue();
	// to_text never ran for the debugger.
	CHECK_EQ(second.Printed, std::vector<std::string> { "0" });
}

TEST_CASE("DebuggerShowsAListThatHoldsItselfQuickly")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 8 });
	// "loop" holds itself; "huge" would be gigabytes of text if it were written out in full.
	Module* module = debug.Load(
		"set loop to [1]\nloop.add(loop)\nloop.add(loop)\nset huge to [\"x\"]\n"
		"repeat 12 times\n    set huge to [huge, huge, huge, huge, huge, huge, huge, huge]\nend\nsay 1\n");
	REQUIRE(debug.Machine.IsStopped());
	CHECK_EQ(FindText(debug.Machine.GetGlobals(module), "loop"), std::string("[1, [...], [...]]"));
	const std::string text = FindText(debug.Machine.GetGlobals(module), "huge");
	CHECK(text.size() <= 503);
	CHECK(text.ends_with("..."));
	debug.Machine.Continue();
}

TEST_CASE("DebuggerShortensLongValues")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 2 });
	Module* module = debug.Load("set long to \"\xC3\xA9\" * 600\nsay 1\n");
	REQUIRE(debug.Machine.IsStopped());
	const std::string text = FindText(debug.Machine.GetGlobals(module), "long");
	CHECK(text.size() <= 503);
	CHECK(text.ends_with("..."));
	// Cut between characters, not inside one.
	CHECK_EQ(text.substr(text.size() - 5, 2), std::string("\xC3\xA9"));
	debug.Machine.Continue();
}

TEST_CASE("DebuggerPausesTheNextLineAndStepsAcrossWaits")
{
	Debugged debug;
	debug.Load(
		"when update\n"        // 1
		"\tsay \"tick\"\n"     // 2
		"end\n"                // 3
		"function slow()\n"    // 4
		"\tsay \"before\"\n"   // 5
		"\twait 1 second\n"    // 6
		"\tsay \"after\"\n"    // 7
		"end\n"                // 8
		"start slow()\n");     // 9
	CHECK_EQ(debug.Printed, std::vector<std::string> { "before" });

	// A module with no handler for the event runs no line, so the pause is still waiting.
	debug.Machine.Pause();
	debug.Machine.Emit(debug.Machine.CreateModule("other.forge"), "update");
	CHECK_FALSE(debug.Machine.IsStopped());

	// A pause stops the next line any task runs: the waiting task after its wait.
	debug.Machine.Update(1.0);
	REQUIRE(debug.Machine.IsStopped());
	CHECK(debug.Machine.GetStop().Reason == DebugStopReason::Pause);
	CHECK_EQ(debug.StoppedLine(), 7);
	CHECK_EQ(debug.Machine.GetCallStack()[0].Function, std::string("slow"));
	debug.Machine.Continue();
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "before", "after" }));

	// Stepping over a wait: the task goes on waiting, and the step ends on the line after it.
	Debugged stepping;
	stepping.Machine.SetBreakpoints("game.forge", { 2 });
	stepping.Load("function slow()\n\twait 1 second\n\tsay \"after\"\nend\nstart slow()\nsay \"started\"\n");
	REQUIRE_EQ(stepping.StoppedLine(), 2);
	stepping.Machine.StepOver();
	CHECK_FALSE(stepping.Machine.IsStopped());
	CHECK_EQ(stepping.Printed, std::vector<std::string> { "started" });
	stepping.Machine.Update(0.5);
	CHECK_FALSE(stepping.Machine.IsStopped());
	stepping.Machine.Update(0.5);
	CHECK_EQ(stepping.StoppedLine(), 3);
	stepping.Machine.Continue();
	CHECK_EQ(stepping.Printed, (std::vector<std::string> { "started", "after" }));
}

TEST_CASE("DebuggerStopsInEventHandlers")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("enemy.forge", { 2 });
	Module* enemy = debug.Load("when hit with damage\n\tsay \"ouch {damage}\"\nend\n", "enemy.forge");
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK(debug.Machine.Emit(enemy, "hit", { Value::FromNumber(3) }));
	REQUIRE_EQ(debug.StoppedLine(), 2);
	CHECK_EQ(debug.Machine.GetCallStack()[0].Function, std::string("when hit"));
	CHECK_EQ(FindText(debug.Machine.GetVariables(0), "damage"), std::string("3"));
	debug.Machine.Continue();
	CHECK_EQ(debug.Printed, std::vector<std::string> { "ouch 3" });
}

TEST_CASE("DebuggerQueuesAStopThatComesWhileStopped")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("a.forge", { 2 });
	debug.Machine.SetBreakpoints("b.forge", { 2 });
	Module* a = debug.Load("when go\n\tsay \"a\"\nend\n", "a.forge");
	Module* b = debug.Load("when go\n\tsay \"b\"\nend\n", "b.forge");
	debug.Machine.Emit(a, "go");
	REQUIRE_EQ(debug.Machine.GetStop().File, std::string("a.forge"));
	// The second handler reaches its breakpoint while the first is held: it waits, unannounced.
	debug.Machine.Emit(b, "go");
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(1));
	CHECK_EQ(debug.Machine.GetStop().File, std::string("a.forge"));

	debug.Machine.Continue();
	REQUIRE_EQ(debug.Stops.size(), static_cast<size_t>(2));
	CHECK_EQ(debug.Machine.GetStop().File, std::string("b.forge"));
	CHECK_EQ(debug.Printed, std::vector<std::string> { "a" });
	debug.Machine.Continue();
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "a", "b" }));
}

TEST_CASE("DebuggerDropsStopsOfUnloadedScripts")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("a.forge", { 2 });
	debug.Machine.SetBreakpoints("b.forge", { 2 });
	Module* a = debug.Load("when go\n\tsay \"a\"\nend\n", "a.forge");
	Module* b = debug.Load("when go\n\tsay \"b\"\nend\n", "b.forge");
	debug.Machine.Emit(a, "go");
	debug.Machine.Emit(b, "go");
	REQUIRE(debug.Machine.IsStopped());

	// Unloading the held script (a hot reload, say) moves on to the next stop.
	debug.Machine.UnloadModule(a);
	CHECK_EQ(debug.Machine.GetStop().File, std::string("b.forge"));
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(2));
	debug.Machine.CancelTasks(nullptr);
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK(debug.Printed.empty());
	// The VM runs on normally afterwards.
	debug.Machine.ClearBreakpoints();
	debug.Machine.Emit(b, "go");
	CHECK_EQ(debug.Printed, std::vector<std::string> { "b" });
}

TEST_CASE("DebuggerPassesOverCodeThatMustFinishRightAway")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 2 });
	debug.Load(
		"set doubled to [1, 2].map(function(n)\n"
		"\treturn n * 2\n"   // 2: called back by 'map', so it cannot stop here
		"end)\n"
		"say doubled\n");
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK(debug.Stops.empty());
	CHECK_EQ(debug.Printed, std::vector<std::string> { "[2, 4]" });

	// VM::Call cannot be held either.
	Debugged call;
	Module* module = call.Load("function f()\n\treturn 5\nend\n");
	call.Machine.SetBreakpoints("game.forge", { 2 });
	Value result;
	REQUIRE(call.Machine.Call(call.Machine.GetGlobal(module, "f"), {}, &result));
	CHECK_EQ(result.AsNumber(), 5.0);
	CHECK_FALSE(call.Machine.IsStopped());
}

TEST_CASE("DebuggerKeepsStoppedValuesThroughGarbageCollection")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 3 });
	debug.Load(
		"function make()\n"
		"\tset parts to [\"a\", \"b\"].join(\"-\")\n"
		"\treturn parts\n"
		"end\n"
		"say make()\n");
	REQUIRE(debug.Machine.IsStopped());
	debug.Machine.CollectGarbage();
	CHECK_EQ(FindText(debug.Machine.GetVariables(0), "parts"), std::string("\"a-b\""));
	debug.Machine.Continue();
	CHECK_EQ(debug.Printed, std::vector<std::string> { "a-b" });
}

TEST_CASE("DebuggerBreakpointsCanChangeWhileStopped")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 1, 3 });
	debug.Load("say 1\nsay 2\nsay 3\nsay 4\n");
	REQUIRE_EQ(debug.StoppedLine(), 1);
	debug.Machine.SetBreakpoints("game.forge", { 4, 0, -2, 4 });
	debug.Machine.Continue();
	CHECK_EQ(debug.StoppedLine(), 4);
	debug.Machine.SetBreakpoints("game.forge", {});
	debug.Machine.Continue();
	CHECK_EQ(debug.Printed.size(), static_cast<size_t>(4));
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(2));
}

TEST_CASE("DebuggerCanBeTurnedOff")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("a.forge", { 2 });
	debug.Machine.SetBreakpoints("b.forge", { 2 });
	Module* a = debug.Load("when go\n\tsay \"a\"\nend\n", "a.forge");
	Module* b = debug.Load("when go\n\tsay \"b\"\nend\n", "b.forge");
	// A held stop, and another waiting its turn.
	debug.Machine.Emit(a, "go");
	debug.Machine.Emit(b, "go");
	REQUIRE(debug.Machine.IsStopped());
	const size_t stopsBefore = debug.Stops.size();

	debug.Machine.Pause();
	debug.Machine.StopDebugging();
	// The held tasks are gone without running the rest of their code or calling the handler.
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Stops.size(), stopsBefore);
	CHECK(debug.Printed.empty());
	CHECK_FALSE(debug.Machine.Continue());

	// No breakpoint or pause is left: everything runs straight through.
	debug.Machine.Emit(a, "go");
	debug.Machine.Emit(b, "go");
	CHECK_EQ(debug.Printed, (std::vector<std::string> { "a", "b" }));
	CHECK_EQ(debug.Stops.size(), stopsBefore);
}

TEST_CASE("DebuggerTurnedOffForgetsAStepAcrossAWait")
{
	Debugged debug;
	debug.Machine.SetBreakpoints("game.forge", { 2 });
	debug.Load("function slow()\n\twait 1 second\n\tsay \"after\"\nend\nstart slow()\n");
	REQUIRE_EQ(debug.StoppedLine(), 2);
	// The step goes on waiting with the task...
	debug.Machine.StepOver();
	CHECK_FALSE(debug.Machine.IsStopped());
	// ...until the debugger is turned off: then the task finishes without stopping.
	debug.Machine.StopDebugging();
	debug.Machine.Update(1.0);
	CHECK_FALSE(debug.Machine.IsStopped());
	CHECK_EQ(debug.Printed, std::vector<std::string> { "after" });
	CHECK_EQ(debug.Stops.size(), static_cast<size_t>(1));
}

TEST_CASE("DebuggerConditionsLogPointsAndEvaluate")
{
	const std::string source =
		"set lives to 3\n"                 // 1
		"function hurt(hp)\n"              // 2
		"\tset left to hp - 10\n"          // 3 a condition: stop only when left will be low
		"\treturn left\n"                  // 4 a log point
		"end\n"                            // 5
		"for each hp in [50, 15, 30] do\n" // 6
		"\tsay hurt(hp)\n"                 // 7
		"end\n";                           // 8

	SUBCASE("a condition stops only when it is true, and Evaluate reads the frame")
	{
		Debugged debug;
		debug.Machine.SetDetailedBreakpoints("game.forge", { { 3, "hp < 20", "" } });
		debug.Load(source);
		REQUIRE_EQ(debug.StoppedLine(), 3);
		CHECK_EQ(debug.Printed, std::vector<std::string> { "40" }); // hp 50 went past
		CHECK(debug.Machine.GetStop().Message.empty());

		DebugVariable value;
		std::string error;
		REQUIRE(debug.Machine.Evaluate(0, "hp * 2 + lives", value, error));
		CHECK_EQ(value.Text, std::string("33"));
		CHECK_EQ(value.Type, std::string("number"));
		REQUIRE(debug.Machine.Evaluate(0, "\"hp is {hp}\"", value, error));
		CHECK_EQ(value.Text, std::string("\"hp is 15\""));
		// The caller's frame has its own variables.
		REQUIRE(debug.Machine.Evaluate(1, "hp is 15 and lives is 3", value, error));
		CHECK_EQ(value.Text, std::string("true"));

		// No calls, so the game can't change; mistakes and missing names say why.
		CHECK_FALSE(debug.Machine.Evaluate(0, "hurt(5)", value, error));
		CHECK_CONTAINS(error, "without calling functions");
		// The message is about the Watch line itself, with no place in the hidden wrapper (S-16).
		CHECK(error.find(" line ") == std::string::npos);
		CHECK_FALSE(debug.Machine.Evaluate(0, "hp.health", value, error));
		CHECK(error.find(" line ") == std::string::npos);
		CHECK_FALSE(debug.Machine.Evaluate(0, "[1, 2].length()", value, error));
		CHECK_CONTAINS(error, "without calling functions");
		CHECK_FALSE(debug.Machine.Evaluate(0, "hp +", value, error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(debug.Machine.Evaluate(0, "1\nset lives to 0", value, error));
		CHECK_EQ(error, std::string("Write it on one line."));
		REQUIRE(debug.Machine.Evaluate(0, "lives", value, error));
		CHECK_EQ(value.Text, std::string("3"));

		debug.Machine.Continue();
		CHECK_FALSE(debug.Machine.IsStopped());
		CHECK_EQ(debug.Printed, (std::vector<std::string> { "40", "5", "20" }));
		CHECK_EQ(debug.Stops.size(), static_cast<size_t>(1));
	}

	SUBCASE("a log point prints and goes on")
	{
		Debugged debug;
		debug.Machine.SetDetailedBreakpoints("game.forge", { { 4, "", "left is {left} (from \"{hp}\")" } });
		debug.Load(source);
		CHECK_FALSE(debug.Machine.IsStopped());
		CHECK(debug.Stops.empty());
		CHECK_EQ(debug.Printed, (std::vector<std::string> { "left is 40 (from \"50\")", "40", "left is 5 (from \"15\")", "5",
			"left is 20 (from \"30\")", "20" }));
	}

	SUBCASE("a condition that can't be worked out stops and says why")
	{
		Debugged debug;
		debug.Machine.SetDetailedBreakpoints("game.forge", { { 3, "hp.health < 5", "" } });
		debug.Load(source);
		REQUIRE_EQ(debug.StoppedLine(), 3);
		CHECK_CONTAINS(debug.Machine.GetStop().Message, "The condition 'hp.health < 5' couldn't be worked out:");
		debug.Machine.StopDebugging();
	}

	SUBCASE("engine properties can be read, engine functions can't be called")
	{
		Debugged debug;
		static double health = 75;
		static NativeType type;
		type.Name = "Entity";
		type.Getters["health"] = [](VM&, void* data) { return Value::FromNumber(*static_cast<double*>(data)); };
		debug.Machine.SetSharedGlobal("enemy", debug.Machine.MakeNative(type, &health));
		debug.Machine.RegisterFunction("heal_all", 0, [](CallContext&) { health = 100; return Value(); });
		debug.Machine.SetBreakpoints("game.forge", { 3 });
		debug.Load(source);
		REQUIRE_EQ(debug.StoppedLine(), 3);
		DebugVariable value;
		std::string error;
		REQUIRE(debug.Machine.Evaluate(0, "enemy.health - hp", value, error));
		CHECK_EQ(value.Text, std::string("25"));
		CHECK_FALSE(debug.Machine.Evaluate(0, "heal_all()", value, error));
		CHECK_EQ(health, 75.0);
		debug.Machine.StopDebugging();
	}

	SUBCASE("the plain form still clears conditions")
	{
		Debugged debug;
		debug.Machine.SetDetailedBreakpoints("game.forge", { { 3, "hp > 1000", "" } });
		debug.Machine.SetBreakpoints("game.forge", { 3 });
		debug.Load(source);
		CHECK_EQ(debug.StoppedLine(), 3);
		CHECK_EQ(debug.Printed.size(), static_cast<size_t>(0));
		debug.Machine.StopDebugging();
	}
}
