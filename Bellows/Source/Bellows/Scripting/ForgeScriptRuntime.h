#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include "ForgeScript/ForgeScript.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Bellows::Scripting {

	// Runs ForgeScript (.forge) entity scripts. ScriptEngine owns it and calls it next to the Python
	// scripts, so both languages follow the same play-mode lifecycle.
	//
	// Every play session gets a fresh virtual machine, and every scripted entity its own module with
	// 'self' set to the entity. Engine callbacks become events the script handles with 'when':
	//   created, update (delta), physics update (delta), destroyed,
	//   collided (other), collision ends (other), entered (other), exited (other),
	//   reloaded (after hot reload).
	// 'trigger' events reach every ForgeScript script directly and other languages through the script
	// event hub (EmitScriptEvent), whose events from other languages reach ForgeScript's 'when' handlers.
	class ForgeScriptRuntime
	{
	public:
		// Receives every script error (context, message), like Python errors do.
		using ErrorReporter = std::function<void(const std::string& context, const std::string& message)>;

		explicit ForgeScriptRuntime(ErrorReporter reporter);
		~ForgeScriptRuntime();

		ForgeScriptRuntime(const ForgeScriptRuntime&) = delete;
		ForgeScriptRuntime& operator=(const ForgeScriptRuntime&) = delete;

		static bool IsForgeScript(const std::string& scriptPath);
		// Makes 'use "Scripts/Helpers"' read Assets/Scripts/Helpers.forge (relative to the asset folder).
		// Every machine that compiles game scripts needs it, including the ones that only check them.
		static void InstallModuleLoader(ForgeScript::VM& vm);

		// Play mode lifecycle
		void Start();
		void Stop();
		bool IsRunning() const { return m_VM != nullptr; }

		bool HasInstance(UUID entity) const { return m_Instances.contains(entity); }
		// Loads the entity's script and runs its top level, then applies Inspector values. Errors are
		// reported and return false; 'when created' runs later, from CallCreated.
		bool CreateInstance(Entity entity);
		void CallCreated(UUID entity);
		void Update(Scene& scene, float timestep);
		void FixedUpdate(Scene& scene, float timestep);
		void EntityDestroyed(UUID entity);
		void Contact(UUID self, Entity other, ContactEventType type);

		// Hot reload during play: a .forge file changed on disk. Every ForgeScript instance gets the new
		// code, since scripts loaded with 'use' are shared by all of them. A script with mistakes keeps
		// running its old version and reports the mistakes. Script variables keep their current values
		// (VM::CarryOverVariables), waiting tasks of the old version stop, and 'when reloaded' runs.
		// Returns false when any script kept its old version (mistakes, a missing file, an error in its
		// top level); every problem has been reported then. Paths that are not .forge change nothing.
		// While a script is stopped in the debugger nothing reloads: it reports "Can't reload '<path>'
		// while a script is stopped at a breakpoint; continue first." and returns false.
		bool Reload(const std::string& changedPath);

		// Debugger (the ForgeScript VM's debugger API, see ForgeScript/VM/VM.h). Breakpoints are set by
		// script path (the asset path, as in error messages) and, like the handler, stay set across play
		// sessions: each new machine gets them. While a script is stopped, Update and FixedUpdate send no
		// events, so nothing else runs until the editor continues or steps through GetMachine(). Stop()
		// ends a stop without running the rest of the held code, and 'when destroyed' does not stop.
		void SetBreakpoints(const std::string& scriptPath, const std::vector<int>& lines);
		void ClearBreakpoints();
		void SetDebugHandler(ForgeScript::DebugHandler handler);
		// The play session's machine, for Continue, the steps, GetCallStack, GetVariables and GetGlobals.
		// Null when not playing.
		ForgeScript::VM* GetMachine() const { return m_VM.get(); }
		bool IsStopped() const { return m_VM && m_VM->IsStopped(); }

		// The Inspector fields of a script: its top-level "set name to <value>" lines.
		// Returns false with 'outError' set when the script cannot be read or has a mistake.
		bool GetScriptInfo(const std::string& scriptPath, ScriptClassInfo& outInfo, std::string& outError);

	private:
		struct Instance
		{
			ScriptEntity Owner;
			ForgeScript::Module* Module = nullptr;
			std::string ScriptPath;
			bool Created = false;
			uint32_t ErrorCount = 0;
			std::vector<std::string> Fields;	// Its Inspector fields, watched for 'field changed' once created
		};

		Scope<ForgeScript::VM> CreateMachine();
		// The names of a script's Inspector fields, as GetClassInfo lists them.
		static std::vector<std::string> FieldNames(const std::string& source);
		// Sends a ForgeScript 'trigger' to the other languages' scripts through the event hub.
		void ForwardEvent(const std::string& event, const std::vector<ForgeScript::Value>& values);
		// Runs a script's top level in a new module for 'entity'. Errors are reported; returns nullptr then.
		ForgeScript::Module* LoadModule(Entity entity, const std::string& scriptPath, const std::string& source);
		// Inspector values for the script's settings, except the names in 'keep'.
		void ApplyFields(ForgeScript::Module* module, Entity entity, const std::string& source, const std::vector<std::string>& keep);
		// 'source' is the name of the entity that raised the event, for 'when "Start" is clicked'.
		void Emit(UUID entity, const std::string& event, const std::vector<ForgeScript::Value>& values, const std::string& source = {});
		// Sends an event with the timestep to every created script that handles it, skipping entities about to be destroyed.
		// With 'only', just the entities whose module is in it (the scripts marked Run While Paused).
		void EmitTimed(Scene& scene, const std::string& event, float timestep,
			const std::unordered_set<const ForgeScript::Module*>* only = nullptr);
		bool RunsWhilePaused(Scene& scene, UUID entity) const;

	private:
		ErrorReporter m_Reporter;
		Scope<ForgeScript::VM> m_VM;
		// Checks scripts for the Inspector outside play mode; never runs them.
		Scope<ForgeScript::VM> m_CheckVM;
		std::unordered_map<UUID, Instance> m_Instances;
		// Each instance's module, the other way round, so 'field changed' finds its entity in one lookup.
		std::unordered_map<ForgeScript::Module*, UUID> m_InstanceByModule;
		// "script|field" pairs already warned about a value 'field changed' couldn't carry.
		std::unordered_set<std::string> m_WarnedFields;
		std::unordered_map<std::string, std::vector<int>> m_Breakpoints;
		ForgeScript::DebugHandler m_DebugHandler;
		ScriptEventListenerID m_EventListener = 0;
		std::unordered_set<std::string> m_WarnedEvents;
	};

}
