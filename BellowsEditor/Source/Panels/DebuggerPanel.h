#pragma once

#include <ForgeScript/VM/VM.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Bellows {

	class EditorLayer;

	// The ForgeScript debugger's window: Continue and the step buttons, where the script stopped,
	// its call stack and the variables of the selected call, and every breakpoint.
	class DebuggerPanel
	{
	public:
		static constexpr const char* WindowName = "Debugger";

		explicit DebuggerPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		void OnImGuiRender(bool* open);
		// Shows the panel's tab (after a script stops).
		void Focus() { m_FocusRequested = true; }
	private:
		void DrawToolbar();
		void DrawCallStack();
		void DrawVariables();
		void DrawBreakpoints();
		// Re-reads the stack and variables when the stop or the selected call changed.
		void Refresh();
	private:
		EditorLayer& m_Editor;
		bool m_FocusRequested = false;

		// What the panel shows of the current stop (read once per stop, not every frame).
		uint64_t m_ShownStop = 0;
		bool m_ShownStopped = false;
		size_t m_SelectedFrame = 0;
		size_t m_ShownFrame = SIZE_MAX;
		std::vector<ForgeScript::DebugFrame> m_CallStack;
		std::vector<ForgeScript::DebugVariable> m_Variables;
		std::vector<ForgeScript::DebugVariable> m_ScriptVariables;
	};

}
