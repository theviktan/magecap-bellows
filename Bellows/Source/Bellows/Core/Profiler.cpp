#include "bwpch.h"
#include "Bellows/Core/Profiler.h"

#include <thread>

namespace Bellows {

	namespace {

		using Clock = std::chrono::steady_clock;

		struct ProfilerState
		{
			// The thread recording the current frame, or no thread between frames. Atomic, because a
			// scope on any thread checks it.
			std::atomic<std::thread::id> FrameThread;
			bool Paused = false;
			Clock::time_point FrameStart;
			ProfileFrame Current;
			std::vector<size_t> Open; // indices into Current.Samples of the scopes not yet ended
			std::deque<ProfileFrame> History;

			std::mutex NamesMutex;
			std::unordered_set<std::string> Names;
		};

		ProfilerState& State()
		{
			static ProfilerState state;
			return state;
		}

		double MillisecondsSince(Clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
		}

		bool IsRecording(const ProfilerState& state)
		{
			return state.FrameThread.load(std::memory_order_relaxed) == std::this_thread::get_id();
		}

		// Per-name totals of one list of samples in each frame (the CPU's or the GPU's).
		std::vector<ProfileSummary> SummarizeSamples(const std::vector<const std::vector<ProfileSample>*>& frames)
		{
			std::vector<ProfileSummary> summaries;
			if (frames.empty())
				return summaries;

			std::unordered_map<std::string_view, size_t> indexByName;
			std::vector<double> frameTotals;
			for (const std::vector<ProfileSample>* samples : frames)
			{
				std::fill(frameTotals.begin(), frameTotals.end(), 0.0);
				// The scopes enclosing the current sample, to find self time and recursion.
				std::vector<const ProfileSample*> parents;
				for (const ProfileSample& sample : *samples)
				{
					while (!parents.empty() && parents.back()->Depth >= sample.Depth)
						parents.pop_back();

					const std::string_view name = sample.Name;
					auto [it, inserted] = indexByName.try_emplace(name, summaries.size());
					if (inserted)
					{
						ProfileSummary summary;
						summary.Name = std::string(name);
						summary.Depth = sample.Depth;
						summaries.push_back(std::move(summary));
						frameTotals.push_back(0.0);
					}
					ProfileSummary& summary = summaries[it->second];
					summary.Depth = std::min(summary.Depth, sample.Depth);
					summary.CallsPerFrame += 1.0;
					summary.AverageSelfMs += sample.DurationMs;
					if (!parents.empty())
						summaries[indexByName[parents.back()->Name]].AverageSelfMs -= sample.DurationMs;

					// A name inside itself (a recursive call) is already counted by the outer one.
					const bool nested = std::any_of(parents.begin(), parents.end(),
						[name](const ProfileSample* parent) { return std::string_view(parent->Name) == name; });
					if (!nested)
						frameTotals[it->second] += sample.DurationMs;

					parents.push_back(&sample);
				}
				for (size_t i = 0; i < summaries.size(); i++)
				{
					summaries[i].AverageMs += frameTotals[i];
					summaries[i].MaxMs = std::max(summaries[i].MaxMs, frameTotals[i]);
				}
			}

			const double frameCount = static_cast<double>(frames.size());
			for (ProfileSummary& summary : summaries)
			{
				summary.AverageMs /= frameCount;
				summary.AverageSelfMs = std::max(0.0, summary.AverageSelfMs / frameCount);
				summary.CallsPerFrame /= frameCount;
			}
			return summaries;
		}

	}

	void Profiler::BeginFrame(uint64_t index)
	{
		// A frame begun inside another (editor automation runs frames from inside one) replaces it:
		// the outer frame is unfinished and shares the new frame's index, so it isn't kept.
		ProfilerState& state = State();
		state.FrameThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
		state.FrameStart = Clock::now();
		state.Current.Index = index;
		state.Current.CpuMs = 0.0;
		state.Current.Samples.clear();
		state.Current.GpuTimed = false;
		state.Current.GpuMs = 0.0;
		state.Current.GpuSamples.clear();
		state.Open.clear();
	}

