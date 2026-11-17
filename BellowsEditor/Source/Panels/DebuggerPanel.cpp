#include "DebuggerPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ScriptEditorPanel.h"

#include "Bellows/Asset/AssetManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <map>
#include <string>

namespace Bellows {

	namespace {

		const char* DescribeReason(ForgeScript::DebugStopReason reason)
		{
			switch (reason)
			{
				case ForgeScript::DebugStopReason::Breakpoint: return "breakpoint";
				case ForgeScript::DebugStopReason::Step:       return "step";
				case ForgeScript::DebugStopReason::Pause:      return "paused";
			}
			return "stopped";
		}

		// "when update (Scripts/Player.forge line 12)", or the script alone for its top level.
		std::string DescribeFrame(const ForgeScript::DebugFrame& frame)
		{
			const std::string where = fmt::format("{} line {}", frame.File, frame.Line);
			return frame.Function.empty() ? where : fmt::format("{}   {}", frame.Function, where);
		}

		void OpenInScriptEditor(EditorLayer& editor, const std::string& file, int line)
		{
			if (!file.empty() && line > 0 && AssetManager::Exists(file))
				editor.GetScriptEditor().OpenScript(file, static_cast<size_t>(line));
		}

		void DrawVariableTable(const char* id, const std::vector<ForgeScript::DebugVariable>& variables)
		{
			if (variables.empty())
			{
				UI::TextMuted("None");
				return;
			}
			const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
			if (!ImGui::BeginTable(id, 3, flags))
				return;
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.28f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.17f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
			ImGui::TableHeadersRow();
			for (const ForgeScript::DebugVariable& variable : variables)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(variable.Name.c_str());
				ImGui::TableNextColumn();
				UI::TextMuted(variable.Type.c_str());
				ImGui::TableNextColumn();
				ImGui::PushFont(Theme::GetFonts().Mono, 0.0f);
				ImGui::TextUnformatted(variable.Text.c_str());
				ImGui::PopFont();
				// Long values are cut by the column; the tooltip shows all of what the debugger gave.
				if (ImGui::IsItemHovered() && ImGui::CalcTextSize(variable.Text.c_str()).x > ImGui::GetColumnWidth())
					ImGui::SetTooltip("%s", variable.Text.c_str());
			}
			ImGui::EndTable();
		}

	}

	void DebuggerPanel::OnImGuiRender(bool* open)
	{
		if (m_FocusRequested)
		{
			// Selects the panel's tab without taking the keyboard from the Script Editor, which shows
			// the stop. A floating panel is already in view.
			m_FocusRequested = false;
			ImGuiWindow* window = ImGui::FindWindowByName(WindowName);
			if (window && window->DockNode && window->DockNode->TabBar)
				window->DockNode->TabBar->NextSelectedTabId = window->TabId;
		}
		if (!UI::BeginPanel(WindowName, open))
		{
			ImGui::End();
			return;
		}

		Refresh();
		DrawToolbar();
		ImGui::Separator();

		const float scale = Theme::GetScale();
		if (ImGui::BeginTable("##DebuggerSplit", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX))
		{
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 300.0f * scale);
			ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::BeginChild("##Calls");
			DrawCallStack();
			ImGui::Spacing();
			DrawBreakpoints();
			ImGui::EndChild();
			ImGui::TableNextColumn();
			ImGui::BeginChild("##Variables");
			DrawVariables();
			ImGui::EndChild();
			ImGui::EndTable();
		}
		ImGui::End();
	}

	void DebuggerPanel::Refresh()
	{
		const ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		const bool stopped = debugger.IsStopped();
		if (stopped != m_ShownStopped || debugger.GetStopCount() != m_ShownStop)
		{
			m_ShownStopped = stopped;
			m_ShownStop = debugger.GetStopCount();
			m_SelectedFrame = 0;
			m_ShownFrame = SIZE_MAX;
			m_CallStack = debugger.GetCallStack();
		}
		if (m_ShownFrame != m_SelectedFrame)
		{
			m_ShownFrame = m_SelectedFrame;
			m_Variables = debugger.GetVariables(m_SelectedFrame);
			m_ScriptVariables = debugger.GetScriptVariables(m_SelectedFrame);
		}
	}

	void DebuggerPanel::DrawToolbar()
	{
		ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		const bool playing = m_Editor.GetSceneState() != SceneState::Edit;
		const bool stopped = debugger.IsStopped();
		const float spacing = 2.0f * Theme::GetScale();

		ImGui::BeginDisabled(!stopped);
		if (UI::IconButton(ICON_PLAY, "Continue (F5)"))
			m_Editor.ResumeScripts(EditorLayer::DebugAction::Continue);
		ImGui::SameLine(0.0f, spacing);
		if (UI::IconButton(ICON_REDO_DOT, "Step over: run this line, including the functions it calls (F10)"))
			m_Editor.ResumeScripts(EditorLayer::DebugAction::StepOver);
		ImGui::SameLine(0.0f, spacing);
		if (UI::IconButton(ICON_ARROW_DOWN_TO_DOT, "Step in: stop inside the function this line calls (F11)"))
			m_Editor.ResumeScripts(EditorLayer::DebugAction::StepIn);
		ImGui::SameLine(0.0f, spacing);
		if (UI::IconButton(ICON_ARROW_UP_FROM_DOT, "Step out: run to the end of this function (Shift+F11)"))
			m_Editor.ResumeScripts(EditorLayer::DebugAction::StepOut);
		ImGui::EndDisabled();
		ImGui::SameLine(0.0f, spacing);
		ImGui::BeginDisabled(!playing || stopped);
		if (UI::IconButton(ICON_PAUSE, "Stop at the next ForgeScript line that runs"))
			m_Editor.PauseScripts();
		ImGui::EndDisabled();

		ImGui::SameLine(0.0f, 14.0f * Theme::GetScale());
		ImGui::AlignTextToFramePadding();
		if (stopped)
		{
			const ForgeScript::DebugStop& stop = debugger.GetStop();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
			ImGui::Text("Stopped at %s line %d (%s)", stop.File.c_str(), stop.Line, DescribeReason(stop.Reason));
			ImGui::PopStyleColor();
		}
		else if (playing)
		{
			UI::TextMuted("Running. The game stops at a breakpoint.");
		}
		else
		{
			UI::TextMuted("Click left of a line in a ForgeScript file (or press F9) to add a breakpoint, then play.");
		}
	}

	void DebuggerPanel::DrawCallStack()
	{
		UI::SectionHeader("Call stack");
		if (m_CallStack.empty())
		{
			UI::TextMuted(m_ShownStopped ? "No calls" : "Shown when a script stops");
			return;
		}
		for (size_t i = 0; i < m_CallStack.size(); i++)
		{
			const ForgeScript::DebugFrame& frame = m_CallStack[i];
			const std::string label = fmt::format("{}##frame{}", DescribeFrame(frame), i);
			if (ImGui::Selectable(label.c_str(), i == m_SelectedFrame))
			{
				m_SelectedFrame = i;
				OpenInScriptEditor(m_Editor, frame.File, frame.Line);
			}
		}
	}

	void DebuggerPanel::DrawVariables()
	{
		if (!m_ShownStopped)
		{
			UI::CenteredText("The variables of a stopped script show here.");
			return;
		}
		UI::SectionHeader("Variables");
		DrawVariableTable("##Locals", m_Variables);
		ImGui::Spacing();
		UI::SectionHeader("Script variables");
		DrawVariableTable("##Globals", m_ScriptVariables);
	}

	void DebuggerPanel::DrawBreakpoints()
	{
		ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		UI::SectionHeader("Breakpoints");
		if (debugger.GetBreakpointCount() == 0)
		{
			UI::TextMuted("None");
			return;
		}
		if (UI::IconButton(ICON_LIST_X, "Remove every breakpoint"))
		{
			debugger.ClearBreakpoints();
			return;
		}

		// Copied, because removing one changes the map being drawn.
		const std::map<std::string, std::vector<int>> breakpoints = debugger.GetAllBreakpoints();
		for (const auto& [script, lines] : breakpoints)
		{
			for (int line : lines)
			{
				ImGui::PushID(script.c_str());
				ImGui::PushID(line);
				if (UI::IconButton(ICON_X, "Remove this breakpoint"))
					debugger.ToggleBreakpoint(script, line);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Error));
				ImGui::TextUnformatted(ICON_CIRCLE_DOT);
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (ImGui::Selectable(fmt::format("{} line {}", script, line).c_str()))
					OpenInScriptEditor(m_Editor, script, line);
				ImGui::PopID();
				ImGui::PopID();
			}
		}
	}

}
