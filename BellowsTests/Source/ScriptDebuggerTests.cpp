#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "ScriptDebugger.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptApi.h"

#include <string>
#include <vector>

using namespace Bellows;

namespace {

	constexpr const char* c_Script = "Scripts/Stepper.forge";

	// Line numbers below are the ones the tests stop at.
	constexpr const char* c_StepperSource =
		"set ticks to 0\n"                       // 1
		"function bump(amount)\n"                // 2
		"\tadd amount to ticks\n"                // 3
		"\treturn ticks\n"                       // 4
		"end\n"                                  // 5
		"when update with delta\n"               // 6
		"\tset value to bump(1)\n"               // 7
		"\tset my name to \"ticks {value}\"\n"   // 8
		"end\n"                                  // 9
		"when destroyed\n"                       // 10
		"\tset my name to \"destroyed\"\n"       // 11
		"end\n";                                 // 12

	struct DebugSession
	{
		Ref<Scene> GameScene = CreateRef<Scene>();
		std::vector<std::string> Errors;
		Scripting::ForgeScriptRuntime Runtime { [this](const std::string&, const std::string& message) { Errors.push_back(message); } };
		Entity Stepper;

		DebugSession()
		{
			const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "ScriptDebugger";
			AssetManager::SetAssetDirectory(folder);
			REQUIRE(FileSystem::WriteTextFile(folder / c_Script, c_StepperSource));
			Scripting::SetContextScene(GameScene.get());
			Stepper = GameScene->CreateEntity("Stepper");
			Stepper.AddComponent<ScriptComponent>().ScriptPath = c_Script;
		}

		~DebugSession()
		{
			Runtime.Stop();
			Scripting::SetContextScene(nullptr);
			AssetManager::SetAssetDirectory({});
		}

		void Start()
		{
			Runtime.Start();
			REQUIRE(Runtime.CreateInstance(Stepper));
			Runtime.CallCreated(Stepper.GetUUID());
		}

		void Update() { Runtime.Update(*GameScene, 1.0f / 60.0f); }
	};

	bool HasVariable(const std::vector<ForgeScript::DebugVariable>& variables, const std::string& name, const std::string& text)
	{
		for (const ForgeScript::DebugVariable& variable : variables)
		{
			if (variable.Name == name)
				return variable.Text == text;
		}
		return false;
	}

}

TEST_CASE("ScriptDebugger keeps sorted breakpoints per script")
{
	ScriptDebugger debugger;
	CHECK_EQ(debugger.GetBreakpointCount(), static_cast<size_t>(0));
	const uint64_t start = debugger.GetBreakpointRevision();

	CHECK(debugger.ToggleBreakpoint(c_Script, 8));
	CHECK(debugger.ToggleBreakpoint(c_Script, 3));
	CHECK(debugger.HasBreakpoint(c_Script, 3));
	CHECK_EQ(debugger.GetBreakpoints(c_Script), std::vector<int> { 3, 8 });
	CHECK_FALSE(debugger.ToggleBreakpoint(c_Script, 8));
	CHECK_FALSE(debugger.HasBreakpoint(c_Script, 8));
	CHECK_GT(debugger.GetBreakpointRevision(), start);

	// Bad lines and repeats are dropped; setting the same lines again changes nothing.
	debugger.SetBreakpoints(c_Script, { 9, 0, -2, 4, 9 });
	CHECK_EQ(debugger.GetBreakpoints(c_Script), std::vector<int> { 4, 9 });
	const uint64_t revision = debugger.GetBreakpointRevision();
	debugger.SetBreakpoints(c_Script, { 4, 9 });
	CHECK_EQ(debugger.GetBreakpointRevision(), revision);

	// Scripts outside the project (no asset path) and lines before the first are refused.
	CHECK_FALSE(debugger.ToggleBreakpoint("", 3));
	CHECK_FALSE(debugger.ToggleBreakpoint(c_Script, 0));

	debugger.SetBreakpoints("Scripts/Other.forge", { 2 });
	CHECK_EQ(debugger.GetBreakpointCount(), static_cast<size_t>(3));
	debugger.SetBreakpoints(c_Script, {});
	CHECK(debugger.GetBreakpoints(c_Script).empty());
	CHECK_EQ(debugger.GetAllBreakpoints().size(), static_cast<size_t>(1));
	debugger.ClearBreakpoints();
	CHECK_EQ(debugger.GetBreakpointCount(), static_cast<size_t>(0));

	// Without a play session there is nothing to go on with.
	CHECK_FALSE(debugger.IsPlaying());
	CHECK_FALSE(debugger.IsStopped());
	CHECK_FALSE(debugger.Continue());
	CHECK_FALSE(debugger.StepOver());
	CHECK_FALSE(debugger.Pause());
	CHECK(debugger.GetCallStack().empty());
	CHECK(debugger.GetVariables(0).empty());
	CHECK(debugger.GetScriptVariables(0).empty());
}

