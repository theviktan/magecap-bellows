#include <doctest/doctest.h>

#include "Bellows/Core/Profiler.h"

#include <chrono>
#include <string>
#include <thread>

using namespace Bellows;

namespace {

	void Sleep(int milliseconds)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	// The profiler is global and other tests run frames through it: start each test clean.
	struct ProfilerReset
	{
		ProfilerReset()
		{
			Profiler::SetPaused(false);
			Profiler::Clear();
		}
		~ProfilerReset()
		{
			Profiler::SetPaused(false);
			Profiler::Clear();
		}
	};

	const ProfileSummary* FindSummary(const std::vector<ProfileSummary>& summaries, const std::string& name)
	{
		for (const ProfileSummary& summary : summaries)
		{
			if (summary.Name == name)
				return &summary;
		}
		return nullptr;
	}

}

TEST_CASE("The profiler records nested scopes with their depth and times")
{
	ProfilerReset reset;
	Profiler::BeginFrame(7);
	{
		BW_PROFILE_SCOPE("Update");
		Sleep(2);
		{
			BW_PROFILE_SCOPE("Physics");
			Sleep(3);
		}
	}
	{
		BW_PROFILE_SCOPE("Render");
	}
	Profiler::EndFrame();

	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == 1);
	const ProfileFrame& frame = history.back();
	CHECK(frame.Index == 7);
	REQUIRE(frame.Samples.size() == 3);

	const ProfileSample& update = frame.Samples[0];
	const ProfileSample& physics = frame.Samples[1];
	const ProfileSample& render = frame.Samples[2];
	CHECK(std::string(update.Name) == "Update");
	CHECK(update.Depth == 0);
	CHECK(std::string(physics.Name) == "Physics");
	CHECK(physics.Depth == 1);
	CHECK(render.Depth == 0);

	CHECK(physics.DurationMs >= 3.0);
	CHECK(update.DurationMs >= physics.DurationMs + 2.0);
	CHECK(physics.StartMs >= update.StartMs);
	CHECK(physics.StartMs + physics.DurationMs <= update.StartMs + update.DurationMs);
	CHECK(render.StartMs >= update.StartMs + update.DurationMs);
	CHECK(frame.CpuMs >= render.StartMs + render.DurationMs);
}

TEST_CASE("The profiler ignores scopes outside a frame, on other threads, and extra ends")
{
	ProfilerReset reset;
	{
		BW_PROFILE_SCOPE("Before the frame");
	}
	Profiler::EndScope();

	Profiler::BeginFrame(1);
	std::thread worker([]
	{
		BW_PROFILE_SCOPE("Worker");
	});
	worker.join();
	Profiler::EndScope(); // nothing open
	{
		BW_PROFILE_SCOPE("Main");
	}
	Profiler::EndFrame();
	Profiler::EndFrame(); // a second end with no frame does nothing

	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == 1);
	REQUIRE(history.back().Samples.size() == 1);
	CHECK(std::string(history.back().Samples[0].Name) == "Main");
}

TEST_CASE("A scope still open when the frame ends is closed with the frame")
{
	ProfilerReset reset;
	Profiler::BeginFrame(1);
	Profiler::BeginScope("Never ended");
	Sleep(1);
	Profiler::EndFrame();

	const ProfileFrame& frame = Profiler::GetHistory().back();
	REQUIRE(frame.Samples.size() == 1);
	CHECK(frame.Samples[0].DurationMs > 0.0);
	CHECK(frame.Samples[0].DurationMs <= frame.CpuMs);

	// The next frame starts with nothing open.
	Profiler::BeginFrame(2);
	Profiler::BeginScope("Top");
	Profiler::EndScope();
	Profiler::EndFrame();
	CHECK(Profiler::GetHistory().back().Samples[0].Depth == 0);
}

