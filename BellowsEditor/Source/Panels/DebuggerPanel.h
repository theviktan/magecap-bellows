#pragma once

#include <ForgeScript/VM/VM.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bellows {

	class EditorLayer;

	// The ForgeScript debugger's window: Continue and the step buttons, where the script stopped,
	// its call stack and the variables of the selected call, a Watch list of lines worked out in that
	// call (ScriptDebugger::Evaluate), and every breakpoint with its condition or log message.
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
		void DrawWatch();
		void DrawBreakpoints();
		// Works out every watch in the selected call (or clears their values when nothing is stopped).
		void EvaluateWatches();
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

		// Watch: lines kept for the editor session and worked out again at every stop and selected call.
		struct Watch
		{
			std::string Expression;
			bool Ok = false;
			std::string Type;
			std::string Text;		// The value, or why it couldn't be worked out
		};
		std::vector<Watch> m_Watches;
		std::string m_NewWatch;
	};

}
