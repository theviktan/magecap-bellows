#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptApi.h"

#include <string>
#include <vector>

using namespace Bellows;

TEST_CASE("ForgeScript debugger stops entity scripts and holds the game until it goes on")
{
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "ForgeDebugger";
	AssetManager::SetAssetDirectory(folder);
	REQUIRE(FileSystem::WriteTextFile(folder / "Scripts/Counter.forge",
		"set ticks to 0\n"                   // 1
		"when update with delta\n"           // 2
		"\tadd 1 to ticks\n"                 // 3 breakpoint
		"\tset my name to \"ticks {ticks}\"\n" // 4
		"end\n"                             // 5
		"when destroyed\n"                  // 6
		"\tset my name to \"destroyed at {ticks}\"\n" // 7
		"end\n"));

	Ref<Scene> scene = CreateRef<Scene>();
	Scripting::SetContextScene(scene.get());
	std::vector<std::string> errors;
	Scripting::ForgeScriptRuntime runtime([&errors](const std::string&, const std::string& message) { errors.push_back(message); });

	std::vector<ForgeScript::DebugStop> stops;
	runtime.SetDebugHandler([&stops](const ForgeScript::DebugStop& stop) { stops.push_back(stop); });
	// Set before play, as the editor's gutter does; the new machine gets them.
	runtime.SetBreakpoints("Scripts/Counter.forge", { 3 });
	CHECK(runtime.GetMachine() == nullptr);
	CHECK_FALSE(runtime.IsStopped());

	Entity counter = scene->CreateEntity("Counter");
	counter.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Counter.forge";
	runtime.Start();
	REQUIRE(runtime.CreateInstance(counter));
	runtime.CallCreated(counter.GetUUID());

	runtime.Update(*scene, 1.0f / 60.0f);
	REQUIRE(runtime.IsStopped());
	REQUIRE_EQ(stops.size(), static_cast<size_t>(1));
	CHECK_EQ(stops[0].File, std::string("Scripts/Counter.forge"));
	CHECK_EQ(stops[0].Line, 3);

	ForgeScript::VM* vm = runtime.GetMachine();
	REQUIRE(vm != nullptr);
	const std::vector<ForgeScript::DebugFrame> stack = vm->GetCallStack();
	REQUIRE_EQ(stack.size(), static_cast<size_t>(1));
	CHECK_EQ(stack[0].Function, std::string("when update"));
	bool sawDelta = false;
	for (const ForgeScript::DebugVariable& variable : vm->GetVariables(0))
		sawDelta = sawDelta || variable.Name == "delta";
	CHECK(sawDelta);
	bool sawSelf = false;
	for (const ForgeScript::DebugVariable& variable : vm->GetGlobals(stack[0].Owner))
	{
		if (variable.Name == "ticks")
			CHECK_EQ(variable.Text, std::string("0"));
		sawSelf = sawSelf || variable.Name == "self";
	}
	CHECK(sawSelf);

	// While stopped, frames run no script: the handler is not started again.
	runtime.Update(*scene, 1.0f / 60.0f);
	runtime.FixedUpdate(*scene, 1.0f / 60.0f);
	CHECK_EQ(stops.size(), static_cast<size_t>(1));

	// Removing the breakpoint during the stop, then going on, lets the game run freely.
	runtime.SetBreakpoints("Scripts/Counter.forge", {});
	CHECK(vm->Continue());
	CHECK_FALSE(runtime.IsStopped());
	CHECK_EQ(counter.GetName(), std::string("ticks 1"));
	runtime.Update(*scene, 1.0f / 60.0f);
	CHECK_EQ(counter.GetName(), std::string("ticks 2"));
	CHECK_EQ(stops.size(), static_cast<size_t>(1));

	// The next play session keeps the breakpoints that are left (none for this script now).
	runtime.Stop();
	runtime.SetBreakpoints("Scripts/Counter.forge", { 4 });
	runtime.Start();
	REQUIRE(runtime.CreateInstance(counter));
	runtime.CallCreated(counter.GetUUID());
	runtime.Update(*scene, 1.0f / 60.0f);
	CHECK(runtime.IsStopped());
	CHECK_EQ(stops.back().Line, 4);
	runtime.ClearBreakpoints();
	runtime.GetMachine()->Continue();
	CHECK_FALSE(runtime.IsStopped());

	// A change to a script while one is held is refused, and the stop stays as it is.
	runtime.SetBreakpoints("Scripts/Counter.forge", { 3, 7 });
	runtime.Update(*scene, 1.0f / 60.0f);
	REQUIRE(runtime.IsStopped());
	const size_t stopsBefore = stops.size();
	CHECK_FALSE(runtime.Reload("Scripts/Counter.forge"));
	REQUIRE_EQ(errors.size(), static_cast<size_t>(1));
	CHECK_EQ(errors[0], std::string("Can't reload 'Scripts/Counter.forge' while a script is stopped at a breakpoint; continue first."));
	CHECK(runtime.IsStopped());
	CHECK(runtime.Reload("Scripts/Other.py"));
	errors.clear();

	// Stopping play while a script is held ends the stop without running the rest of it, and
	// 'when destroyed' (line 7, which has a breakpoint) runs without stopping.
	runtime.Stop();
	CHECK_FALSE(runtime.IsStopped());
	CHECK_EQ(stops.size(), stopsBefore);
	// This session counted one update; the held one never added its tick.
	CHECK_EQ(counter.GetName(), std::string("destroyed at 1"));
	CHECK(errors.empty());

	Scripting::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("ForgeScript debugger holds the rest of the frame when a script stops")
{
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "ForgeDebuggerFrame";
	AssetManager::SetAssetDirectory(folder);
	REQUIRE(FileSystem::WriteTextFile(folder / "Scripts/Ticker.forge",
		"set ticks to 0\n"
		"when update with delta\n"
		"\tadd 1 to ticks\n"
		"\tset my name to \"ticks {ticks}\"\n"
		"end\n"));

	Ref<Scene> scene = CreateRef<Scene>();
	Scripting::SetContextScene(scene.get());
	Scripting::ForgeScriptRuntime runtime([](const std::string&, const std::string&) {});
	runtime.SetDebugHandler([](const ForgeScript::DebugStop&) {});
	std::vector<Entity> tickers;
	for (int i = 0; i < 3; i++)
	{
		Entity ticker = scene->CreateEntity("Ticker");
		ticker.AddComponent<ScriptComponent>().ScriptPath = "Scripts/Ticker.forge";
		tickers.push_back(ticker);
	}
	runtime.Start();
	for (Entity ticker : tickers)
	{
		REQUIRE(runtime.CreateInstance(ticker));
		runtime.CallCreated(ticker.GetUUID());
	}

	// A pause stops whichever script runs its update first; the others, which have no reason to stop,
	// still wait for the next frame instead of running theirs in this one.
	runtime.GetMachine()->Pause();
	runtime.Update(*scene, 1.0f / 60.0f);
	REQUIRE(runtime.IsStopped());
	int ticked = 0;
	for (Entity ticker : tickers)
		ticked += ticker.GetName() != "Ticker";
	CHECK_EQ(ticked, 0);

	runtime.GetMachine()->Continue();
	CHECK_FALSE(runtime.IsStopped());
	runtime.Update(*scene, 1.0f / 60.0f);
	for (Entity ticker : tickers)
		CHECK(ticker.GetName().starts_with("ticks "));
	runtime.Stop();
	Scripting::SetContextScene(nullptr);
	AssetManager::SetAssetDirectory({});
}
