#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Bellows {

	// Times a renderer's GPU work, pass by pass, for the profiler. Begin and End wrap one command list's
	// recording in a scope with the timer's name; BeginScope and EndScope time the passes inside it.
	// The GPU finishes a recording frames after the CPU, so each recording's times are read when its
	// slot comes round again (c_Latency recordings later, past the frames in flight) and only once the
	// GPU has written them; then they go to the frame that recorded it (Profiler::AddGpuSamples).
	// Nothing is recorded outside a profiled frame, or where the driver can't write timestamps.
	class GpuTimer
	{
	public:
		static constexpr size_t c_Latency = 4;

		// The name must outlive the timer (a string literal).
		explicit GpuTimer(const char* name);
		~GpuTimer();

		GpuTimer(const GpuTimer&) = delete;
		GpuTimer& operator=(const GpuTimer&) = delete;

		// Whether the GPU can be timed at all (the device writes timestamps).
		static bool IsSupported();

		void SetEnabled(bool enabled) { m_Enabled = enabled; }
		bool IsEnabled() const { return m_Enabled; }

		void Begin(nvrhi::ICommandList* commandList);
		// The name must outlive the timer (a string literal).
		void BeginScope(nvrhi::ICommandList* commandList, const char* name);
		void EndScope(nvrhi::ICommandList* commandList);
		// Ends every scope still open. The command list must be executed after it.
		void End(nvrhi::ICommandList* commandList);
	private:
		struct Timing
		{
			const char* Name = "";
			uint32_t Depth = 0;
			nvrhi::TimerQueryHandle Query;
		};
		struct Recording
		{
			std::optional<uint64_t> Frame; // the profiled frame that recorded it
			std::vector<Timing> Timings; // queries are kept and reused; Used of them were begun
			size_t Used = 0;
		};

		void Collect(Recording& recording);
	private:
		const char* m_Name;
		bool m_Enabled = true;
		bool m_Recording = false;
		std::array<Recording, c_Latency> m_Recordings;
		size_t m_Current = 0;
		std::vector<size_t> m_Open; // indices into the current recording's Timings; SIZE_MAX for an untimed scope
	};

}