TEST_CASE("ScriptDebugger stops at breakpoints, steps and shows variables")
{
	DebugSession session;
	ScriptDebugger debugger;
	std::vector<ForgeScript::DebugStop> stops;
	debugger.SetStopCallback([&stops](const ForgeScript::DebugStop& stop) { stops.push_back(stop); });
	// Breakpoints set before connecting (and before play) reach the game.
	debugger.ToggleBreakpoint(c_Script, 7);
	debugger.Connect(&session.Runtime);
	session.Start();
	CHECK(debugger.IsPlaying());

	session.Update();
	REQUIRE(debugger.IsStopped());
	REQUIRE_EQ(stops.size(), static_cast<size_t>(1));
	CHECK_EQ(debugger.GetStopCount(), static_cast<uint64_t>(1));
	CHECK_EQ(debugger.GetStop().File, std::string(c_Script));
	CHECK_EQ(debugger.GetStop().Line, 7);
	CHECK(debugger.GetStop().Reason == ForgeScript::DebugStopReason::Breakpoint);
	CHECK(HasVariable(debugger.GetScriptVariables(0), "ticks", "0"));
	CHECK_FALSE(debugger.Pause());

	// Step in: the next line is inside bump, one call deeper.
	REQUIRE(debugger.StepIn());
	REQUIRE(debugger.IsStopped());
	CHECK_EQ(debugger.GetStop().Line, 3);
	CHECK(debugger.GetStop().Reason == ForgeScript::DebugStopReason::Step);
	std::vector<ForgeScript::DebugFrame> stack = debugger.GetCallStack();
	REQUIRE_EQ(stack.size(), static_cast<size_t>(2));
	CHECK_EQ(stack[0].Line, 3);
	CHECK_EQ(stack[1].Line, 7);
	CHECK(HasVariable(debugger.GetVariables(0), "amount", "1"));

	// Step over stays in bump; step out goes back to the handler, past the call.
	REQUIRE(debugger.StepOver());
	CHECK_EQ(debugger.GetStop().Line, 4);
	CHECK(HasVariable(debugger.GetScriptVariables(0), "ticks", "1"));
	REQUIRE(debugger.StepOut());
	REQUIRE(debugger.IsStopped());
	CHECK_EQ(debugger.GetStop().Line, 8);
	CHECK_EQ(debugger.GetCallStack().size(), static_cast<size_t>(1));

	// Continue runs to the end of the update; the next update stops at the breakpoint again.
	const uint64_t stopsBefore = debugger.GetStopCount();
	REQUIRE(debugger.Continue());
	CHECK_FALSE(debugger.IsStopped());
	CHECK_EQ(session.Stepper.GetName(), std::string("ticks 1"));
	session.Update();
	CHECK(debugger.IsStopped());
	CHECK_EQ(debugger.GetStopCount(), stopsBefore + 1);

	// Removing the breakpoint while stopped lets the game run freely once it goes on.
	debugger.ToggleBreakpoint(c_Script, 7);
	REQUIRE(debugger.Continue());
	session.Update();
	CHECK_FALSE(debugger.IsStopped());
	CHECK_EQ(session.Stepper.GetName(), std::string("ticks 3"));

	// Pause stops at the next line that runs.
	REQUIRE(debugger.Pause());
	session.Update();
	REQUIRE(debugger.IsStopped());
	CHECK(debugger.GetStop().Reason == ForgeScript::DebugStopReason::Pause);
	CHECK_EQ(debugger.GetStop().Line, 7);
	CHECK(debugger.Continue());
	CHECK(session.Errors.empty());
}

TEST_CASE("ScriptDebugger keeps its breakpoints across play sessions")
{
	DebugSession session;
	ScriptDebugger debugger;
	debugger.Connect(&session.Runtime);
	debugger.SetBreakpoints(c_Script, { 7, 11 });
	session.Start();
	session.Update();
	REQUIRE(debugger.IsStopped());
	const uint64_t stops = debugger.GetStopCount();

	// Play stops while the script is held: the held code does not go on, and 'when destroyed' runs
	// without stopping at its breakpoint (line 11).
	session.Runtime.Stop();
	CHECK_FALSE(debugger.IsStopped());
	CHECK_FALSE(debugger.IsPlaying());
	CHECK_EQ(debugger.GetStopCount(), stops);
	CHECK_EQ(session.Stepper.GetName(), std::string("destroyed"));

	// The breakpoints are still there for the next play session.
	CHECK_EQ(debugger.GetBreakpoints(c_Script), std::vector<int> { 7, 11 });
	session.Start();
	session.Update();
	CHECK(debugger.IsStopped());
	CHECK_EQ(debugger.GetStop().Line, 7);

	// Disconnected, the runtime no longer stops.
	session.Runtime.Stop();
	debugger.Connect(nullptr);
	session.Start();
	session.Update();
	CHECK_FALSE(session.Runtime.IsStopped());
	CHECK(session.Errors.empty());
}
