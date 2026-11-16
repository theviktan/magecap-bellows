#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include "ForgeScript/ForgeScript.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace Bellows::Scripting {

	// Runs ForgeScript (.forge) entity scripts. ScriptEngine owns it and calls it next to the Python
	// scripts, so both languages follow the same play-mode lifecycle.
	//
	// Every play session gets a fresh virtual machine, and every scripted entity its own module with
	// 'self' set to the entity. Engine callbacks become events the script handles with 'when':
	//   created, update (delta), physics update (delta), destroyed,
	//   collided (other), collision ends (other), entered (other), exited (other).
	class ForgeScriptRuntime
	{
	public:
		// Receives every script error (context, message), like Python errors do.
		using ErrorReporter = std::function<void(const std::string& context, const std::string& message)>;

		explicit ForgeScriptRuntime(ErrorReporter reporter);

		ForgeScriptRuntime(const ForgeScriptRuntime&) = delete;
		ForgeScriptRuntime& operator=(const ForgeScriptRuntime&) = delete;

		static bool IsForgeScript(const std::string& scriptPath);

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

		// The Inspector fields of a script: its top-level "set name to <value>" lines.
		// Returns false with 'outError' set when the script cannot be read or has a mistake.
		bool GetScriptInfo(const std::string& scriptPath, ScriptClassInfo& outInfo, std::string& outError);

	private:
		struct Instance
		{
			ForgeScript::Module* Module = nullptr;
			std::string ScriptPath;
			bool Created = false;
			uint32_t ErrorCount = 0;
		};

		Scope<ForgeScript::VM> CreateMachine();
		void Emit(UUID entity, const std::string& event, const std::vector<ForgeScript::Value>& values);
		// Sends an event with the timestep to every created script that handles it, skipping entities about to be destroyed.
		void EmitTimed(Scene& scene, const std::string& event, float timestep);

	private:
		ErrorReporter m_Reporter;
		Scope<ForgeScript::VM> m_VM;
		// Checks scripts for the Inspector outside play mode; never runs them.
		Scope<ForgeScript::VM> m_CheckVM;
		std::unordered_map<UUID, Instance> m_Instances;
	};

}
