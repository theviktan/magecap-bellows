#include "ProfilerPanel.h"

#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"

#include "Bellows/Core/Profiler.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace Bellows {

	namespace {

		constexpr std::array<int, 4> c_AverageFrameCounts = { 30, 60, 120, 300 };
		constexpr const char* c_AverageFrameLabels[] = { "30 frames", "60 frames", "120 frames", "300 frames" };

		// Indents a name by its depth, so the tables read as a tree.
		void ScopeName(const char* name, uint32_t depth)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing * 0.6f);
			ImGui::TextUnformatted(name);
		}

		void Milliseconds(double value)
		{
			ImGui::Text("%.3f", value);
		}

	}

	void ProfilerPanel::OnImGuiRender(bool* open)
	{
		ImGui::SetNextWindowSize(ImVec2(560.0f * Theme::GetScale(), 440.0f * Theme::GetScale()), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(WindowName, open))
		{
			ImGui::End();
			return;
		}

		DrawToolbar();
		const std::deque<ProfileFrame>& history = Profiler::GetHistory();
		if (history.empty())
		{
			UI::TextMuted(Profiler::IsPaused() ? "Recording is paused. Press Resume to record frames."
				: "No frames recorded yet.");
			ImGui::End();
			return;
		}

		// A picked frame that has left the history (recording resumed) gives way to the newest.
		if (m_SelectedFrame && FindSelectedFrame(history).Index != *m_SelectedFrame)
			m_SelectedFrame.reset();

		DrawFrameGraph(history);
		if (ImGui::BeginTabBar("##ProfilerViews"))
		{
			if (ImGui::BeginTabItem("Average"))
			{
				DrawAverages();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Frame"))
			{
				DrawSelectedFrame(FindSelectedFrame(history));
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	}

	void ProfilerPanel::DrawToolbar()
	{
		const bool paused = Profiler::IsPaused();
		if (UI::IconButton(paused ? ICON_PLAY : ICON_PAUSE, paused ? "Resume recording" : "Pause recording, to look at the frames already recorded", paused))
			Profiler::SetPaused(!paused);
		ImGui::SameLine();
		if (UI::IconButton(ICON_TRASH_2, "Clear the recorded frames"))
		{
			Profiler::Clear();
			m_SelectedFrame.reset();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f * Theme::GetScale());
		ImGui::Combo("Average over", &m_AverageChoice, c_AverageFrameLabels, static_cast<int>(c_AverageFrameCounts.size()));
		if (m_SelectedFrame)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Follow newest"))
				m_SelectedFrame.reset();
		}
	}

	void ProfilerPanel::DrawFrameGraph(const std::deque<ProfileFrame>& history)
	{
		const float scale = Theme::GetScale();
		const ImVec2 size(std::max(ImGui::GetContentRegionAvail().x, 50.0f), 80.0f * scale);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##FrameGraph", size);
		const bool hovered = ImGui::IsItemHovered();
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

		// Scaled to the 95th-percentile frame, so one very slow frame (loading a project, starting play)
		// doesn't flatten the rest; frames above the scale are drawn full height in the error color.
		// At least 30 FPS, so a smooth game doesn't look spiky.
		std::vector<double> times;
		times.reserve(history.size());
		for (const ProfileFrame& frame : history)
			times.push_back(frame.CpuMs);
		const auto percentile = times.begin() + static_cast<std::ptrdiff_t>((times.size() - 1) * 95 / 100);
		std::nth_element(times.begin(), percentile, times.end());
		const float top = static_cast<float>(std::max(33.4, *percentile * 1.25));

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 end(origin.x + size.x, origin.y + size.y);
		drawList->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);

		// One slot per frame the history can hold, newest at the right, so the graph scrolls left.
		const float slot = size.x / static_cast<float>(Profiler::c_HistorySize);
		const size_t firstSlot = Profiler::c_HistorySize - history.size();
		const uint64_t selected = FindSelectedFrame(history).Index;
		const ImU32 barColor = Theme::Accent;
		const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_Text);
		std::optional<size_t> hoveredFrame;
		for (size_t i = 0; i < history.size(); i++)
		{
			const float x0 = origin.x + static_cast<float>(firstSlot + i) * slot;
			const bool clipped = static_cast<float>(history[i].CpuMs) > top;
			const float height = std::min(static_cast<float>(history[i].CpuMs) / top, 1.0f) * size.y;
			const bool isSelected = m_SelectedFrame && history[i].Index == selected;
			drawList->AddRectFilled(ImVec2(x0, end.y - std::max(height, 1.0f)), ImVec2(x0 + std::max(slot - 1.0f, 1.0f), end.y),
				isSelected ? selectedColor : clipped ? Theme::Error : barColor);
			if (hovered && ImGui::GetIO().MousePos.x >= x0 && ImGui::GetIO().MousePos.x < x0 + slot)
				hoveredFrame = i;
		}

		// Guides at 60 and 30 FPS. On a slow machine the lines sit close together; a label that would
		// overlap the one below it is left off.
		float lastLabelY = end.y + ImGui::GetTextLineHeight();
		for (const auto& [milliseconds, label] : { std::pair(16.7f, "60 FPS"), std::pair(33.3f, "30 FPS") })
		{
			if (milliseconds > top)
				continue;
			const float y = end.y - milliseconds / top * size.y;
			drawList->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), ImGui::GetColorU32(ImGuiCol_TextDisabled));
			if (lastLabelY - y < ImGui::GetTextLineHeight())
				continue;
			drawList->AddText(ImVec2(origin.x + 4.0f * scale, y - ImGui::GetTextLineHeight()), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
			lastLabelY = y;
		}

		if (hoveredFrame)
		{
			const ProfileFrame& frame = history[*hoveredFrame];
			ImGui::SetTooltip("Frame %llu: %.2f ms\nClick to look at this frame", static_cast<unsigned long long>(frame.Index), frame.CpuMs);
			// Picking a frame pauses recording, so the frame stays in the history while it's read.
			if (clicked)
			{
				m_SelectedFrame = frame.Index;
				Profiler::SetPaused(true);
			}
		}
	}

	void ProfilerPanel::DrawAverages()
	{
		const int frameCount = c_AverageFrameCounts[static_cast<size_t>(std::clamp(m_AverageChoice, 0, static_cast<int>(c_AverageFrameCounts.size()) - 1))];
		const std::vector<ProfileFrame> frames = Profiler::GetRecentFrames(static_cast<size_t>(frameCount));
		double average = 0.0;
		for (const ProfileFrame& frame : frames)
			average += frame.CpuMs;
		average /= static_cast<double>(frames.size());
		ImGui::Text("CPU frame: %.2f ms on average over %zu frames (%.0f FPS)", average, frames.size(), average > 0.0 ? 1000.0 / average : 0.0);
		UI::HelpMarker("Total includes the scopes inside a row; Self leaves them out. Max is the slowest single frame. "
			"Times are in milliseconds, averaged over every frame, including frames where the row didn't run.");

		const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("##ProfilerAverages", 5, flags))
			return;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.44f);
		ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthStretch, 0.14f);
		ImGui::TableSetupColumn("Self", ImGuiTableColumnFlags_WidthStretch, 0.14f);
		ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.14f);
		ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthStretch, 0.14f);
		ImGui::TableHeadersRow();
		for (const ProfileSummary& summary : Profiler::Summarize(frames))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ScopeName(summary.Name.c_str(), summary.Depth);
			ImGui::TableNextColumn();
			Milliseconds(summary.AverageMs);
			ImGui::TableNextColumn();
			Milliseconds(summary.AverageSelfMs);
			ImGui::TableNextColumn();
			Milliseconds(summary.MaxMs);
			ImGui::TableNextColumn();
			ImGui::Text("%.1f", summary.CallsPerFrame);
		}
		ImGui::EndTable();
	}

	void ProfilerPanel::DrawSelectedFrame(const ProfileFrame& frame)
	{
		ImGui::Text("Frame %llu: %.2f ms", static_cast<unsigned long long>(frame.Index), frame.CpuMs);
		if (!m_SelectedFrame)
		{
			ImGui::SameLine();
			UI::TextMuted("(the newest; click a bar in the graph to pick one)");
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if (!ImGui::BeginTable("##ProfilerFrame", 4, flags))
			return;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch, 0.52f);
		ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthStretch, 0.16f);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch, 0.16f);
		ImGui::TableSetupColumn("% of Frame", ImGuiTableColumnFlags_WidthStretch, 0.16f);
		ImGui::TableHeadersRow();
		for (const ProfileSample& sample : frame.Samples)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ScopeName(sample.Name, sample.Depth);
			ImGui::TableNextColumn();
			Milliseconds(sample.StartMs);
			ImGui::TableNextColumn();
			Milliseconds(sample.DurationMs);
			ImGui::TableNextColumn();
			ImGui::Text("%.1f%%", frame.CpuMs > 0.0 ? sample.DurationMs / frame.CpuMs * 100.0 : 0.0);
		}
		ImGui::EndTable();
	}

	const ProfileFrame& ProfilerPanel::FindSelectedFrame(const std::deque<ProfileFrame>& history) const
	{
		if (m_SelectedFrame)
		{
			auto it = std::find_if(history.begin(), history.end(), [this](const ProfileFrame& frame) { return frame.Index == *m_SelectedFrame; });
			if (it != history.end())
				return *it;
		}
		return history.back();
	}

}
