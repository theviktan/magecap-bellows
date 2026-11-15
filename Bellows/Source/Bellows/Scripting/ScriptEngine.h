#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/ScriptField.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Bellows {

	class Scene;
	class Entity;

	// Describes one field a script class exposes to the editor.
	struct ScriptFieldInfo
	{
		std::string Name;
		ScriptFieldValue DefaultValue;
	};

	struct ScriptClassInfo
	{
		std::string ClassName;
		std::vector<ScriptFieldInfo> Fields;
	};

	// Embedded CPython runtime that runs game scripts.
	//
	// Scripts are Python modules inside the project's asset folder. A script class derives from
	// bellows.Script and implements any of: on_create, on_update(dt), on_fixed_update(dt), on_destroy,
	// on_collision_enter(other), on_collision_exit(other), on_trigger_enter(other), on_trigger_exit(other).
	// Script errors are logged with a traceback and never crash the engine.
	class ScriptEngine
	{
	public:
		// Starts the interpreter. 'pythonHome' points at a bundled Python runtime (exported games); empty = system Python.
		static bool Init(const std::filesystem::path& pythonHome = {});
		static void Shutdown();
		static bool IsInitialized();

		// Folder that holds the 'bellows' Python package (engine Resources/Python).
		static void SetEngineModuleDirectory(const std::filesystem::path& directory);

		// Folder next to the executable that holds a bundled Python runtime (exported games).
		// Distinct from the "Python" folder of the bellows package, which Windows would treat as
		// the same folder if only the letter case differed.
		static constexpr const char* BundledRuntimeFolder = "PythonRuntime";

		// Play mode lifecycle
		static void OnRuntimeStart(Scene& scene);
		static void OnRuntimeStop(Scene& scene);
		// Creates instances for scripted entities that appeared since the last call and runs on_create for them.
		static void InstantiatePendingScripts(Scene& scene);
		static void OnUpdate(Scene& scene, float timestep);
		static void OnFixedUpdate(Scene& scene, float timestep);
		static void OnEntityDestroyed(Entity entity);
		static void OnContactEvent(Scene& scene, const ContactEvent& event);

		// Reads the class (and its annotated fields) declared in a script file without running play mode.
		static bool GetClassInfo(const std::string& scriptPath, const std::string& className, ScriptClassInfo& outInfo);

		// Runs Python source in the engine's global namespace (editor automation, agents, tests).
		// Returns false on exception; output and errors are written to 'outOutput'.
		static bool ExecuteSource(const std::string& source, std::string& outOutput, const std::string& filename = "<bellows>");
		static bool ExecuteFile(const std::filesystem::path& path, std::string& outOutput);

		// Number of exceptions raised by scripts since the last reset (tests assert this stays zero).
		static uint32_t GetErrorCount();
		static void ResetErrorCount();
		static const std::string& GetLastError();

		// Python object for an entity's script instance (nullptr if none). Returned as a borrowed PyObject*.
		static void* GetScriptInstance(UUID entity);

		// Adds functions/classes to the native '_bellows' module (the editor adds its automation API this way).
		// The callback receives the module as a PyObject*.
		static void RegisterModuleExtension(const std::function<void(void* module)>& extension);

		// The scene module-level functions (create_entity, find_entity, ...) operate on outside of play mode.
		static void SetContextScene(Scene* scene);
		// What bellows.quit(exit_code) and bellows.load_scene(path) do is decided by the host application.
		static void SetQuitHandler(std::function<void(int)> handler);
		static void SetLoadSceneHandler(std::function<bool(const std::string&)> handler);
	};

}
