#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Profiler.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	// The samples of one frame, by name, for checking what ran and how it nested.
	const ProfileSample* FindSample(const ProfileFrame& frame, const std::string& name)
	{
		auto it = std::find_if(frame.Samples.begin(), frame.Samples.end(),
			[&name](const ProfileSample& sample) { return name == sample.Name; });
		return it == frame.Samples.end() ? nullptr : &*it;
	}

	size_t CountSamples(const ProfileFrame& frame, const std::string& name)
	{
		return static_cast<size_t>(std::count_if(frame.Samples.begin(), frame.Samples.end(),
			[&name](const ProfileSample& sample) { return name == sample.Name; }));
	}

}

TEST_CASE("ForgeScript tasks show in the profiler by script, and by what each task began with")
{
	REQUIRE(ScriptEngine::IsInitialized());
	Profiler::SetPaused(false);
	Profiler::Clear();

	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "ForgeScriptProfiler";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Guard.forge",
		"set steps to 0\n"
		"function patrol()\n"
		"\twait 1 frame\n"
		"\tadd 1 to steps\n"
		"end\n"
		"when update with delta\n"
		"\tadd 1 to steps\n"
		"end\n"
		"start patrol()\n"));
	AssetManager::SetAssetDirectory(assets);

	Ref<Scene> play = CreateRef<Scene>();
	play->CreateEntity("Guard").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Guard.forge";
	// Frame 0 starts play, which runs the script's top level; frames 1 to 3 update the scene.
	for (uint64_t frame = 0; frame <= 3; frame++)
	{
		Profiler::BeginFrame(frame);
		if (frame == 0)
		{
			play->OnRuntimeStart();
			ScriptEngine::SetContextScene(play.get());
		}
		else
		{
			play->OnUpdateRuntime(1.0f / 60.0f);
		}
		Profiler::EndFrame();
	}
	play->OnRuntimeStop();

	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == 4);

	// Every run is inside its script's scope, one level up.
	for (const ProfileFrame& frame : history)
	{
		for (size_t i = 0; i < frame.Samples.size(); i++)
		{
			const ProfileSample& sample = frame.Samples[i];
			if (std::string(sample.Name).rfind("Scripts/Guard.forge: ", 0) != 0)
				continue;
			REQUIRE(i > 0);
			CHECK(std::string(frame.Samples[i - 1].Name) == "Scripts/Guard.forge");
			CHECK(frame.Samples[i - 1].Depth + 1 == sample.Depth);
		}
	}

	// Frame 0: the top level, and inside it the task it starts, up to that task's wait.
	const ProfileSample* topLevel = FindSample(history[0], "Scripts/Guard.forge: top level");
	const ProfileSample* started = FindSample(history[0], "Scripts/Guard.forge: patrol");
	REQUIRE(topLevel);
	REQUIRE(started);
	CHECK(started->Depth == topLevel->Depth + 2);
	CHECK(started->StartMs >= topLevel->StartMs);
	CHECK(started->StartMs + started->DurationMs <= topLevel->StartMs + topLevel->DurationMs + 1e-6);

	// Frame 1: the update, and patrol carrying on after its wait, each a run of its own, under
	// Scripts, then ForgeScript.
	const ProfileFrame& first = history[1];
	CHECK(CountSamples(first, "Scripts/Guard.forge: when update") == 1);
	CHECK(CountSamples(first, "Scripts/Guard.forge: patrol") == 1);
	CHECK(CountSamples(first, "Scripts/Guard.forge") == 2);
	const ProfileSample* forgeScript = FindSample(first, "ForgeScript");
	const ProfileSample* script = FindSample(first, "Scripts/Guard.forge");
	REQUIRE(forgeScript);
	REQUIRE(script);
	CHECK(script->Depth == forgeScript->Depth + 1);

	// Frames 2 and 3: patrol has finished; only the update runs.
	for (size_t i = 2; i <= 3; i++)
	{
		CHECK(CountSamples(history[i], "Scripts/Guard.forge: when update") == 1);
		CHECK(CountSamples(history[i], "Scripts/Guard.forge: patrol") == 0);
	}

	Profiler::Clear();
	ScriptEngine::ResetErrorCount();
}
