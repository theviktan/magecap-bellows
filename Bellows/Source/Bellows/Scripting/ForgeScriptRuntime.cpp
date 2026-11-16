#include "bwpch.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"

#include "ForgeScript/Compiler/ScriptVariables.h"

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
		vm->SetErrorHandler([this](const ForgeScript::ScriptError& error) { m_Reporter("ForgeScript", Describe(error)); });
		// use "Scripts/Helpers" loads Assets/Scripts/Helpers.forge.
		vm->SetModuleLoader([](const std::string& path, std::string& outName, std::string& outSource)
		{
			const std::string scriptPath = std::filesystem::path(path).has_extension() ? path : path + ".forge";
			outName = scriptPath;
			return ReadScript(scriptPath, outSource);
		});
		RegisterForgeScriptApi(*vm);
		return vm;
	}

	void ForgeScriptRuntime::Start()
	{
		m_Instances.clear();
		m_VM = CreateMachine();
		ForgeScript::VM* vm = m_VM.get();
		// "trigger name" reaches every script, so entities can talk to each other.
		vm->SetTriggerHandler([vm](ForgeScript::Module*, const std::string& event, const std::vector<ForgeScript::Value>& values)
		{
			vm->EmitAll(event, values);
		});
	}

	void ForgeScriptRuntime::Stop()
	{
		if (!m_VM)
			return;

		std::vector<UUID> entities;
		for (const auto& [uuid, instance] : m_Instances)
		{
			if (instance.Created)
				entities.push_back(uuid);
		}
		for (UUID uuid : entities)
			Emit(uuid, "destroyed", {});

		m_Instances.clear();
		m_VM.reset();
	}

	bool ForgeScriptRuntime::CreateInstance(Entity entity)
	{
		if (!m_VM)
			return false;

		const ScriptComponent& component = entity.GetComponent<ScriptComponent>();
		const std::string context = "Script '" + component.ScriptPath + "' on '" + entity.GetName() + "'";
		std::string source;
		if (!ReadScript(component.ScriptPath, source))
		{
			m_Reporter(context, "Script file '" + component.ScriptPath + "' was not found");
			return false;
		}

		ForgeScript::Module* module = m_VM->CreateModule(component.ScriptPath);
		m_VM->SetGlobal(module, "self", MakeForgeScriptEntity(*m_VM, ScriptEntity(entity)));
		// Errors in the script's top level reach the reporter through the VM's error handler.
		if (!m_VM->RunInModule(module, source, component.ScriptPath).Success)
		{
			m_VM->UnloadModule(module);
			return false;
		}

		// Inspector values replace the script's own starting values, before 'when created' runs.
		if (!component.Fields.empty())
		{
			const std::vector<ForgeScript::ScriptVariable> variables = ForgeScript::FindScriptVariables(source);
			for (const auto& [name, value] : component.Fields)
			{
				const bool declared = std::any_of(variables.begin(), variables.end(), [&name](const ForgeScript::ScriptVariable& variable) { return variable.Name == name; });
				if (declared)
					m_VM->SetGlobal(module, name, FieldToForgeScript(*m_VM, value));
				else
					BW_WARN("Script '{}' has no field '{}' (set on '{}')", component.ScriptPath, name, entity.GetName());
			}
		}

		Instance instance;
		instance.Module = module;
		instance.ScriptPath = component.ScriptPath;
		m_Instances[entity.GetUUID()] = instance;
		return true;
	}

	void ForgeScriptRuntime::Emit(UUID entity, const std::string& event, const std::vector<ForgeScript::Value>& values)
	{
		auto it = m_Instances.find(entity);
		if (it == m_Instances.end() || it->second.ErrorCount >= c_MaxErrorsPerInstance)
			return;

		// Copy what we need: handlers may create or destroy entities, which changes the instance map.
		ForgeScript::Module* module = it->second.Module;
		const std::string scriptPath = it->second.ScriptPath;
		if (m_VM->Emit(module, event, values))
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
			Entity entity = scene.TryGetEntityByUUID(uuid);
			if (entity && !scene.IsPendingDestroy(entity))
				Emit(uuid, event, { ForgeScript::Value::FromNumber(timestep) });
		}
	}

	void ForgeScriptRuntime::Update(Scene& scene, float timestep)
	{
		if (!m_VM)
			return;
		EmitTimed(scene, "update", timestep);
		// Resume every task whose 'wait' has finished.
		m_VM->Update(timestep);
	}

	void ForgeScriptRuntime::FixedUpdate(Scene& scene, float timestep)
	{
		if (m_VM)
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
