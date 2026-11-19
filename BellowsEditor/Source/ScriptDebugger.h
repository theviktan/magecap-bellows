#pragma once

#include <ForgeScript/VM/VM.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Bellows {

	namespace Scripting { class ForgeScriptRuntime; }

	// The editor's ForgeScript debugger, without any UI: breakpoints by script, the current stop,
	// stepping and what the stopped script can see. It drives ForgeScriptRuntime's debugger API
	// (from the ForgeScript VM), so the Script Editor and the Debugger panel share one state.
	//
	// Breakpoints are kept here for the whole editor session and handed to the runtime, which gives
	// them to every play session's machine. A stop holds the script until Continue or a step; the
	// editor pauses the game meanwhile (see EditorLayer). When play stops, the runtime ends a stop
	// without running the rest of the held code.
	class ScriptDebugger
	{
	public:
		using StopCallback = std::function<void(const ForgeScript::DebugStop&)>;

		ScriptDebugger() = default;
		~ScriptDebugger();
		ScriptDebugger(const ScriptDebugger&) = delete;
		ScriptDebugger& operator=(const ScriptDebugger&) = delete;

		// Installs the stop handler and the breakpoints in 'runtime'; null disconnects. The runtime must
		// outlive the connection.
		void Connect(Scripting::ForgeScriptRuntime* runtime);
		// Called after a script stops, inside the game's update: note the stop and return.
		void SetStopCallback(StopCallback callback) { m_StopCallback = std::move(callback); }

		// Breakpoints: 1-based lines by script asset path ("Scripts/Player.forge").
		// Returns whether the line has a breakpoint now.
		bool ToggleBreakpoint(const std::string& script, int line);
		bool HasBreakpoint(const std::string& script, int line) const;
		// Replaces a script's breakpoints; lines below 1 and repeats are dropped, an empty list removes them.
		void SetBreakpoints(const std::string& script, std::vector<int> lines);
		const std::vector<int>& GetBreakpoints(const std::string& script) const;
		const std::map<std::string, std::vector<int>>& GetAllBreakpoints() const { return m_Breakpoints; }
		size_t GetBreakpointCount() const;
		void ClearBreakpoints();
		// Goes up by one whenever a breakpoint is added or removed, so views know to redraw them.
		uint64_t GetBreakpointRevision() const { return m_BreakpointRevision; }

		bool IsPlaying() const;
		bool IsStopped() const;
		// The current stop; meaningful only while IsStopped().
		const ForgeScript::DebugStop& GetStop() const { return m_Stop; }
		// Goes up by one at every stop, so views can tell a new stop from the one they show.
		uint64_t GetStopCount() const { return m_StopCount; }

		// Let the stopped script go on. Each returns false when nothing is stopped. The script may stop
		// again before these return (the stop callback has run then).
		bool Continue();
		bool StepOver();
		bool StepIn();
		bool StepOut();
		// Stops the next ForgeScript line that runs. False when not playing or already stopped.
		bool Pause();
		// Play is ending: held code does not go on, and nothing stops at a breakpoint for the rest of
		// this play session, so quit handlers run to the end. The breakpoints stay set for the next one.
		void EndSession();

		// What the stopped script can see; empty when nothing is stopped. Frame 0 is the innermost call.
		std::vector<ForgeScript::DebugFrame> GetCallStack() const;
		std::vector<ForgeScript::DebugVariable> GetVariables(size_t frame) const;
		// The variables of the script that the frame's code belongs to.
		std::vector<ForgeScript::DebugVariable> GetScriptVariables(size_t frame) const;
	private:
		ForgeScript::VM* GetMachine() const;
		void OnStop(const ForgeScript::DebugStop& stop);
		void PushBreakpoints(const std::string& script);
	private:
		Scripting::ForgeScriptRuntime* m_Runtime = nullptr;
		StopCallback m_StopCallback;
		std::map<std::string, std::vector<int>> m_Breakpoints;
		ForgeScript::DebugStop m_Stop;
		uint64_t m_StopCount = 0;
		uint64_t m_BreakpointRevision = 0;
	};

}
