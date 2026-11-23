#include "ScriptDebugger.h"

#include "Bellows/Scripting/ForgeScriptRuntime.h"

#include <algorithm>
#include <string_view>

namespace Bellows {

	namespace {

		std::string Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t\r\n");
			return std::string(text.substr(first, last - first + 1));
		}

		bool SameBreakpoints(const std::vector<ForgeScript::Breakpoint>& a, const std::vector<ForgeScript::Breakpoint>& b)
		{
			return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const ForgeScript::Breakpoint& x, const ForgeScript::Breakpoint& y)
			{
				return x.Line == y.Line && x.Condition == y.Condition && x.LogMessage == y.LogMessage;
			});
		}

	}

	ScriptDebugger::~ScriptDebugger()
	{
		Connect(nullptr);
	}

	void ScriptDebugger::Connect(Scripting::ForgeScriptRuntime* runtime)
	{
		if (m_Runtime)
		{
			m_Runtime->SetDebugHandler(nullptr);
			m_Runtime->ClearBreakpoints();
		}
		m_Runtime = runtime;
		if (!m_Runtime)
			return;
		m_Runtime->SetDebugHandler([this](const ForgeScript::DebugStop& stop) { OnStop(stop); });
		m_Runtime->ClearBreakpoints();
		for (const auto& [script, breakpoints] : m_Breakpoints)
			m_Runtime->SetDetailedBreakpoints(script, breakpoints);
	}

	// -------------------------------------------------------------------------
	// Breakpoints
	// -------------------------------------------------------------------------

	bool ScriptDebugger::ToggleBreakpoint(const std::string& script, int line)
	{
		if (script.empty() || line < 1)
			return false;
		std::vector<int> lines = GetBreakpoints(script);
		const auto found = std::find(lines.begin(), lines.end(), line);
		const bool added = found == lines.end();
		if (added)
			lines.push_back(line);
		else
			lines.erase(found);
		SetBreakpoints(script, std::move(lines));
		return added;
	}

	bool ScriptDebugger::HasBreakpoint(const std::string& script, int line) const
	{
		return FindBreakpoint(script, line) != nullptr;
	}

	void ScriptDebugger::SetBreakpoints(const std::string& script, std::vector<int> lines)
	{
		// Lines that stay keep their conditions and log messages, so toggling one line (or moving lines
		// while the script is edited) never loses another's.
		std::vector<ForgeScript::Breakpoint> breakpoints;
		breakpoints.reserve(lines.size());
		for (int line : lines)
		{
			if (const ForgeScript::Breakpoint* existing = FindBreakpoint(script, line))
				breakpoints.push_back(*existing);
			else
				breakpoints.push_back({ line, {}, {} });
		}
		SetDetailedBreakpoints(script, std::move(breakpoints));
	}

	void ScriptDebugger::SetDetailedBreakpoints(const std::string& script, std::vector<ForgeScript::Breakpoint> breakpoints)
	{
		if (script.empty())
			return;
		std::erase_if(breakpoints, [](const ForgeScript::Breakpoint& breakpoint) { return breakpoint.Line < 1; });
		for (ForgeScript::Breakpoint& breakpoint : breakpoints)
		{
			breakpoint.Condition = Trim(breakpoint.Condition);
			breakpoint.LogMessage = Trim(breakpoint.LogMessage);
		}
		// Stable, so of two at one line the later one given comes last and is the one kept.
		std::stable_sort(breakpoints.begin(), breakpoints.end(), [](const auto& a, const auto& b) { return a.Line < b.Line; });
		std::vector<ForgeScript::Breakpoint> unique;
		unique.reserve(breakpoints.size());
		for (ForgeScript::Breakpoint& breakpoint : breakpoints)
		{
			if (!unique.empty() && unique.back().Line == breakpoint.Line)
				unique.back() = std::move(breakpoint);
			else
				unique.push_back(std::move(breakpoint));
		}

		const auto existing = m_Breakpoints.find(script);
		if (existing != m_Breakpoints.end() && SameBreakpoints(existing->second, unique))
			return;
		if (unique.empty())
		{
			if (existing == m_Breakpoints.end())
				return;
			m_Breakpoints.erase(existing);
		}
		else
		{
			m_Breakpoints[script] = std::move(unique);
		}
		m_BreakpointRevision++;
		PushBreakpoints(script);
	}

	bool ScriptDebugger::SetBreakpoint(const std::string& script, int line, const std::string& condition, const std::string& logMessage)
	{
		if (script.empty() || line < 1)
			return false;
		std::vector<ForgeScript::Breakpoint> breakpoints = GetDetailedBreakpoints(script);
		breakpoints.push_back({ line, condition, logMessage });
		SetDetailedBreakpoints(script, std::move(breakpoints));
		return true;
	}

	const ForgeScript::Breakpoint* ScriptDebugger::FindBreakpoint(const std::string& script, int line) const
	{
		const std::vector<ForgeScript::Breakpoint>& breakpoints = GetDetailedBreakpoints(script);
		const auto found = std::lower_bound(breakpoints.begin(), breakpoints.end(), line,
			[](const ForgeScript::Breakpoint& breakpoint, int value) { return breakpoint.Line < value; });
		return found != breakpoints.end() && found->Line == line ? &*found : nullptr;
	}

	std::vector<int> ScriptDebugger::GetBreakpoints(const std::string& script) const
	{
		std::vector<int> lines;
		for (const ForgeScript::Breakpoint& breakpoint : GetDetailedBreakpoints(script))
			lines.push_back(breakpoint.Line);
		return lines;
	}

	const std::vector<ForgeScript::Breakpoint>& ScriptDebugger::GetDetailedBreakpoints(const std::string& script) const
	{
		static const std::vector<ForgeScript::Breakpoint> s_None;
		const auto found = m_Breakpoints.find(script);
		return found != m_Breakpoints.end() ? found->second : s_None;
	}

	size_t ScriptDebugger::GetBreakpointCount() const
	{
		size_t count = 0;
		for (const auto& [script, breakpoints] : m_Breakpoints)
			count += breakpoints.size();
		return count;
	}

	void ScriptDebugger::ClearBreakpoints()
	{
		if (!m_Breakpoints.empty())
			m_BreakpointRevision++;
		m_Breakpoints.clear();
		if (m_Runtime)
			m_Runtime->ClearBreakpoints();
	}

	void ScriptDebugger::PushBreakpoints(const std::string& script)
	{
		if (m_Runtime)
			m_Runtime->SetDetailedBreakpoints(script, GetDetailedBreakpoints(script));
	}

	// -------------------------------------------------------------------------
	// The play session
	// -------------------------------------------------------------------------

	ForgeScript::VM* ScriptDebugger::GetMachine() const
	{
		return m_Runtime ? m_Runtime->GetMachine() : nullptr;
	}

	bool ScriptDebugger::IsPlaying() const
	{
		return GetMachine() != nullptr;
	}

	bool ScriptDebugger::IsStopped() const
	{
		const ForgeScript::VM* machine = GetMachine();
		return machine && machine->IsStopped();
	}

	void ScriptDebugger::OnStop(const ForgeScript::DebugStop& stop)
	{
		m_Stop = stop;
		m_StopCount++;
		if (m_StopCallback)
			m_StopCallback(stop);
	}

	void ScriptDebugger::EndSession()
	{
		if (ForgeScript::VM* machine = GetMachine())
			machine->StopDebugging();
	}

	bool ScriptDebugger::Continue()
	{
		ForgeScript::VM* machine = GetMachine();
		return machine && machine->Continue();
	}

	bool ScriptDebugger::StepOver()
	{
		ForgeScript::VM* machine = GetMachine();
		return machine && machine->StepOver();
	}

	bool ScriptDebugger::StepIn()
	{
		ForgeScript::VM* machine = GetMachine();
		return machine && machine->StepIn();
	}

	bool ScriptDebugger::StepOut()
	{
		ForgeScript::VM* machine = GetMachine();
		return machine && machine->StepOut();
	}

	bool ScriptDebugger::Pause()
	{
		ForgeScript::VM* machine = GetMachine();
		if (!machine || machine->IsStopped())
			return false;
		machine->Pause();
		return true;
	}

	std::vector<ForgeScript::DebugFrame> ScriptDebugger::GetCallStack() const
	{
		const ForgeScript::VM* machine = GetMachine();
		return machine ? machine->GetCallStack() : std::vector<ForgeScript::DebugFrame>();
	}

	std::vector<ForgeScript::DebugVariable> ScriptDebugger::GetVariables(size_t frame) const
	{
		ForgeScript::VM* machine = GetMachine();
		if (!machine || !machine->IsStopped())
			return {};
		return machine->GetVariables(frame);
	}

	std::vector<ForgeScript::DebugVariable> ScriptDebugger::GetScriptVariables(size_t frame) const
	{
		ForgeScript::VM* machine = GetMachine();
		if (!machine || !machine->IsStopped())
			return {};
		const std::vector<ForgeScript::DebugFrame> stack = machine->GetCallStack();
		if (frame >= stack.size() || !stack[frame].Owner)
			return {};
		return machine->GetGlobals(stack[frame].Owner);
	}

	bool ScriptDebugger::Evaluate(size_t frame, const std::string& expression, ForgeScript::DebugVariable& outValue, std::string& outError)
	{
		ForgeScript::VM* machine = GetMachine();
		if (!machine || !machine->IsStopped())
		{
			outError = "Nothing is stopped. Values can be worked out while a script is stopped at a breakpoint.";
			return false;
		}
		const std::string code = Trim(expression);
		if (code.empty())
		{
			outError = "Type something to work out, like 'health * 2'.";
			return false;
		}
		if (frame >= machine->GetCallStack().size())
		{
			outError = "That call isn't on the stack any more.";
			return false;
		}
		return machine->Evaluate(frame, code, outValue, outError);
	}

}