TEST_CASE("A frame begun inside another replaces it")
{
	// Editor automation runs frames from inside a frame: the unfinished outer one isn't kept.
	ProfilerReset reset;
	Profiler::BeginFrame(4);
	Profiler::BeginScope("Events");
	Profiler::BeginFrame(4);
	{
		BW_PROFILE_SCOPE("Inner");
	}
	Profiler::EndFrame();
	// The rest of the outer frame is ignored.
	Profiler::EndScope();
	{
		BW_PROFILE_SCOPE("After");
	}
	Profiler::EndFrame();

	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == 1);
	CHECK(history[0].Index == 4);
	REQUIRE(history[0].Samples.size() == 1);
	CHECK(std::string(history[0].Samples[0].Name) == "Inner");
	CHECK(history[0].Samples[0].Depth == 0);
}

TEST_CASE("The profiler keeps only the newest frames, and none while paused")
{
	ProfilerReset reset;
	for (uint64_t i = 0; i < Profiler::c_HistorySize + 25; i++)
	{
		Profiler::BeginFrame(i);
		Profiler::EndFrame();
	}
	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == Profiler::c_HistorySize);
	CHECK(history.front().Index == 25);
	CHECK(history.back().Index == Profiler::c_HistorySize + 24);

	Profiler::SetPaused(true);
	CHECK(Profiler::IsPaused());
	Profiler::BeginFrame(1000);
	Profiler::EndFrame();
	CHECK(history.back().Index == Profiler::c_HistorySize + 24);

	Profiler::SetPaused(false);
	Profiler::BeginFrame(1001);
	Profiler::EndFrame();
	CHECK(history.back().Index == 1001);

	const std::vector<ProfileFrame> recent = Profiler::GetRecentFrames(3);
	REQUIRE(recent.size() == 3);
	CHECK(recent.back().Index == 1001);
	CHECK(Profiler::GetRecentFrames(100000).size() == Profiler::c_HistorySize);

	Profiler::Clear();
	CHECK(Profiler::GetHistory().empty());
	CHECK(Profiler::GetRecentFrames(5).empty());
	CHECK(Profiler::Summarize(5).empty());
}

TEST_CASE("Interned names are stable and shared")
{
	std::string name = "Script: player.forge";
	const char* first = Profiler::Intern(name);
	name += " (changed)";
	CHECK(std::string(first) == "Script: player.forge");
	CHECK(Profiler::Intern("Script: player.forge") == first);
	CHECK(Profiler::Intern(name) != first);
}

TEST_CASE("Summaries average totals, self time and calls per frame")
{
	// Built by hand, so the numbers are exact.
	auto sample = [](const char* name, uint32_t depth, double start, double duration)
	{
		ProfileSample result;
		result.Name = name;
		result.Depth = depth;
		result.StartMs = start;
		result.DurationMs = duration;
		return result;
	};

	ProfileFrame first;
	first.CpuMs = 10.0;
	first.Samples = {
		sample("Update", 0, 0.0, 6.0),
		sample("Scripts", 1, 0.0, 2.0),
		sample("Scripts", 1, 2.0, 1.0),
		sample("Physics", 1, 3.0, 2.0),
		sample("Render", 0, 6.0, 4.0),
	};
	ProfileFrame second;
	second.CpuMs = 4.0;
	second.Samples = {
		sample("Update", 0, 0.0, 4.0),
		// A recursive scope: the inner one is part of the outer one's total.
		sample("Physics", 1, 0.0, 3.0),
		sample("Physics", 2, 1.0, 1.0),
	};

	const std::vector<ProfileSummary> summaries = Profiler::Summarize(std::vector<ProfileFrame> { first, second });
	REQUIRE(summaries.size() == 4);
	CHECK(summaries[0].Name == "Update");
	CHECK(summaries[1].Name == "Scripts");
	CHECK(summaries[2].Name == "Physics");
	CHECK(summaries[3].Name == "Render");

	const ProfileSummary* update = FindSummary(summaries, "Update");
	CHECK(update->Depth == 0);
	CHECK(update->AverageMs == doctest::Approx(5.0));
	CHECK(update->MaxMs == doctest::Approx(6.0));
	// Frame 1: 6 - (2 + 1 + 2) = 1; frame 2: 4 - 3 = 1.
	CHECK(update->AverageSelfMs == doctest::Approx(1.0));
	CHECK(update->CallsPerFrame == doctest::Approx(1.0));

	const ProfileSummary* scripts = FindSummary(summaries, "Scripts");
	CHECK(scripts->AverageMs == doctest::Approx(1.5)); // 3 in frame 1, none in frame 2
	CHECK(scripts->MaxMs == doctest::Approx(3.0));
	CHECK(scripts->CallsPerFrame == doctest::Approx(1.0));

	const ProfileSummary* physics = FindSummary(summaries, "Physics");
	CHECK(physics->Depth == 1);
	CHECK(physics->AverageMs == doctest::Approx(2.5)); // 2 and 3, not 2 and 4
	CHECK(physics->MaxMs == doctest::Approx(3.0));
	CHECK(physics->AverageSelfMs == doctest::Approx(2.5)); // 2, then (3 - 1) + 1
	CHECK(physics->CallsPerFrame == doctest::Approx(1.5));

	const ProfileSummary* render = FindSummary(summaries, "Render");
	CHECK(render->AverageMs == doctest::Approx(2.0));
	CHECK(render->AverageSelfMs == doctest::Approx(2.0));
}

