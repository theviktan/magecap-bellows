#include "ConsolePanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <sstream>

namespace Bellows {

	void ConsolePanel::PullMessages()
	{
		for (LogMessage& message : Log::GetMessagesSince(m_LastSequence))
		{
			m_LastSequence = message.Sequence;
			m_Entries.push_back({ message.Level, message.Source, std::move(message.Text), false });
			m_ScrollToBottom = m_AutoScroll;
		}
		if (m_Entries.size() > c_MaxEntries)
			m_Entries.erase(m_Entries.begin(), m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Entries.size() - c_MaxEntries));
	}

	void ConsolePanel::Submit()
	{
		if (m_Input.empty())
			return;
		const std::string source = m_Input;
		m_Input.clear();
		if (m_History.empty() || m_History.back() != source)
			m_History.push_back(source);
		m_HistoryIndex = -1;

		PullMessages();
		m_Entries.push_back({ LogLevel::Info, LogSource::Client, source, true });

		// Runs between frames so the code may call bellows.editor.run_frames().
		m_Editor.RunAtFrameBoundary([this, source]
		{
			std::string output;
			const bool success = m_Editor.ExecutePython(source, output);
			PullMessages();	// Messages logged by the code come before its result
			// A traceback is one error however many lines it has: only its first line is counted.
			std::istringstream stream(output);
			std::string line;
			bool first = true;
			while (std::getline(stream, line))
			{
				m_Entries.push_back({ success ? LogLevel::Info : LogLevel::Error, LogSource::Client, line, false, !first });
				first = false;
			}
			m_ScrollToBottom = true;
		});
	}

	int ConsolePanel::OnInputCallback(ImGuiInputTextCallbackData* data)
	{
		if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || m_History.empty())
			return 0;

		const int previous = m_HistoryIndex;
		if (data->EventKey == ImGuiKey_UpArrow)
			m_HistoryIndex = m_HistoryIndex < 0 ? static_cast<int>(m_History.size()) - 1 : std::max(0, m_HistoryIndex - 1);
		else if (data->EventKey == ImGuiKey_DownArrow && m_HistoryIndex >= 0)
			m_HistoryIndex = m_HistoryIndex + 1 >= static_cast<int>(m_History.size()) ? -1 : m_HistoryIndex + 1;

		if (previous != m_HistoryIndex)
		{
			data->DeleteChars(0, data->BufTextLen);
			if (m_HistoryIndex >= 0)
				data->InsertChars(0, m_History[static_cast<size_t>(m_HistoryIndex)].c_str());
		}
		return 0;
	}

	void ConsolePanel::OnImGuiRender(bool* open)
	{
		PullMessages();

		if (!UI::BeginPanel("Console", open))
		{
			ImGui::End();
			return;
		}
		const float scale = Theme::GetScale();

		uint32_t warnings = 0, errors = 0;
		for (const Entry& entry : m_Entries)
		{
			warnings += entry.Level == LogLevel::Warn;
			errors += entry.Level >= LogLevel::Error && !entry.Continues;
		}

		// Toolbar
		if (UI::IconButton(ICON_TRASH_2, "Clear"))
			m_Entries.clear();
		ImGui::SameLine();
		auto toggle = [](const char* label, bool& value, ImU32 color)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(value ? color : Theme::TextDisabled));
			ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(value ? Theme::Field : IM_COL32(0, 0, 0, 0)));
			if (ImGui::Button(label))
				value = !value;
			ImGui::PopStyleColor(2);
		};
		toggle(ICON_INFO " Info", m_ShowInfo, Theme::TextMuted);
		ImGui::SameLine();
		const std::string warningLabel = fmt::format(ICON_TRIANGLE_ALERT " {}", warnings);
		toggle(warningLabel.c_str(), m_ShowWarnings, Theme::Warning);
		ImGui::SameLine();
		const std::string errorLabel = fmt::format(ICON_CIRCLE_X " {}", errors);
		toggle(errorLabel.c_str(), m_ShowErrors, Theme::Error);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(std::max(120.0f * scale, ImGui::GetContentRegionAvail().x - 110.0f * scale));
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Filter messages", &m_Filter);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

		// Messages
		const float inputHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f * scale;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(Theme::Background));
		ImGui::BeginChild("##Messages", ImVec2(0.0f, -inputHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::PopStyleColor();
		ImGui::PushFont(Theme::GetFonts().Mono, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f * scale));

		std::vector<const Entry*> visible;
		visible.reserve(m_Entries.size());
		for (const Entry& entry : m_Entries)
		{
			bool shown = m_ShowInfo || entry.IsCommand;
			if (entry.Level >= LogLevel::Error)
				shown = m_ShowErrors;
			else if (entry.Level == LogLevel::Warn)
				shown = m_ShowWarnings;
			if (!shown)
				continue;
			if (!m_Filter.empty() && entry.Text.find(m_Filter) == std::string::npos)
				continue;
			visible.push_back(&entry);
		}
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(visible.size()));
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				const Entry& entry = *visible[static_cast<size_t>(i)];
				ImU32 color = Theme::Text;
				const char* icon = "  ";
				if (entry.IsCommand) { color = Theme::NeutralBright; icon = ICON_CHEVRON_RIGHT; }
				else if (entry.Level >= LogLevel::Error) { color = Theme::Error; icon = ICON_CIRCLE_X; }
				else if (entry.Level == LogLevel::Warn) { color = Theme::Warning; icon = ICON_TRIANGLE_ALERT; }
				else if (entry.Level == LogLevel::Trace) color = Theme::TextDisabled;
				else if (entry.Source == LogSource::Core) color = Theme::TextMuted;

				ImGui::PushID(i);
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(color));
				ImGui::TextUnformatted(icon);
				ImGui::SameLine();
				ImGui::TextUnformatted(entry.Text.c_str());
				// The line's color stops here: its menu is drawn in the ordinary text color.
				ImGui::PopStyleColor();
				if (ImGui::BeginPopupContextItem("##line"))
				{
					if (ImGui::MenuItem(ICON_COPY "  Copy"))
						ImGui::SetClipboardText(entry.Text.c_str());
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
		ImGui::PopFont();
		if (m_ScrollToBottom)
		{
			ImGui::SetScrollHereY(1.0f);
			m_ScrollToBottom = false;
		}
		ImGui::EndChild();

		// Python prompt
		ImGui::PushFont(Theme::GetFonts().Mono, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::NeutralBright));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(">>>");
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (m_FocusInput)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusInput = false;
		}
		const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
		if (ImGui::InputTextWithHint("##python", "Python, e.g.  bellows.find_entity(\"Player\").transform.position", &m_Input, flags,
			[](ImGuiInputTextCallbackData* data) { return static_cast<ConsolePanel*>(data->UserData)->OnInputCallback(data); }, this))
		{
			Submit();
			m_FocusInput = true;
		}
		ImGui::PopFont();

		ImGui::End();
	}

}
