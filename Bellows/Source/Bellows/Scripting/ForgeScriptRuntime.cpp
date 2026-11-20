#include "bwpch.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"

#include "ForgeScript/Compiler/ScriptVariables.h"
#include "ForgeScript/Tools/ProjectPaths.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace Bellows::Scripting {

	namespace {

		// Scripts that keep failing are switched off so the console stays readable (same limit as Python).
		constexpr uint32_t c_MaxErrorsPerInstance = 10;
		// A frame that runs this many instructions without a 'wait' is stopped with an error instead of
		// freezing the game (for example "repeat forever" without a wait).
		constexpr uint64_t c_InstructionLimit = 50'000'000;

		bool ReadScript(const std::string& scriptPath, std::string& outSource)
		{
			std::ifstream stream(AssetManager::GetAbsolutePath(scriptPath), std::ios::in | std::ios::binary);
			if (!stream)
				return false;
			std::ostringstream contents;
			contents << stream.rdbuf();
			outSource = contents.str();
			return true;
		}

		// "Scripts/Player.forge line 12: <message>", the same shape for mistakes found while
		// loading and errors raised while running, so the Console and the Inspector read alike.
		std::string Describe(const std::string& file, int line, const std::string& message)
		{
			std::string text = file.empty() ? std::string("script") : file;
			if (line > 0)
				text += " line " + std::to_string(line);
			return text + ": " + message;
		}

		// A script's place for the 'script error' event: its asset path, with forward slashes.
		std::string ReportedPath(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return path;
		}

		// Sends a ForgeScript error to the 'script error' event, as Python's errors are. A function calling
		// itself is one frame with a count in ForgeScript; the event lists each call, up to a limit, so a
		// runaway recursion can't make the stack huge.
		void SendScriptError(const std::string& message, const std::string& file, int line, const std::vector<ForgeScript::StackFrame>& frames)
		{
			constexpr size_t c_MaxStack = 64;
			Scripting::ScriptErrorReport report;
			report.Message = message;
			report.File = ReportedPath(file);
			report.Line = line;
			report.Language = "forgescript";
			for (const ForgeScript::StackFrame& frame : frames)
			{
				std::string place = ReportedPath(frame.File) + ":" + std::to_string(frame.Line);
				if (!frame.Function.empty())
					place += " in " + frame.Function;
				for (int i = 0; i < frame.Repeats && report.Stack.size() < c_MaxStack; i++)
					report.Stack.push_back(place);
			}
			if (report.Stack.empty())
				report.Stack.push_back(report.File + ":" + std::to_string(line));
			Scripting::ReportScriptError(report);
		}

		std::string Describe(const ForgeScript::ScriptError& error)
		{
			std::string text = Describe(error.File, error.Line, error.Message);
			// A single trace line only repeats the location; a longer one shows how the error was reached.
			if (error.Trace.size() > 1)
			{
				for (const std::string& line : error.Trace)
					text += "\n    " + line;
			}
			return text;
		}

		ForgeScript::Value FieldToForgeScript(ForgeScript::VM& vm, const ScriptFieldValue& value)
		{
			return std::visit([&vm](const auto& v) -> ForgeScript::Value
			{
				using V = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<V, bool>)
					return ForgeScript::Value::FromBool(v);
				else if constexpr (std::is_same_v<V, int64_t> || std::is_same_v<V, double>)
					return ForgeScript::Value::FromNumber(static_cast<double>(v));
				else if constexpr (std::is_same_v<V, std::string>)
					return vm.MakeText(v);
				else if constexpr (std::is_same_v<V, glm::vec2>)
					return vm.MakeVector(v.x, v.y, 0.0);
				else if constexpr (std::is_same_v<V, glm::vec3>)
					return vm.MakeVector(v.x, v.y, v.z);
				else
					return vm.MakeList({ ForgeScript::Value::FromNumber(v.r), ForgeScript::Value::FromNumber(v.g),
						ForgeScript::Value::FromNumber(v.b), ForgeScript::Value::FromNumber(v.a) });
			}, value);
		}

		ScriptFieldValue VariableToField(const ForgeScript::ScriptVariable& variable)
		{
			using Kind = ForgeScript::ScriptVariable::ValueKind;
			const double* c = variable.Components;
			switch (variable.Kind)
			{
				case Kind::WholeNumber: return static_cast<int64_t>(variable.Number);
				case Kind::Text:        return variable.Text;
				case Kind::Boolean:     return variable.Boolean;
				case Kind::Vector2:     return glm::vec2(static_cast<float>(c[0]), static_cast<float>(c[1]));
				case Kind::Vector3:     return glm::vec3(static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]));
				case Kind::Color:       return glm::vec4(static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), static_cast<float>(c[3]));
				case Kind::Number:      break;
			}
			return variable.Number;
		}

	}

	ForgeScriptRuntime::ForgeScriptRuntime(ErrorReporter reporter)
		: m_Reporter(std::move(reporter))
	{
	}

	ForgeScriptRuntime::~ForgeScriptRuntime()
	{
		// The hub must never call a runtime that is gone, even if play mode was never stopped.
		RemoveScriptEventListener(m_EventListener);
	}

	bool ForgeScriptRuntime::IsForgeScript(const std::string& scriptPath)
	{
		return std::filesystem::path(scriptPath).extension() == ".forge";
	}

	Scope<ForgeScript::VM> ForgeScriptRuntime::CreateMachine()
	{
		ForgeScript::VMConfig config;
		config.InstructionLimit = c_InstructionLimit;
		Scope<ForgeScript::VM> vm = CreateScope<ForgeScript::VM>(config);
		vm->SetPrintHandler([](const std::string& text) { BW_INFO("{}", text); });
		vm->SetErrorHandler([this](const ForgeScript::ScriptError& error)
		{
			m_Reporter("ForgeScript", Describe(error));
			SendScriptError(error.Message, error.File, error.Line, error.Frames);
		});
		InstallModuleLoader(*vm);
		RegisterForgeScriptApi(*vm);
		vm->SetDebugHandler(m_DebugHandler);
		for (const auto& [scriptPath, lines] : m_Breakpoints)
			vm->SetBreakpoints(scriptPath, lines);
		return vm;
	}

	void ForgeScriptRuntime::SetBreakpoints(const std::string& scriptPath, const std::vector<int>& lines)
	{
		if (lines.empty())
			m_Breakpoints.erase(scriptPath);
		else
			m_Breakpoints[scriptPath] = lines;
		if (m_VM)
			m_VM->SetBreakpoints(scriptPath, lines);
	}

	void ForgeScriptRuntime::ClearBreakpoints()
	{
		m_Breakpoints.clear();
		if (m_VM)
			m_VM->ClearBreakpoints();
	}

	void ForgeScriptRuntime::SetDebugHandler(ForgeScript::DebugHandler handler)
	{
		m_DebugHandler = std::move(handler);
		if (m_VM)
			m_VM->SetDebugHandler(m_DebugHandler);
	}

	void ForgeScriptRuntime::InstallModuleLoader(ForgeScript::VM& vm)
	{
		vm.SetModuleLoader([](const std::string& path, std::string& outName, std::string& outSource)
		{
			// The VM has already refused absolute paths, ".." and other extensions (CheckUsePath); a link
			// inside the assets can still point outside, so the file it reaches must be in the asset folder.
			const std::string scriptPath = std::filesystem::path(path).has_extension() ? path : path + ".forge";
			if (!ForgeScript::CheckUsePath(path).empty() ||
				!ForgeScript::IsInsideFolder(AssetManager::GetAssetDirectory(), AssetManager::GetAbsolutePath(scriptPath)))
				return false;
			outName = scriptPath;
			return ReadScript(scriptPath, outSource);
		});
	}

	void ForgeScriptRuntime::Start()
	{
		m_Instances.clear();
		m_WarnedEvents.clear();
		m_VM = CreateMachine();

		// Broadcasts from other languages (Python's bellows.emit) reach every ForgeScript 'when' handler.
		// An event for one entity (a UI widget's, on its way up the hierarchy) reaches only that entity's
		// script, with the name of the entity that raised it for 'when "Start" is clicked'.
		m_EventListener = AddScriptEventListener([this](const std::string& event, const ScriptValueList& values, const ScriptEventDelivery& delivery)
		{
			if (!m_VM)
				return;
			std::vector<ForgeScript::Value> converted;
			for (const ScriptValue& value : values)
				converted.push_back(FromScriptValue(*m_VM, value));
			if (!delivery.Target)
			{
				m_VM->EmitAll(event, converted);
				return;
			}
			std::string source;
			if (delivery.Source)
			{
				if (const Entity raisedBy = delivery.Source->TryResolve())
					source = raisedBy.GetName();
			}
			Emit(delivery.Target->ID, event, converted, source);
		});

		// "trigger name" reaches every script, so entities can talk to each other: ForgeScript scripts get
		// the values as they are, and other languages get them through the event hub.
		m_VM->SetTriggerHandler([this](ForgeScript::Module*, const std::string& event, const std::vector<ForgeScript::Value>& values)
		{
			m_VM->EmitAll(event, values);
			ForwardEvent(event, values);
		});
	}

	void ForgeScriptRuntime::ForwardEvent(const std::string& event, const std::vector<ForgeScript::Value>& values)
	{
		if (!m_VM)
			return;
		ScriptValueList converted;
		std::vector<std::string> unsendable;
		for (const ForgeScript::Value& value : values)
			converted.push_back(ToScriptValue(*m_VM, value, unsendable));
		// One warning per event name, so an event sent every frame does not flood the Console.
		if (!unsendable.empty() && m_WarnedEvents.insert(event).second)
			BW_WARN("ForgeScript event '{}': {} cannot be sent to scripts in other languages; they receive nothing in its place", event, unsendable.front());
		try
		{
			EmitScriptEvent(event, converted, m_EventListener);
		}
		catch (const ScriptValueError& error)
		{
			throw ForgeScript::RuntimeError(error.what());
		}
	}

	void ForgeScriptRuntime::Stop()
	{
		if (!m_VM)
			return;

		// Play is ending: a script held by the debugger does not go on, and 'when destroyed' runs without
		// stopping. The runtime keeps its breakpoints for the next session.
		m_VM->StopDebugging();

		std::vector<UUID> entities;
		for (const auto& [uuid, instance] : m_Instances)
		{
			if (instance.Created)
				entities.push_back(uuid);
		}
		for (UUID uuid : entities)
			Emit(uuid, "destroyed", {});

		m_Instances.clear();
		RemoveScriptEventListener(m_EventListener);
		m_EventListener = 0;
		m_VM.reset();
	}

	ForgeScript::Module* ForgeScriptRuntime::LoadModule(Entity entity, const std::string& scriptPath, const std::string& source)
	{
		ForgeScript::Module* module = m_VM->CreateModule(scriptPath);
		m_VM->SetGlobal(module, "self", MakeForgeScriptEntity(*m_VM, ScriptEntity(entity)));
		// Errors in the script's top level reach the reporter through the VM's error handler.
		if (!m_VM->RunInModule(module, source, scriptPath).Success)
		{
			m_VM->UnloadModule(module);
			return nullptr;
		}
		return module;
	}

	void ForgeScriptRuntime::ApplyFields(ForgeScript::Module* module, Entity entity, const std::string& source, const std::vector<std::string>& keep)
	{
		const ScriptComponent& component = entity.GetComponent<ScriptComponent>();
		if (component.Fields.empty())
			return;

		const std::vector<ForgeScript::ScriptVariable> variables = ForgeScript::FindScriptVariables(source);
		for (const auto& [name, value] : component.Fields)
		{
			if (std::find(keep.begin(), keep.end(), name) != keep.end())
				continue;
			const bool declared = std::any_of(variables.begin(), variables.end(), [&name](const ForgeScript::ScriptVariable& variable) { return variable.Name == name; });
			if (declared)
				m_VM->SetGlobal(module, name, FieldToForgeScript(*m_VM, value));
			else
				BW_WARN("Script '{}' has no field '{}' (set on '{}')", component.ScriptPath, name, entity.GetName());
		}
	}

	bool ForgeScriptRuntime::CreateInstance(Entity entity)
	{
		if (!m_VM)
			return false;

		const ScriptComponent& component = entity.GetComponent<ScriptComponent>();
		std::string source;
		if (!ReadScript(component.ScriptPath, source))
		{
			m_Reporter("Script '" + component.ScriptPath + "' on '" + entity.GetName() + "'", "Script file '" + component.ScriptPath + "' was not found");
			return false;
		}

		ForgeScript::Module* module = LoadModule(entity, component.ScriptPath, source);
		if (!module)
			return false;
		// Inspector values replace the script's own starting values, before 'when created' runs.
		ApplyFields(module, entity, source, {});

		Instance instance;
		instance.Owner = ScriptEntity(entity);
		instance.Module = module;
		instance.ScriptPath = component.ScriptPath;
		m_Instances[entity.GetUUID()] = instance;
		return true;
	}

	bool ForgeScriptRuntime::Reload(const std::string& changedPath)
	{
		if (!m_VM || !IsForgeScript(changedPath))
			return true;
		// Reloading unloads the modules the stopped task runs in, which would end the stop under the
		// debugger's feet; the editor applies the change after Continue instead.
		if (m_VM->IsStopped())
		{
			m_Reporter("ForgeScript", "Can't reload '" + changedPath + "' while a script is stopped at a breakpoint; continue first.");
			return false;
		}

		// Scripts loaded with 'use' are shared, so any change can affect every script: read them again.
		m_VM->ClearModuleCache();
		std::unordered_map<std::string, std::string> sources;
		std::unordered_map<std::string, bool> usable;
		for (const auto& [uuid, instance] : m_Instances)
		{
			if (usable.contains(instance.ScriptPath))
				continue;
			std::string source;
			bool ok = ReadScript(instance.ScriptPath, source);
			if (!ok)
			{
				m_Reporter("ForgeScript", "Script file '" + instance.ScriptPath + "' was not found; it keeps running its old version");
			}
			else
			{
				// A script with mistakes keeps running as it was; report each mistake once, not per entity.
				const std::vector<ForgeScript::CompileError> mistakes = m_VM->FindMistakes(source, instance.ScriptPath);
				for (const ForgeScript::CompileError& mistake : mistakes)
				{
					m_Reporter("ForgeScript", Describe(mistake.File, mistake.Line, mistake.Message));
					SendScriptError(mistake.Message, mistake.File, mistake.Line, {});
				}
				ok = mistakes.empty();
			}
			usable[instance.ScriptPath] = ok;
			sources[instance.ScriptPath] = std::move(source);
		}

		bool allReloaded = std::all_of(usable.begin(), usable.end(), [](const auto& entry) { return entry.second; });
		std::vector<UUID> entities;
		for (const auto& [uuid, instance] : m_Instances)
		{
			if (usable[instance.ScriptPath])
				entities.push_back(uuid);
		}
		for (UUID uuid : entities)
		{
			auto it = m_Instances.find(uuid);
			if (it == m_Instances.end())
				continue;
			Entity entity = it->second.Owner.TryResolve();
			if (!entity)
				continue;

			const std::string scriptPath = it->second.ScriptPath;
			ForgeScript::Module* oldModule = it->second.Module;
			ForgeScript::Module* newModule = LoadModule(entity, scriptPath, sources[scriptPath]);
			if (!newModule)
			{
				allReloaded = false;
				continue;
			}

			const std::vector<std::string> carried = m_VM->CarryOverVariables(oldModule, newModule, { "self" });
			// Inspector values still apply to settings the new version adds.
			ApplyFields(newModule, entity, sources[scriptPath], carried);
			m_VM->UnloadModule(oldModule);

			// Loading ran script code, which may have changed the instance map.
			auto again = m_Instances.find(uuid);
			if (again == m_Instances.end())
			{
				m_VM->UnloadModule(newModule);
				continue;
			}
			again->second.Module = newModule;
			again->second.ErrorCount = 0;
			if (again->second.Created)
				Emit(uuid, "reloaded", {});
		}
		return allReloaded;
	}

	void ForgeScriptRuntime::Emit(UUID entity, const std::string& event, const std::vector<ForgeScript::Value>& values, const std::string& source)
	{
		auto it = m_Instances.find(entity);
		if (it == m_Instances.end() || it->second.ErrorCount >= c_MaxErrorsPerInstance)
			return;

		// Copy what we need: handlers may create or destroy entities, which changes the instance map.
		ForgeScript::Module* module = it->second.Module;
		const std::string scriptPath = it->second.ScriptPath;
		if (m_VM->Emit(module, event, values, source))
			return;

		auto again = m_Instances.find(entity);
		if (again == m_Instances.end())
			return;
		if (++again->second.ErrorCount == c_MaxErrorsPerInstance)
		{
			BW_ERROR("Script '{}' on entity {} raised {} errors and has been disabled", scriptPath,
				static_cast<uint64_t>(entity), c_MaxErrorsPerInstance);
		}
	}

	void ForgeScriptRuntime::CallCreated(UUID entity)
	{
		auto it = m_Instances.find(entity);
		if (it == m_Instances.end() || it->second.Created)
			return;
		it->second.Created = true;
		Emit(entity, "created", {});
	}

	void ForgeScriptRuntime::EmitTimed(Scene& scene, const std::string& event, float timestep)
	{
		std::vector<UUID> entities;
		for (const auto& [uuid, instance] : m_Instances)
		{
			if (instance.Created && m_VM->HasHandler(instance.Module, event))
				entities.push_back(uuid);
		}
		for (UUID uuid : entities)
		{
			// A script stopped by the debugger holds the rest of the frame too, as the editor pauses
			// the game right after it.
			if (m_VM->IsStopped())
				return;
			Entity entity = scene.TryGetEntityByUUID(uuid);
			if (entity && !scene.IsPendingDestroy(entity))
				Emit(uuid, event, { ForgeScript::Value::FromNumber(ForgeScriptNumber(timestep)) });
		}
	}

	void ForgeScriptRuntime::Update(Scene& scene, float timestep)
	{
		// A script stopped in the debugger holds the whole game, like the editor's pause.
		if (!m_VM || m_VM->IsStopped())
			return;
		EmitTimed(scene, "update", timestep);
		// Resume every task whose 'wait' has finished.
		m_VM->Update(timestep);
	}

	void ForgeScriptRuntime::FixedUpdate(Scene& scene, float timestep)
	{
		if (m_VM && !m_VM->IsStopped())
			EmitTimed(scene, "physics update", timestep);
	}

	void ForgeScriptRuntime::EntityDestroyed(UUID entity)
	{
		auto it = m_Instances.find(entity);
		if (it == m_Instances.end())
			return;
		const bool created = it->second.Created;
		ForgeScript::Module* module = it->second.Module;
		if (created)
			Emit(entity, "destroyed", {});
		// Look the entity up again: the handler may have created entities, which changes the map.
		m_Instances.erase(entity);
		m_VM->UnloadModule(module);
	}

	void ForgeScriptRuntime::Contact(UUID self, Entity other, ContactEventType type)
	{
		auto it = m_Instances.find(self);
		if (it == m_Instances.end() || !it->second.Created)
			return;

		const char* event = nullptr;
		switch (type)
		{
			case ContactEventType::CollisionEnter: event = "collided"; break;
			case ContactEventType::CollisionExit:  event = "collision ends"; break;
			case ContactEventType::TriggerEnter:   event = "entered"; break;
			case ContactEventType::TriggerExit:    event = "exited"; break;
		}
		if (m_VM->HasHandler(it->second.Module, event))
			Emit(self, event, { MakeForgeScriptEntity(*m_VM, ScriptEntity(other)) });
	}

	bool ForgeScriptRuntime::GetScriptInfo(const std::string& scriptPath, ScriptClassInfo& outInfo, std::string& outError)
	{
		std::string source;
		if (!ReadScript(scriptPath, source))
		{
			outError = "Script file '" + scriptPath + "' was not found";
			return false;
		}

		if (!m_CheckVM)
			m_CheckVM = CreateMachine();
		ForgeScript::CompileError error;
		if (!m_CheckVM->Check(source, scriptPath, error))
		{
			outError = Describe(error.File, error.Line, error.Message);
			return false;
		}

		outInfo.ClassName = std::filesystem::path(scriptPath).stem().string();
		outInfo.Fields.clear();
		for (const ForgeScript::ScriptVariable& variable : ForgeScript::FindScriptVariables(source))
			outInfo.Fields.push_back({ variable.Name, VariableToField(variable) });
		return true;
	}

}