TEST_CASE("GPU samples join the frame that recorded them")
{
	ProfilerReset reset;
	CHECK_FALSE(Profiler::GetRecordingFrame().has_value());
	for (uint64_t i = 10; i < 13; i++)
	{
		Profiler::BeginFrame(i);
		CHECK(Profiler::GetRecordingFrame() == i);
		std::thread worker([] { CHECK_FALSE(Profiler::GetRecordingFrame().has_value()); });
		worker.join();
		Profiler::EndFrame();
	}
	CHECK_FALSE(Profiler::GetRecordingFrame().has_value());

	auto sample = [](const char* name, uint32_t depth, double duration)
	{
		ProfileSample result;
		result.Name = name;
		result.Depth = depth;
		result.DurationMs = duration;
		return result;
	};
	// Two renderers report for frame 11, a few frames late; frame 99 isn't in the history.
	Profiler::AddGpuSamples(11, { sample("Viewport", 0, 4.0), sample("Shadows", 1, 1.5), sample("Opaque", 1, 2.0) });
	Profiler::AddGpuSamples(11, { sample("Editor UI", 0, 1.0) });
	Profiler::AddGpuSamples(99, { sample("Viewport", 0, 9.0) });

	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == 3);
	CHECK_FALSE(history[0].GpuTimed);
	CHECK(history[1].GpuTimed);
	CHECK(history[1].GpuMs == doctest::Approx(5.0)); // the depth-0 samples: 4 + 1
	CHECK(history[1].GpuSamples.size() == 4);
	CHECK_FALSE(history[2].GpuTimed);

	// Only frames with GPU times count in the GPU summary.
	const std::vector<ProfileSummary> gpu = Profiler::SummarizeGpu(Profiler::GetRecentFrames(3));
	REQUIRE(gpu.size() == 4);
	CHECK(gpu[0].Name == "Viewport");
	CHECK(gpu[0].AverageMs == doctest::Approx(4.0));
	CHECK(gpu[0].AverageSelfMs == doctest::Approx(0.5));
	CHECK(gpu[1].Name == "Shadows");
	CHECK(gpu[1].Depth == 1);
	CHECK(gpu[3].Name == "Editor UI");
	CHECK(gpu[3].AverageMs == doctest::Approx(1.0));
	CHECK(Profiler::SummarizeGpu({ history[0] }).empty());

	// A frame begun again starts without GPU times.
	Profiler::SetPaused(true);
	Profiler::BeginFrame(11);
	Profiler::EndFrame();
	Profiler::SetPaused(false);
	Profiler::BeginFrame(13);
	Profiler::EndFrame();
	CHECK_FALSE(Profiler::GetHistory().back().GpuTimed);
	CHECK(Profiler::GetHistory().back().GpuSamples.empty());
}