	void Profiler::EndFrame()
	{
		ProfilerState& state = State();
		if (!IsRecording(state))
			return;

		const double now = MillisecondsSince(state.FrameStart);
		// A scope still open (an exception unwound past its end, or a scope begun and never ended)
		// ends with the frame, so every sample has a duration.
		for (size_t index : state.Open)
		{
			ProfileSample& sample = state.Current.Samples[index];
			sample.DurationMs = now - sample.StartMs;
		}
		state.Open.clear();
		state.Current.CpuMs = now;
		state.FrameThread.store(std::thread::id(), std::memory_order_relaxed);

		if (state.Paused)
			return;

		const size_t sampleCount = state.Current.Samples.size();
		state.History.push_back(std::move(state.Current));
		while (state.History.size() > c_HistorySize)
			state.History.pop_front();
		state.Current = ProfileFrame();
		state.Current.Samples.reserve(sampleCount);
	}

	std::optional<uint64_t> Profiler::GetRecordingFrame()
	{
		const ProfilerState& state = State();
		if (!IsRecording(state))
			return std::nullopt;
		return state.Current.Index;
	}

	void Profiler::AddGpuSamples(uint64_t frameIndex, const std::vector<ProfileSample>& samples)
	{
		// Newest first: GPU times arrive a few frames after theirs.
		std::deque<ProfileFrame>& history = State().History;
		for (auto it = history.rbegin(); it != history.rend(); ++it)
		{
			if (it->Index != frameIndex)
				continue;
			it->GpuTimed = true;
			for (const ProfileSample& sample : samples)
			{
				it->GpuSamples.push_back(sample);
				if (sample.Depth == 0)
					it->GpuMs += sample.DurationMs;
			}
			return;
		}
	}

	void Profiler::BeginScope(const char* name)
	{
		ProfilerState& state = State();
		if (!IsRecording(state))
			return;

		ProfileSample sample;
		sample.Name = name ? name : "";
		sample.Depth = static_cast<uint32_t>(state.Open.size());
		sample.StartMs = MillisecondsSince(state.FrameStart);
		state.Open.push_back(state.Current.Samples.size());
		state.Current.Samples.push_back(sample);
	}

	void Profiler::EndScope()
	{
		ProfilerState& state = State();
		if (!IsRecording(state) || state.Open.empty())
			return;

		ProfileSample& sample = state.Current.Samples[state.Open.back()];
		sample.DurationMs = MillisecondsSince(state.FrameStart) - sample.StartMs;
		state.Open.pop_back();
	}

	const char* Profiler::Intern(std::string_view name)
	{
		ProfilerState& state = State();
		std::scoped_lock lock(state.NamesMutex);
		// Elements of an unordered_set never move, so the pointer stays valid as the set grows.
		return state.Names.emplace(name).first->c_str();
	}

	void Profiler::SetPaused(bool paused)
	{
		State().Paused = paused;
	}

	bool Profiler::IsPaused()
	{
		return State().Paused;
	}

	const std::deque<ProfileFrame>& Profiler::GetHistory()
	{
		return State().History;
	}

	std::vector<ProfileFrame> Profiler::GetRecentFrames(size_t count)
	{
		const std::deque<ProfileFrame>& history = State().History;
		const size_t first = history.size() > count ? history.size() - count : 0;
		return std::vector<ProfileFrame>(history.begin() + static_cast<std::ptrdiff_t>(first), history.end());
	}

	void Profiler::Clear()
	{
		State().History.clear();
	}

	std::vector<ProfileSummary> Profiler::Summarize(size_t frameCount)
	{
		return Summarize(GetRecentFrames(frameCount));
	}

	std::vector<ProfileSummary> Profiler::Summarize(const std::vector<ProfileFrame>& frames)
	{
		std::vector<const std::vector<ProfileSample>*> samples;
		samples.reserve(frames.size());
		for (const ProfileFrame& frame : frames)
			samples.push_back(&frame.Samples);
		return SummarizeSamples(samples);
	}

	std::vector<ProfileSummary> Profiler::SummarizeGpu(const std::vector<ProfileFrame>& frames)
	{
		std::vector<const std::vector<ProfileSample>*> samples;
		for (const ProfileFrame& frame : frames)
		{
			if (frame.GpuTimed)
				samples.push_back(&frame.GpuSamples);
		}
		return SummarizeSamples(samples);
	}

}
