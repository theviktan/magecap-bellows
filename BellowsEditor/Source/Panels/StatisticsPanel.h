#pragma once

#include <array>
#include <cstddef>

namespace Bellows {

	class EditorLayer;

	// Frame timing graph and renderer, scene and scripting counters.
	class StatisticsPanel
	{
	public:
		explicit StatisticsPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		void OnImGuiRender(bool* open);
	private:
		EditorLayer& m_Editor;
		std::array<float, 240> m_FrameTimes {};
		size_t m_FrameTimeOffset = 0;
	};

}
