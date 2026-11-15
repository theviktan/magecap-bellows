#pragma once

#include "Bellows/Core/Log.h"

#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace Bellows {

	class EditorLayer;

	// Engine and game log with level filters, plus an interactive Python prompt.
	class ConsolePanel
	{
	public:
		explicit ConsolePanel(EditorLayer& editor)
			: m_Editor(editor) {}

		void OnImGuiRender();
	private:
		void PullMessages();
		void Submit();
		int OnInputCallback(ImGuiInputTextCallbackData* data);
	private:
		struct Entry
		{
			LogLevel Level = LogLevel::Info;
			LogSource Source = LogSource::Core;
			std::string Text;
			bool IsCommand = false;		// Echo of a Python prompt line
		};

		EditorLayer& m_Editor;
		std::vector<Entry> m_Entries;
		uint64_t m_LastSequence = 0;
		std::string m_Filter;
		std::string m_Input;
		std::vector<std::string> m_History;
		int m_HistoryIndex = -1;
		bool m_ShowInfo = true;
		bool m_ShowWarnings = true;
		bool m_ShowErrors = true;
		bool m_AutoScroll = true;
		bool m_ScrollToBottom = false;
		bool m_FocusInput = false;

		static constexpr size_t c_MaxEntries = 5000;
	};

}
