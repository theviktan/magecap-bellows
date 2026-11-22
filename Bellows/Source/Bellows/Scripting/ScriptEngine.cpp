#include "bwpch.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/PythonWatchdog.h"
#include "Bellows/Scripting/ScriptGlue.h"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace Bellows {

	namespace {

		// Scripts that raise this many exceptions are disabled to keep the log readable.
		constexpr uint32_t c_MaxErrorsPerInstance = 10;

		struct ScriptInstance
		{
			py::object Object;
			std::string ScriptPath;
			std::string ClassName;
			bool HasUpdate = false;
			bool HasFixedUpdate = false;
			bool HasDestroy = false;
			bool HasCollisionEnter = false;
			bool HasCollisionExit = false;
			bool HasTriggerEnter = false;
			bool HasTriggerExit = false;
			bool HasReload = false;
			bool HasEvent = false;
			bool Created = false;
			uint64_t Order = 0;		// Creation order, which is the order events are delivered in
			uint32_t ErrorCount = 0;
		};

		struct LoadedModule
		{
			py::object Module;
			std::filesystem::file_time_type WriteTime;
		};

		// Configuration that persists across Init/Shutdown.
		std::filesystem::path s_EngineModuleDirectory;
		std::vector<std::function<void(void*)>> s_ModuleExtensions;

		struct ScriptEngineData
		{
			bool Initialized = false;
			std::unordered_map<std::string, LoadedModule> Modules;
			std::unordered_map<UUID, ScriptInstance> Instances;
			std::unordered_set<UUID> FailedEntities;
			py::object ScriptBaseClass;
			// Runs the .forge scripts; entities with Python scripts stay in Instances.
			Scope<Scripting::ForgeScriptRuntime> ForgeScript;
			uint32_t ErrorCount = 0;
			std::string LastError;
			uint64_t NextInstanceOrder = 0;
			Scripting::ScriptEventListenerID EventListener = 0;
		};

		ScriptEngineData* s_Data = nullptr;

		void ReportError(const std::string& context, const std::string& message)
		{
			s_Data->ErrorCount++;
			s_Data->LastError = message;
			BW_ERROR("{}: {}", context, message);
		}

		// The asset path (forward slashes) of a Python file inside the project's assets.
		std::optional<std::string> AssetPathOf(const std::string& file)
		{
			const std::filesystem::path& assets = AssetManager::GetAssetDirectory();
			const std::filesystem::path path(file);
			if (assets.empty() || !path.is_absolute())
				return std::nullopt;
			const std::filesystem::path relative = path.lexically_normal().lexically_relative(assets.lexically_normal());
			if (relative.empty() || *relative.begin() == "..")
				return std::nullopt;
			return relative.generic_string();
		}

		// The asset path of a Python file, or its file name when it is outside the assets (the standard
		// library), so the path never holds a drive letter's ':'.
		std::string ScriptFileForReport(const std::string& file)
		{
			return AssetPathOf(file).value_or(std::filesystem::path(file).filename().string());
		}

		Scripting::ScriptErrorReport DescribePythonError(const py::error_already_set& exception, const std::string& scriptPath)
		{
			Scripting::ScriptErrorReport report;
			report.Language = "python";
			report.File = scriptPath;
			bool foundAssetFrame = false;
			try
			{
				report.Name = py::str(exception.type().attr("__name__"));
				report.Message = py::str(exception.value());

				auto addFrame = [&](const std::string& filename, int line, const std::string& function)
				{
					const std::optional<std::string> assetPath = AssetPathOf(filename);
					const std::string file = assetPath.value_or(std::filesystem::path(filename).filename().string());
					report.Stack.push_back(file + ":" + std::to_string(line) + (function.empty() || function == "<module>" ? "" : " in " + function));
					// The error's place is the innermost call in the project's own scripts.
					if (!foundAssetFrame && assetPath)
					{
						report.File = file;
						report.Line = line;
						foundAssetFrame = true;
					}
				};

				// A syntax error happens in the file being read, which is not a frame of the traceback.
				const py::object value = exception.value();
				if (PyErr_GivenExceptionMatches(exception.type().ptr(), PyExc_SyntaxError) && py::hasattr(value, "filename")
					&& !value.attr("filename").is_none())
				{
					const py::object line = value.attr("lineno");
					addFrame(py::str(value.attr("filename")), line.is_none() ? 0 : line.cast<int>(), "");
					report.Message = value.attr("msg").is_none() ? report.Message : std::string(py::str(value.attr("msg")));
				}

				if (exception.trace())
				{
					py::list frames = py::module_::import("traceback").attr("extract_tb")(exception.trace());
					for (size_t i = frames.size(); i-- > 0; )
					{
						const py::object frame = frames[i];
						const std::string filename = py::str(frame.attr("filename"));
						// Python's own import machinery: not where anything went wrong.
						if (filename.rfind("<frozen ", 0) == 0)
							continue;
						const py::object line = frame.attr("lineno");
						addFrame(filename, line.is_none() ? 0 : line.cast<int>(), py::str(frame.attr("name")));
					}
				}
			}
			catch (const std::exception&)
			{
				// Describing the error must never fail its reporting; what is known so far is sent.
				if (report.Message.empty())
					report.Message = exception.what();
			}
			return report;
		}

		// Logs a script's error and sends it to the 'script error' event. A Python exception carries its
		// name, place and stack; any other error is placed at the script it came from.
		void ReportScriptException(const std::string& context, const std::exception& exception, const std::string& scriptPath)
		{
			ReportError(context, exception.what());
			Scripting::ScriptErrorReport report;
			if (const auto* python = dynamic_cast<const py::error_already_set*>(&exception))
			{
				report = DescribePythonError(*python, scriptPath);
			}
			else
			{
				report.Message = exception.what();
				report.File = scriptPath;
				report.Language = "python";
			}
			Scripting::ReportScriptError(report);
		}

		std::string ModuleNameForPath(const std::string& scriptPath)
		{
			std::string name = "bellows_script_";
			for (char c : scriptPath)
				name += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
			return name;
		}

		// Imports (or re-imports after a change) a project script as a module.
		py::object LoadScriptModule(const std::string& scriptPath)
		{
			const std::filesystem::path absolutePath = AssetManager::GetAbsolutePath(scriptPath);
			std::error_code error;
			const auto writeTime = std::filesystem::last_write_time(absolutePath, error);
			if (error)
				throw std::runtime_error("Script file '" + scriptPath + "' was not found");

			auto it = s_Data->Modules.find(scriptPath);
			if (it != s_Data->Modules.end() && it->second.WriteTime == writeTime)
				return it->second.Module;

			const std::string moduleName = ModuleNameForPath(scriptPath);
			py::module_ importlibUtil = py::module_::import("importlib.util");
			py::object spec = importlibUtil.attr("spec_from_file_location")(moduleName, absolutePath.string());
			if (spec.is_none())
				throw std::runtime_error("Script file '" + scriptPath + "' could not be imported");
			py::object module = importlibUtil.attr("module_from_spec")(spec);
			py::module_::import("sys").attr("modules")[py::str(moduleName)] = module;
			{
				// A script's top level runs here, when play starts or it is reloaded.
				Scripting::PythonRun run;
				spec.attr("loader").attr("exec_module")(module);
			}

			s_Data->Modules[scriptPath] = { module, writeTime };
			return module;
		}

		// The class named 'className', or the first bellows.Script subclass defined in the module.
		py::object FindScriptClass(const py::object& module, const std::string& className)
		{
			py::module_ inspect = py::module_::import("inspect");
			if (!className.empty())
			{
				if (!py::hasattr(module, className.c_str()))
					throw std::runtime_error("Class '" + className + "' was not found in the script");
				return module.attr(className.c_str());
			}

			const std::string moduleName = module.attr("__name__").cast<std::string>();
			for (auto item : module.attr("__dict__").cast<py::dict>())
			{
				py::object value = py::reinterpret_borrow<py::object>(item.second);
				if (!inspect.attr("isclass")(value).cast<bool>())
					continue;
				if (value.attr("__module__").cast<std::string>() != moduleName)
					continue;
				if (py::isinstance(value, py::module_::import("builtins").attr("type"))
					&& PyObject_IsSubclass(value.ptr(), s_Data->ScriptBaseClass.ptr()) == 1)
					return value;
			}
			throw std::runtime_error("The script defines no class deriving from bellows.Script");
		}

		bool Overrides(const py::object& cls, const char* method)
		{
			if (!py::hasattr(cls, method))
				return false;
			return !cls.attr(method).is(s_Data->ScriptBaseClass.attr(method));
		}

		py::object FieldToPython(const ScriptFieldValue& value)
		{
			return std::visit([](const auto& v) -> py::object { return py::cast(v); }, value);
		}

		// Converts a class default/annotation into a field value. Returns false for unsupported types.
		bool PythonToField(const py::object& annotation, const py::object& value, ScriptFieldValue& outValue)
		{
			py::module_ builtins = py::module_::import("builtins");
			py::module_ bellows = py::module_::import("_bellows");

			std::string typeName;
			if (py::isinstance<py::str>(annotation))
				typeName = annotation.cast<std::string>();
			else if (py::hasattr(annotation, "__name__"))
				typeName = annotation.attr("__name__").cast<std::string>();

			if (typeName.rfind("bellows.", 0) == 0)
				typeName = typeName.substr(8);

			try
			{
				if (typeName == "bool") { outValue = value.is_none() ? false : value.cast<bool>(); return true; }
				if (typeName == "int") { outValue = value.is_none() ? int64_t(0) : value.cast<int64_t>(); return true; }
				if (typeName == "float") { outValue = value.is_none() ? 0.0 : value.cast<double>(); return true; }
				if (typeName == "str") { outValue = value.is_none() ? std::string() : value.cast<std::string>(); return true; }
				if (typeName == "Vec2") { outValue = value.is_none() ? glm::vec2(0.0f) : value.cast<glm::vec2>(); return true; }
				if (typeName == "Vec3") { outValue = value.is_none() ? glm::vec3(0.0f) : value.cast<glm::vec3>(); return true; }
				if (typeName == "Vec4") { outValue = value.is_none() ? glm::vec4(1.0f) : value.cast<glm::vec4>(); return true; }
			}
			catch (const py::cast_error&)
			{
				return false;
			}
			return false;
		}

		// Calls 'method' on an instance, isolating any exception.
		template<typename... Args>
		void Invoke(UUID uuid, ScriptInstance& instance, const char* method, Args&&... args)
		{
			if (instance.ErrorCount >= c_MaxErrorsPerInstance)
				return;
			try
			{
				Scripting::PythonRun run;
				instance.Object.attr(method)(std::forward<Args>(args)...);
			}
			catch (const std::exception& exception)
			{
				instance.ErrorCount++;
				ReportScriptException(instance.ClassName + "." + method, exception, instance.ScriptPath);
				if (instance.ErrorCount == c_MaxErrorsPerInstance)
					BW_ERROR("Script '{}' on entity {} raised {} errors and has been disabled", instance.ClassName,
						static_cast<uint64_t>(uuid), c_MaxErrorsPerInstance);
			}
		}

		// Points an instance at its class and notes which callbacks the class implements.
		void UseClass(ScriptInstance& instance, const py::object& cls)
		{
			instance.ClassName = cls.attr("__name__").cast<std::string>();
			instance.HasUpdate = Overrides(cls, "on_update");
			instance.HasFixedUpdate = Overrides(cls, "on_fixed_update");
			instance.HasDestroy = Overrides(cls, "on_destroy");
			instance.HasCollisionEnter = Overrides(cls, "on_collision_enter");
			instance.HasCollisionExit = Overrides(cls, "on_collision_exit");
			instance.HasTriggerEnter = Overrides(cls, "on_trigger_enter");
			instance.HasTriggerExit = Overrides(cls, "on_trigger_exit");
			instance.HasReload = Overrides(cls, "on_reload");
			instance.HasEvent = Overrides(cls, "on_event");
		}

		// Reloads helper modules (imported by name from the asset folder) whose file is 'file'.
		// Returns true when one was found; reload errors are reported and set 'outFailed'.
		bool ReloadHelperModules(const std::filesystem::path& file, bool& outFailed)
		{
			py::module_ importlib = py::module_::import("importlib");
			py::dict modules = py::module_::import("sys").attr("modules");
			std::vector<std::pair<std::string, py::object>> matches;
			for (auto item : modules)
			{
				const std::string name = py::str(item.first);
				py::object module = py::reinterpret_borrow<py::object>(item.second);
				if (name.rfind("bellows_script_", 0) == 0 || !py::hasattr(module, "__file__") || module.attr("__file__").is_none())
					continue;
				std::error_code error;
				if (std::filesystem::equivalent(module.attr("__file__").cast<std::string>(), file, error))
					matches.emplace_back(name, module);
			}
			for (auto& [name, module] : matches)
			{
				try
				{
					Scripting::PythonRun run;
					importlib.attr("reload")(module);
				}
				catch (const py::error_already_set& exception)
				{
					ReportScriptException("Module '" + name + "' was not reloaded", exception, ScriptFileForReport(module.attr("__file__").cast<std::string>()));
					outFailed = true;
				}
			}
			return !matches.empty();
		}

		bool ReloadPythonScripts(const std::string& changedPath)
		{
			const std::filesystem::path file = AssetManager::GetAbsolutePath(changedPath);
			bool failed = false;
			const bool helper = ReloadHelperModules(file, failed);
			if (failed)
				return false;

			// The scripts to run again: the changed one, or every script when a helper they may import changed.
			std::vector<std::string> paths;
			for (const auto& [uuid, instance] : s_Data->Instances)
			{
				if ((helper || instance.ScriptPath == changedPath) && std::find(paths.begin(), paths.end(), instance.ScriptPath) == paths.end())
					paths.push_back(instance.ScriptPath);
			}

			bool allReloaded = true;
			py::object sysModules = py::module_::import("sys").attr("modules");
			for (const std::string& path : paths)
			{
				// Run the module again; on any error the old module stays in use.
				auto old = s_Data->Modules.find(path);
				std::optional<LoadedModule> previous;
				if (old != s_Data->Modules.end())
				{
					previous = old->second;
					s_Data->Modules.erase(old);
				}
				py::object module;
				try
				{
					module = LoadScriptModule(path);
				}
				catch (const std::exception& exception)
				{
					ReportScriptException("Script '" + path + "' kept its old version", exception, path);
					if (previous)
					{
						s_Data->Modules[path] = *previous;
						sysModules[py::str(ModuleNameForPath(path))] = previous->Module;
					}
					allReloaded = false;
					continue;
				}

				std::vector<UUID> uuids;
				for (const auto& [uuid, instance] : s_Data->Instances)
				{
					if (instance.ScriptPath == path)
						uuids.push_back(uuid);
				}
				for (UUID uuid : uuids)
				{
					auto it = s_Data->Instances.find(uuid);
					if (it == s_Data->Instances.end())
						continue;
					try
					{
						if (!py::hasattr(module, it->second.ClassName.c_str()))
							throw std::runtime_error("Class '" + it->second.ClassName + "' is no longer in the script");
						py::object cls = module.attr(it->second.ClassName.c_str());
						// The object keeps its attributes (current values and Inspector fields); only its methods change.
						it->second.Object.attr("__class__") = cls;
						UseClass(it->second, cls);
						it->second.ErrorCount = 0;
					}
					catch (const std::exception& exception)
					{
						ReportScriptException("Script '" + path + "' kept its old version", exception, path);
						allReloaded = false;
						continue;
					}
					if (it->second.Created && it->second.HasReload)
					{
						// Copy the handle: on_reload may spawn entities, which can rehash the instance map.
						ScriptInstance instance = it->second;
						Invoke(uuid, instance, "on_reload");
						if (auto again = s_Data->Instances.find(uuid); again != s_Data->Instances.end())
							again->second.ErrorCount = instance.ErrorCount;
					}
				}
			}
			return allReloaded;
		}

		// Script events (bellows.emit, ForgeScript's trigger) reach on_event(self, name, *values), in the
		// order the scripts were created. As the event hub requires, this never throws: errors are
		// reported as script errors.
		// A broadcast goes to every Python script with on_event, in the order they were created; an event
		// for one entity (a UI widget's, on its way up the hierarchy) only to that entity's script.
		void DispatchEvent(const std::string& name, const Scripting::ScriptValueList& values, const Scripting::ScriptEventDelivery& delivery)
		{
			std::vector<std::pair<uint64_t, UUID>> receivers;
			for (const auto& [uuid, instance] : s_Data->Instances)
			{
				if (instance.HasEvent && instance.Created && (!delivery.Target || delivery.Target->ID == uuid))
					receivers.emplace_back(instance.Order, uuid);
			}
			if (receivers.empty())
				return;
			std::sort(receivers.begin(), receivers.end());

			py::tuple arguments;
			try
			{
				arguments = py::tuple(values.size() + 1);
				arguments[0] = py::str(name);
				for (size_t i = 0; i < values.size(); i++)
					arguments[i + 1] = Scripting::ScriptValueToPython(values[i]);
			}
			catch (const std::exception& exception)
			{
				ReportError("Event '" + name + "'", exception.what());
				return;
			}

			for (const auto& [order, uuid] : receivers)
			{
				auto it = s_Data->Instances.find(uuid);
				if (it == s_Data->Instances.end())
					continue;
				// Copy the handle: a handler may spawn entities, which can rehash the instance map.
				ScriptInstance instance = it->second;
				Invoke(uuid, instance, "on_event", *arguments);
				if (auto again = s_Data->Instances.find(uuid); again != s_Data->Instances.end())
					again->second.ErrorCount = instance.ErrorCount;
			}
		}

		bool CreateInstance(Scene& scene, Entity entity)
		{
			const ScriptComponent& component = entity.GetComponent<ScriptComponent>();
			const UUID uuid = entity.GetUUID();
			if (component.ScriptPath.empty())
				return false;

			if (Scripting::ForgeScriptRuntime::IsForgeScript(component.ScriptPath))
			{
				if (s_Data->ForgeScript->CreateInstance(entity))
					return true;
				s_Data->FailedEntities.insert(uuid);
				return false;
			}

			try
			{
				py::object module = LoadScriptModule(component.ScriptPath);
				py::object cls = FindScriptClass(module, component.ClassName);
				py::object object;
				{
					Scripting::PythonRun run;
					object = cls();
				}
				object.attr("entity") = Scripting::ScriptEntity(entity);

				for (const auto& [name, value] : component.Fields)
				{
					if (py::hasattr(cls, name.c_str()) || py::hasattr(object, name.c_str()))
						object.attr(name.c_str()) = FieldToPython(value);
					else
						BW_WARN("Script '{}' has no field '{}' (set on '{}')", component.ScriptPath, name, entity.GetName());
				}

				ScriptInstance instance;
				instance.Object = object;
				instance.ScriptPath = component.ScriptPath;
				instance.Order = s_Data->NextInstanceOrder++;
				UseClass(instance, cls);
				s_Data->Instances[uuid] = std::move(instance);
				return true;
			}
			catch (const std::exception& exception)
			{
				ReportScriptException("Script '" + component.ScriptPath + "' on '" + entity.GetName() + "'", exception, component.ScriptPath);
			}
			s_Data->FailedEntities.insert(uuid);
			return false;
		}

		void CallCreate(const std::vector<UUID>& created)
		{
			for (UUID uuid : created)
			{
				if (s_Data->ForgeScript->HasInstance(uuid))
				{
					s_Data->ForgeScript->CallCreated(uuid);
					continue;
				}
				auto it = s_Data->Instances.find(uuid);
				if (it == s_Data->Instances.end() || it->second.Created)
					continue;
				it->second.Created = true;
				// Copy the handle: on_create may spawn entities, which can rehash the instance map.
				ScriptInstance instance = it->second;
				Invoke(uuid, instance, "on_create");
				if (auto again = s_Data->Instances.find(uuid); again != s_Data->Instances.end())
					again->second.ErrorCount = instance.ErrorCount;
			}
		}

		const char* c_PythonBootstrap = R"PY(
import sys

class _BellowsLogWriter:
    """Routes print() output to the MageCap Bellows console."""
    def __init__(self, level):
        self._level = level
        self._buffer = ""
    def write(self, text):
        self._buffer += str(text)
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            if line.strip():
                import _bellows
                _bellows._log_line(self._level, line)
        return len(text)
    def flush(self):
        if self._buffer.strip():
            import _bellows
            _bellows._log_line(self._level, self._buffer)
        self._buffer = ""
    def isatty(self):
        return False

sys.stdout = _BellowsLogWriter(0)
sys.stderr = _BellowsLogWriter(2)
)PY";

		// The game's asset folder goes last on Python's import path, after the engine's modules and
		// the standard library: a game file can't stand in for them (a json.py in Assets would
		// otherwise run whenever anything imports json). A file named like the engine's own modules is
		// never imported, so it gets a warning.
		void AddAssetDirectoryToPath()
		{
			const std::filesystem::path assetDirectory = AssetManager::GetAssetDirectory();
			if (assetDirectory.empty())
				return;
			py::list path = py::module_::import("sys").attr("path");
			const std::string folder = assetDirectory.string();
			if (path.contains(folder))
				return;
			path.append(folder);

			for (const char* name : { "bellows.py", "bellows", "_bellows.py" })
			{
				std::error_code error;
				if (std::filesystem::exists(assetDirectory / name, error))
					BW_CORE_WARN("Assets/{} has the name of one of MageCap Bellows' own Python modules, so it is never imported. Rename it.", name);
			}
		}

	}

	bool ScriptEngine::Init(const std::filesystem::path& pythonHome)
	{
		if (s_Data)
			return true;

		Scripting::LinkBindings();
		s_Data = new ScriptEngineData();
		s_Data->ForgeScript = CreateScope<Scripting::ForgeScriptRuntime>(ReportError);
		s_Data->EventListener = Scripting::AddScriptEventListener(DispatchEvent);
		const std::filesystem::path engineModuleDirectory = s_EngineModuleDirectory.empty()
			? Platform::GetExecutableDirectory() / "Python" : s_EngineModuleDirectory;

		// Python runtime location: explicit argument, a bundled runtime next to the executable
		// (exported games), or the Python the engine was built against.
		std::filesystem::path home = pythonHome;
		if (home.empty())
		{
			const std::filesystem::path bundled = Platform::GetExecutableDirectory() / BundledRuntimeFolder;
			if (std::filesystem::exists(bundled))
				home = bundled;
		}
#ifdef BELLOWS_PYTHON_HOME
		if (home.empty() && std::filesystem::exists(BELLOWS_PYTHON_HOME))
			home = BELLOWS_PYTHON_HOME;
#endif

		PyConfig config;
		PyConfig_InitIsolatedConfig(&config);
		config.install_signal_handlers = 0;
		config.parse_argv = 0;
		config.write_bytecode = 0;
		config.user_site_directory = 0;
		if (!home.empty())
		{
			const std::wstring homeString = home.wstring();
			PyStatus status = PyConfig_SetString(&config, &config.home, homeString.c_str());
			if (PyStatus_Exception(status))
			{
				PyConfig_Clear(&config);
				BW_CORE_CRITICAL("Failed to configure the Python home '{}'", home.string());
				delete s_Data;
				s_Data = nullptr;
				return false;
			}
		}

		try
		{
			py::initialize_interpreter(&config, 0, nullptr, false);
		}
		catch (const std::exception& exception)
		{
			BW_CORE_CRITICAL("Failed to start the Python interpreter: {}", exception.what());
			delete s_Data;
			s_Data = nullptr;
			return false;
		}

		try
		{
			py::module_ sys = py::module_::import("sys");
			sys.attr("path").attr("insert")(0, engineModuleDirectory.string());
			AddAssetDirectoryToPath();
			sys.attr("dont_write_bytecode") = true;

			py::exec(c_PythonBootstrap);

			py::module_ native = py::module_::import("_bellows");
			for (const auto& extension : s_ModuleExtensions)
				extension(native.ptr());

			py::module_ bellows = py::module_::import("bellows");
			s_Data->ScriptBaseClass = bellows.attr("Script");
			// Automation code (ExecuteSource) can use 'bellows' without importing it.
			py::module_::import("__main__").attr("bellows") = bellows;
		}
		catch (const std::exception& exception)
		{
			BW_CORE_CRITICAL("Failed to import the 'bellows' Python package from '{}': {}", engineModuleDirectory.string(), exception.what());
			s_Data->ScriptBaseClass = py::object();
			py::finalize_interpreter();
			delete s_Data;
			s_Data = nullptr;
			return false;
		}

		s_Data->Initialized = true;
		Scripting::PythonWatchdog::Start();
		const std::string version = py::module_::import("sys").attr("version").cast<std::string>();
		BW_CORE_INFO("Python {} ready", version.substr(0, version.find(' ')));
		return true;
	}

	void ScriptEngine::Shutdown()
	{
		if (!s_Data)
			return;

		Scripting::PythonWatchdog::Stop();
		Scripting::RemoveScriptEventListener(s_Data->EventListener);
		s_Data->ForgeScript->Stop();
		s_Data->Instances.clear();
		s_Data->Modules.clear();
		s_Data->ScriptBaseClass = py::object();
		Scripting::SetContextScene(nullptr);
		try
		{
			py::module_::import("sys").attr("stdout").attr("flush")();
		}
		catch (const std::exception&)
		{
		}
		py::finalize_interpreter();
		delete s_Data;
		s_Data = nullptr;
	}

	bool ScriptEngine::IsInitialized()
	{
		return s_Data && s_Data->Initialized;
	}

	Scripting::ForgeScriptRuntime* ScriptEngine::GetForgeScriptRuntime()
	{
		return s_Data ? s_Data->ForgeScript.get() : nullptr;
	}

	void ScriptEngine::SetEngineModuleDirectory(const std::filesystem::path& directory)
	{
		s_EngineModuleDirectory = directory;
	}

	void ScriptEngine::RegisterModuleExtension(const std::function<void(void*)>& extension)
	{
		s_ModuleExtensions.push_back(extension);
		if (IsInitialized())
			extension(py::module_::import("_bellows").ptr());
	}

	void ScriptEngine::SetContextScene(Scene* scene)
	{
		Scripting::SetContextScene(scene);
	}

	void ScriptEngine::SetQuitHandler(std::function<void(int)> handler)
	{
		Scripting::SetQuitHandler(std::move(handler));
	}

	void ScriptEngine::SetLoadSceneHandler(std::function<bool(const std::string&)> handler)
	{
		Scripting::SetLoadSceneHandler(std::move(handler));
	}

	void ScriptEngine::SetScreenshotHandler(std::function<std::optional<Image>()> handler)
	{
		Scripting::SetScreenshotHandler(std::move(handler));
	}

	void ScriptEngine::OnRuntimeStart(Scene& scene)
	{
		if (!IsInitialized())
			return;

		Scripting::SetContextScene(&scene);
		Scripting::SetFrameTiming(0.0f, 0.0f, 0);
		s_Data->Instances.clear();
		s_Data->FailedEntities.clear();
		s_Data->ForgeScript->Start();

		// Make sure the project folder is importable and edited scripts are reloaded.
		try
		{
			AddAssetDirectoryToPath();
			py::module_::import("importlib").attr("invalidate_caches")();
		}
		catch (const std::exception& exception)
		{
			ReportError("ScriptEngine", exception.what());
		}

		InstantiatePendingScripts(scene);
	}

	void ScriptEngine::OnRuntimeStop(Scene& scene)
	{
		if (!IsInitialized())
			return;

		std::vector<UUID> uuids;
		for (const auto& [uuid, instance] : s_Data->Instances)
			uuids.push_back(uuid);
		for (UUID uuid : uuids)
		{
			auto it = s_Data->Instances.find(uuid);
			if (it != s_Data->Instances.end() && it->second.HasDestroy && it->second.Created)
				Invoke(uuid, it->second, "on_destroy");
		}
		s_Data->Instances.clear();
		s_Data->FailedEntities.clear();
		s_Data->ForgeScript->Stop();

		// Run Python's garbage collector so script objects release resources now.
		try
		{
			py::module_::import("gc").attr("collect")();
		}
		catch (const std::exception&)
		{
		}

		if (Scripting::GetContextScene() == &scene)
			Scripting::SetContextScene(nullptr);
	}

	bool ScriptEngine::ReloadScripts(const std::string& scriptPath)
	{
		if (!IsInitialized())
			return true;
		if (Scripting::ForgeScriptRuntime::IsForgeScript(scriptPath))
			return s_Data->ForgeScript->Reload(scriptPath);
		if (std::filesystem::path(scriptPath).extension() != ".py")
			return true;
		try
		{
			return ReloadPythonScripts(scriptPath);
		}
		catch (const std::exception& exception)
		{
			ReportScriptException("Reloading '" + scriptPath + "'", exception, scriptPath);
			return false;
		}
	}

	void ScriptEngine::InstantiatePendingScripts(Scene& scene)
	{
		if (!IsInitialized())
			return;

		std::vector<UUID> created;
		auto view = scene.GetAllEntitiesWith<ScriptComponent>();
		std::vector<Entity> pending;
		for (entt::entity handle : view)
		{
			Entity entity(handle, &scene);
			const UUID uuid = entity.GetUUID();
			if (!s_Data->Instances.contains(uuid) && !s_Data->ForgeScript->HasInstance(uuid) && !s_Data->FailedEntities.contains(uuid) && !scene.IsPendingDestroy(entity))
				pending.push_back(entity);
		}

		for (Entity entity : pending)
		{
			if (CreateInstance(scene, entity))
				created.push_back(entity.GetUUID());
		}
		CallCreate(created);
	}

	void ScriptEngine::OnUpdate(Scene& scene, float timestep)
	{
		if (!IsInitialized())
			return;

		Scripting::SetContextScene(&scene);
		Scripting::SetFrameTiming(timestep, scene.GetTime(), scene.GetFrameCount());

		// While the game is paused only scripts marked Run While Paused update.
		const bool paused = scene.IsGamePaused();
		std::vector<UUID> uuids;
		uuids.reserve(s_Data->Instances.size());
		for (const auto& [uuid, instance] : s_Data->Instances)
		{
			if (instance.HasUpdate && instance.Created)
				uuids.push_back(uuid);
		}

		for (UUID uuid : uuids)
		{
			auto it = s_Data->Instances.find(uuid);
			if (it == s_Data->Instances.end())
				continue;
			Entity entity = scene.TryGetEntityByUUID(uuid);
			if (!entity || scene.IsPendingDestroy(entity))
				continue;
			if (paused)
			{
				const auto* script = entity.TryGetComponent<ScriptComponent>();
				if (!script || !script->RunWhilePaused)
					continue;
			}
			ScriptInstance instance = it->second;
			Invoke(uuid, instance, "on_update", timestep);
			if (auto again = s_Data->Instances.find(uuid); again != s_Data->Instances.end())
				again->second.ErrorCount = instance.ErrorCount;
		}

		// ForgeScript applies the same Run While Paused rule to its updates and waits.
		s_Data->ForgeScript->Update(scene, timestep);
	}

	void ScriptEngine::OnFixedUpdate(Scene& scene, float timestep)
	{
		if (!IsInitialized())
			return;

		std::vector<UUID> uuids;
		for (const auto& [uuid, instance] : s_Data->Instances)
		{
			if (instance.HasFixedUpdate && instance.Created)
				uuids.push_back(uuid);
		}
		for (UUID uuid : uuids)
		{
			auto it = s_Data->Instances.find(uuid);
			if (it == s_Data->Instances.end())
				continue;
			Entity entity = scene.TryGetEntityByUUID(uuid);
			if (!entity || scene.IsPendingDestroy(entity))
				continue;
			ScriptInstance instance = it->second;
			Invoke(uuid, instance, "on_fixed_update", timestep);
			if (auto again = s_Data->Instances.find(uuid); again != s_Data->Instances.end())
				again->second.ErrorCount = instance.ErrorCount;
		}

		s_Data->ForgeScript->FixedUpdate(scene, timestep);
	}

	void ScriptEngine::OnEntityDestroyed(Entity entity)
	{
		if (!IsInitialized())
			return;

		const UUID uuid = entity.GetUUID();
		if (s_Data->ForgeScript->HasInstance(uuid))
		{
			s_Data->ForgeScript->EntityDestroyed(uuid);
			return;
		}
		auto it = s_Data->Instances.find(uuid);
		if (it == s_Data->Instances.end())
			return;

		ScriptInstance instance = std::move(it->second);
		s_Data->Instances.erase(it);
		if (instance.HasDestroy && instance.Created)
			Invoke(uuid, instance, "on_destroy");
	}

	void ScriptEngine::OnContactEvent(Scene& scene, const ContactEvent& event)
	{
		if (!IsInitialized())
			return;

		auto dispatch = [&](UUID self, UUID other)
		{
			if (s_Data->ForgeScript->HasInstance(self))
			{
				if (Entity otherEntity = scene.TryGetEntityByUUID(other))
					s_Data->ForgeScript->Contact(self, otherEntity, event.Type);
				return;
			}
			auto it = s_Data->Instances.find(self);
			if (it == s_Data->Instances.end() || !it->second.Created)
				return;
			Entity otherEntity = scene.TryGetEntityByUUID(other);
			if (!otherEntity)
				return;

			const char* method = nullptr;
			switch (event.Type)
			{
				case ContactEventType::CollisionEnter:	if (it->second.HasCollisionEnter) method = "on_collision_enter"; break;
				case ContactEventType::CollisionExit:	if (it->second.HasCollisionExit) method = "on_collision_exit"; break;
				case ContactEventType::TriggerEnter:	if (it->second.HasTriggerEnter) method = "on_trigger_enter"; break;
				case ContactEventType::TriggerExit:		if (it->second.HasTriggerExit) method = "on_trigger_exit"; break;
			}
			if (!method)
				return;

			ScriptInstance instance = it->second;
			Invoke(self, instance, method, Scripting::ScriptEntity(otherEntity));
			if (auto again = s_Data->Instances.find(self); again != s_Data->Instances.end())
				again->second.ErrorCount = instance.ErrorCount;
		};

		dispatch(event.EntityA, event.EntityB);
		dispatch(event.EntityB, event.EntityA);
	}

	bool ScriptEngine::GetClassInfo(const std::string& scriptPath, const std::string& className, ScriptClassInfo& outInfo)
	{
		if (!IsInitialized() || scriptPath.empty())
			return false;
		if (Scripting::ForgeScriptRuntime::IsForgeScript(scriptPath))
			return s_Data->ForgeScript->GetScriptInfo(scriptPath, outInfo, s_Data->LastError);

		try
		{
			py::object module = LoadScriptModule(scriptPath);
			py::object cls = FindScriptClass(module, className);
			outInfo.ClassName = cls.attr("__name__").cast<std::string>();
			outInfo.Fields.clear();

			// Walk the class hierarchy from the most-derived class, collecting annotated fields once.
			py::module_ inspect = py::module_::import("inspect");
			std::unordered_set<std::string> seen;
			for (auto base : cls.attr("__mro__"))
			{
				py::object baseClass = py::reinterpret_borrow<py::object>(base);
				if (baseClass.is(s_Data->ScriptBaseClass) || baseClass.is(py::module_::import("builtins").attr("object")))
					break;
				py::dict annotations = inspect.attr("get_annotations")(baseClass);
				for (auto item : annotations)
				{
					const std::string name = item.first.cast<std::string>();
					if (name.empty() || name[0] == '_' || name == "entity" || seen.contains(name))
						continue;
					seen.insert(name);
					py::object value = py::hasattr(cls, name.c_str()) ? py::object(cls.attr(name.c_str())) : py::object(py::none());
					ScriptFieldValue fieldValue;
					if (PythonToField(py::reinterpret_borrow<py::object>(item.second), value, fieldValue))
						outInfo.Fields.push_back({ name, fieldValue });
				}
			}
			return true;
		}
		catch (const py::error_already_set& exception)
		{
			s_Data->LastError = exception.what();
		}
		catch (const std::exception& exception)
		{
			s_Data->LastError = exception.what();
		}
		return false;
	}

	bool ScriptEngine::ExecuteSource(const std::string& source, std::string& outOutput, const std::string& filename)
	{
		outOutput.clear();
		if (!IsInitialized())
		{
			outOutput = "Python is not initialized";
			return false;
		}

		py::module_ sys = py::module_::import("sys");
		py::object previousStdout = sys.attr("stdout");
		py::object previousStderr = sys.attr("stderr");
		py::object buffer = py::module_::import("io").attr("StringIO")();
		sys.attr("stdout") = buffer;
		sys.attr("stderr") = buffer;

		bool success = true;
		try
		{
			py::object mainModule = py::module_::import("__main__");
			py::object globals = mainModule.attr("__dict__");
			py::object builtins = py::module_::import("builtins");

			// Like an interactive prompt, a trailing expression statement prints its value.
			py::module_ ast = py::module_::import("ast");
			py::object tree = ast.attr("parse")(source, filename, "exec");
			py::list body = tree.attr("body");
			py::object lastExpression = py::none();
			if (py::len(body) > 0 && py::isinstance(body[py::len(body) - 1], ast.attr("Expr")))
			{
				lastExpression = ast.attr("Expression")(body.attr("pop")().attr("value"));
				ast.attr("fix_missing_locations")(lastExpression);
			}
			// The Console and agent requests are watched: a loop with no end is stopped, not the editor.
			Scripting::PythonRun run;
			builtins.attr("exec")(builtins.attr("compile")(tree, filename, "exec"), globals);
			if (!lastExpression.is_none())
			{
				py::object result = builtins.attr("eval")(builtins.attr("compile")(lastExpression, filename, "eval"), globals);
				if (!result.is_none())
					buffer.attr("write")(py::repr(result).cast<std::string>() + "\n");
			}
		}
		catch (const py::error_already_set& exception)
		{
			success = false;
			buffer.attr("write")(std::string(exception.what()) + "\n");
			s_Data->LastError = exception.what();
		}
		catch (const std::exception& exception)
		{
			success = false;
			buffer.attr("write")(std::string(exception.what()) + "\n");
			s_Data->LastError = exception.what();
		}

		outOutput = buffer.attr("getvalue")().cast<std::string>();
		sys.attr("stdout") = previousStdout;
		sys.attr("stderr") = previousStderr;
		return success;
	}

	bool ScriptEngine::ExecuteFile(const std::filesystem::path& path, std::string& outOutput)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
		{
			outOutput = "Could not open '" + path.string() + "'";
			return false;
		}
		std::ostringstream contents;
		contents << stream.rdbuf();
		if (!IsInitialized())
			return ExecuteSource(contents.str(), outOutput, path.string());

		// Like `python file.py`, the script can find files next to itself through __file__.
		py::dict globals = py::module_::import("__main__").attr("__dict__");
		const bool hadFile = globals.contains("__file__");
		py::object previousFile = hadFile ? py::object(globals["__file__"]) : py::object();
		globals["__file__"] = std::filesystem::absolute(path).string();
		const bool success = ExecuteSource(contents.str(), outOutput, path.string());
		if (hadFile)
			globals["__file__"] = previousFile;
		else if (globals.contains("__file__"))
			PyDict_DelItemString(globals.ptr(), "__file__");
		return success;
	}

	uint32_t ScriptEngine::GetErrorCount()
	{
		return s_Data ? s_Data->ErrorCount : 0;
	}

	void ScriptEngine::ResetErrorCount()
	{
		if (s_Data)
			s_Data->ErrorCount = 0;
	}

	const std::string& ScriptEngine::GetLastError()
	{
		static const std::string s_Empty;
		return s_Data ? s_Data->LastError : s_Empty;
	}

	void* ScriptEngine::GetScriptInstance(UUID entity)
	{
		if (!IsInitialized())
			return nullptr;
		auto it = s_Data->Instances.find(entity);
		return it != s_Data->Instances.end() ? it->second.Object.ptr() : nullptr;
	}

}
