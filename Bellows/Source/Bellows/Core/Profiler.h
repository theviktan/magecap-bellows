#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	// One timed scope in a frame. Times are milliseconds from the start of the frame.
	struct ProfileSample
	{
		const char* Name = "";
		uint32_t Depth = 0; // 0 for a scope directly inside the frame
		double StartMs = 0.0;
		double DurationMs = 0.0;
	};

	// One finished frame: its CPU time and its scopes in the order they began.
	struct ProfileFrame
	{
		uint64_t Index = 0;
		double CpuMs = 0.0;
		std::vector<ProfileSample> Samples;
	};

	// One scope name over several frames: inclusive time (with the scopes inside it), self time
	// (without them) and how often it ran, averaged per frame.
	struct ProfileSummary
	{
		std::string Name;
		uint32_t Depth = 0; // the shallowest depth the name was seen at
		double AverageMs = 0.0;
		double MaxMs = 0.0; // the largest per-frame total
		double AverageSelfMs = 0.0;
		double CallsPerFrame = 0.0;
	};

	// CPU frame profiler. Scopes are recorded on the thread that began the frame (the main thread)
	// between BeginFrame and EndFrame; scopes on other threads, or outside a frame, are ignored.
	// The last c_HistorySize frames are kept. A frame begun before the last one ended replaces it.
	class Profiler
	{
	public:
		static constexpr size_t c_HistorySize = 300;

		static void BeginFrame(uint64_t index);
		static void EndFrame();

		// The name must outlive the profiler: a string literal, or a name from Intern.
		static void BeginScope(const char* name);
		static void EndScope();

		// A stable copy of a name built at run time (a script's file and handler). Each distinct
		// name is stored once for the life of the program.
		static const char* Intern(std::string_view name);

		// While paused, frames are still timed but not added to the history.
		static void SetPaused(bool paused);
		static bool IsPaused();

		// Oldest first.
		static const std::deque<ProfileFrame>& GetHistory();
		// The newest frames, at most count of them, newest last.
		static std::vector<ProfileFrame> GetRecentFrames(size_t count);
		static void Clear();

		// Per-name totals over the newest frames (at most frameCount), in first-seen order.
		static std::vector<ProfileSummary> Summarize(size_t frameCount);
		static std::vector<ProfileSummary> Summarize(const std::vector<ProfileFrame>& frames);
	};

	// Times the enclosing block.
	class ProfileScope
	{
	public:
		explicit ProfileScope(const char* name) { Profiler::BeginScope(name); }
		~ProfileScope() { Profiler::EndScope(); }

		ProfileScope(const ProfileScope&) = delete;
		ProfileScope& operator=(const ProfileScope&) = delete;
	};

}

#define BW_PROFILE_CONCAT_INNER(a, b) a##b
#define BW_PROFILE_CONCAT(a, b) BW_PROFILE_CONCAT_INNER(a, b)
#define BW_PROFILE_SCOPE(name) ::Bellows::ProfileScope BW_PROFILE_CONCAT(bwProfileScope, __LINE__)(name)
