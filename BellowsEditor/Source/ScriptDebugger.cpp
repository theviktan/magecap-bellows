#include "ScriptDebugger.h"

#include "Bellows/Scripting/ForgeScriptRuntime.h"

#include <algorithm>

namespace Bellows {

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
		for (const auto& [script, lines] : m_Breakpoints)
			m_Runtime->SetBreakpoints(script, lines);
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
		const std::vector<int>& lines = GetBreakpoints(script);
		return std::binary_search(lines.begin(), lines.end(), line);
	}

	void ScriptDebugger::SetBreakpoints(const std::string& script, std::vector<int> lines)
	{
		if (script.empty())
			return;
		std::erase_if(lines, [](int line) { return line < 1; });
		std::sort(lines.begin(), lines.end());
		lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

		const auto existing = m_Breakpoints.find(script);
		if (existing != m_Breakpoints.end() && existing->second == lines)
			return;
		if (lines.empty())
		{
			if (existing == m_Breakpoints.end())
				return;
			m_Breakpoints.erase(existing);
		}
		else
		{
			m_Breakpoints[script] = std::move(lines);
		}
		m_BreakpointRevision++;
		PushBreakpoints(script);
	}

	const std::vector<int>& ScriptDebugger::GetBreakpoints(const std::string& script) const
	{
		static const std::vector<int> s_None;
		const auto found = m_Breakpoints.find(script);
		return found != m_Breakpoints.end() ? found->second : s_None;
	}

	size_t ScriptDebugger::GetBreakpointCount() const
	{
		size_t count = 0;
		for (const auto& [script, lines] : m_Breakpoints)
			count += lines.size();
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
			m_Runtime->SetBreakpoints(script, GetBreakpoints(script));
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

}
