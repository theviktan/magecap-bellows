#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace Bellows {

	struct ProfileFrame;

	// The profiler's view: a graph of recent CPU and GPU frame times, the time each part of the frame
	// (or each render pass, on the GPU) took on average, and the scopes of one frame picked from the graph.
	class ProfilerPanel
	{
	public:
		static constexpr const char* WindowName = "Profiler";

		void OnImGuiRender(bool* open);
	private:
		void DrawToolbar();
		void DrawFrameGraph(const std::deque<ProfileFrame>& history);
		void DrawAverages();
		void DrawSelectedFrame(const ProfileFrame& frame);
		const ProfileFrame& FindSelectedFrame(const std::deque<ProfileFrame>& history) const;
	private:
		std::optional<uint64_t> m_SelectedFrame; // a frame index; none follows the newest frame
		int m_AverageChoice = 2; // an index into the panel's frame counts
		bool m_ShowGpu = false; // the tabs show GPU times instead of CPU times
	};

}
