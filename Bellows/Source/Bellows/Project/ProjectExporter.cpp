#include "bwpch.h"
#include "Bellows/Project/ProjectExporter.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Scripting/ScriptEngine.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <pybind11/embed.h>

namespace py = pybind11;

namespace Bellows {

	namespace {

		// Standard-library folders a game never needs.
		const std::unordered_set<std::string> c_ExcludedPythonFolders = {
			"test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "site-packages", "dist-packages",
			"__pycache__", "lib2to3", "pydoc_data", "venv",
		};

		bool IsExcluded(const std::filesystem::path& relative)
		{
			for (const auto& part : relative)
			{
				const std::string name = part.string();
				if (c_ExcludedPythonFolders.contains(name) || name.rfind("config-", 0) == 0)
					return true;
			}
			const std::string extension = relative.extension().string();
			return extension == ".pyc" || extension == ".a" || extension == ".exe";
		}

		bool CopyFiltered(const std::filesystem::path& source, const std::filesystem::path& destination,
			const std::function<bool(const std::filesystem::path&)>& exclude, uint64_t& totalBytes)
		{
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(source, std::filesystem::directory_options::skip_permission_denied, error);
				it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (error)
					return false;
				const std::filesystem::path relative = std::filesystem::relative(it->path(), source, error);
				if (exclude(relative))
				{
					if (it->is_directory(error))
						it.disable_recursion_pending();
					continue;
				}

				const std::filesystem::path target = destination / relative;
				if (it->is_directory(error))
				{
					std::filesystem::create_directories(target, error);
				}
				else if (it->is_regular_file(error))
				{
					std::filesystem::create_directories(target.parent_path(), error);
					std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, error);
					if (error)
					{
						BW_CORE_ERROR("Export: could not copy '{}': {}", it->path().string(), error.message());
						return false;
					}
					totalBytes += it->file_size(error);
				}
			}
			return true;
		}

		bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& destination, uint64_t& totalBytes)
		{
			std::error_code error;
			std::filesystem::create_directories(destination.parent_path(), error);
			std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
			if (error)
			{
				BW_CORE_ERROR("Export: could not copy '{}' to '{}': {}", source.string(), destination.string(), error.message());
				return false;
			}
			totalBytes += std::filesystem::file_size(destination, error);
			return true;
		}

		struct PythonRuntimeInfo
		{
			std::filesystem::path StandardLibrary;
			std::filesystem::path PlatformLibrary;		// Windows: DLLs folder
			std::vector<std::filesystem::path> SharedLibraries;
			std::string Version;
		};

		bool QueryPythonRuntime(PythonRuntimeInfo& info)
		{
			if (!ScriptEngine::IsInitialized())
				return false;
			try
			{
				py::module_ sysconfig = py::module_::import("sysconfig");
				py::module_ sys = py::module_::import("sys");
				info.StandardLibrary = sysconfig.attr("get_paths")()["stdlib"].cast<std::string>();
				info.Version = py::str(sys.attr("version_info").attr("major")).cast<std::string>() + "."
					+ py::str(sys.attr("version_info").attr("minor")).cast<std::string>();
				const std::filesystem::path prefix = sys.attr("base_prefix").cast<std::string>();
#if defined(BW_PLATFORM_WINDOWS)
				info.PlatformLibrary = prefix / "DLLs";
				// The Python and C++ runtime DLLs sit in the Python installation (development builds)
				// or next to the executable (the downloadable editor, whose Python is bundled).
				std::unordered_set<std::string> found;
				for (const std::filesystem::path& directory : { prefix, Platform::GetExecutableDirectory() })
				{
					std::error_code error;
					for (const auto& entry : std::filesystem::directory_iterator(directory, error))
					{
						const std::string name = entry.path().filename().string();
						const bool runtimeLibrary = name.rfind("python3", 0) == 0 || name.rfind("vcruntime", 0) == 0 || name.rfind("msvcp", 0) == 0;
						if (entry.path().extension() == ".dll" && runtimeLibrary && found.insert(name).second)
							info.SharedLibraries.push_back(entry.path());
					}
				}
#else
				py::object libraryDirectory = sysconfig.attr("get_config_var")("LIBDIR");
				py::object libraryName = sysconfig.attr("get_config_var")("INSTSONAME");
				if (libraryName.is_none())
					libraryName = sysconfig.attr("get_config_var")("LDLIBRARY");
				if (!libraryDirectory.is_none() && !libraryName.is_none())
				{
					// LIBDIR is where Python was built to live; a bundled runtime keeps the library in <prefix>/lib.
					std::filesystem::path library = std::filesystem::path(libraryDirectory.cast<std::string>()) / libraryName.cast<std::string>();
					if (!FileSystem::Exists(library))
						library = prefix / "lib" / libraryName.cast<std::string>();
					info.SharedLibraries.push_back(library);
				}
#endif
				return true;
			}
			catch (const std::exception& exception)
			{
				BW_CORE_ERROR("Export: could not inspect the Python runtime: {}", exception.what());
				return false;
			}
		}

	}

	std::string ProjectExporter::SanitizeName(const std::string& name)
	{
		std::string result;
		for (char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ')
				result += c;
		}
		while (!result.empty() && result.back() == ' ')
			result.pop_back();
		return result.empty() ? std::string("Game") : result;
	}

	ExportResult ProjectExporter::Export(const Project& project, const ExportOptions& options)
	{
		ExportResult result;
		auto fail = [&result](const std::string& message)
		{
			BW_CORE_ERROR("Export failed: {}", message);
			result.Messages.push_back("Error: " + message);
			result.Success = false;
			return result;
		};
		auto note = [&result](const std::string& message)
		{
			BW_CORE_INFO("Export: {}", message);
			result.Messages.push_back(message);
		};

		if (options.OutputDirectory.empty())
			return fail("No output folder was given");

		std::error_code error;
		const std::filesystem::path output = std::filesystem::absolute(options.OutputDirectory, error);
		const std::filesystem::path projectDirectory = std::filesystem::weakly_canonical(project.GetProjectDirectory(), error);
		const std::string relativeToProject = FileSystem::MakeRelativeGenericPath(output, project.GetAssetDirectory());
		if (!relativeToProject.empty() || std::filesystem::weakly_canonical(output, error) == projectDirectory)
			return fail("The export folder must not be the project folder or inside the asset folder");

		const std::filesystem::path startScene = project.GetAssetDirectory() / project.GetConfig().StartScene;
		if (!FileSystem::Exists(startScene))
			return fail("The start scene '" + project.GetConfig().StartScene + "' does not exist");

		const std::filesystem::path engineDirectory = Platform::GetExecutableDirectory();
		const std::filesystem::path runtime = engineDirectory / ("BellowsRuntime" + Platform::GetExecutableExtension());
		if (!FileSystem::Exists(runtime))
			return fail("The runtime executable was not found next to the editor (" + runtime.string() + ")");

		// Start from a clean folder so no stale files ship.
		for (const char* folder : { "Assets", "Resources", "Shaders", "Python", ScriptEngine::BundledRuntimeFolder })
			std::filesystem::remove_all(output / folder, error);
		if (!FileSystem::CreateDirectories(output))
			return fail("Could not create '" + output.string() + "'");

		const std::string gameName = SanitizeName(project.GetConfig().Name);
		result.ExecutablePath = output / (gameName + Platform::GetExecutableExtension());
		if (!CopyFile(runtime, result.ExecutablePath, result.TotalBytes))
			return fail("Could not copy the runtime executable");
#if !defined(BW_PLATFORM_WINDOWS)
		std::filesystem::permissions(result.ExecutablePath,
			std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
			std::filesystem::perm_options::add, error);
#endif
		note("Runtime: " + result.ExecutablePath.filename().string());

		auto noExclusions = [](const std::filesystem::path& relative)
		{
			return relative.filename() == "__pycache__" || relative.extension() == ".pyc";
		};
		if (!CopyFiltered(engineDirectory / "Shaders", output / "Shaders", noExclusions, result.TotalBytes))
			return fail("Could not copy shaders");
		if (!CopyFiltered(engineDirectory / "Python", output / "Python", noExclusions, result.TotalBytes))
			return fail("Could not copy the bellows Python package");
		// Text components with no font use the built-in one; its license (OFL.txt) is in the same folder.
		const std::filesystem::path builtinFont = std::filesystem::path("Resources") / "Fonts" / "Inter";
		if (!CopyFiltered(engineDirectory / builtinFont, output / builtinFont, noExclusions, result.TotalBytes))
			return fail("Could not copy the built-in font");

		// Open-source license notices travel with every build of the game.
		if (const std::filesystem::path notices = engineDirectory / "ThirdPartyNotices.md"; FileSystem::Exists(notices))
		{
			std::error_code copyError;
			std::filesystem::copy_file(notices, output / "ThirdPartyNotices.md", std::filesystem::copy_options::overwrite_existing, copyError);
			if (copyError)
				note("Warning: could not copy ThirdPartyNotices.md (" + copyError.message() + ")");
		}

		auto assetExclusions = [](const std::filesystem::path& relative)
		{
			const std::string first = relative.begin() != relative.end() ? relative.begin()->string() : std::string();
			return first == ".bellows" || relative.filename() == "__pycache__" || relative.extension() == ".pyc";
		};
		if (!CopyFiltered(project.GetAssetDirectory(), output / "Assets", assetExclusions, result.TotalBytes))
			return fail("Could not copy project assets");
		note("Assets copied");

		// The exported project file always points at the bundled Assets folder.
		{
			const std::optional<std::string> projectText = FileSystem::ReadTextFile(project.GetProjectFilePath());
			Json json = projectText ? Json::parse(*projectText, nullptr, false) : Json();
			if (json.is_discarded() || !json.is_object())
				json = Json::object();
			json["Name"] = project.GetConfig().Name;
			json["StartScene"] = project.GetConfig().StartScene;
			json["AssetDirectory"] = "Assets";
			if (!FileSystem::WriteTextFile(output / (gameName + Project::FileExtension), json.dump(1, '\t')))
				return fail("Could not write the project file");
		}

		if (options.IncludePythonRuntime)
		{
			PythonRuntimeInfo python;
			if (!QueryPythonRuntime(python))
				return fail("The Python runtime could not be located");

#if defined(BW_PLATFORM_WINDOWS)
			const std::filesystem::path pythonHome = output / ScriptEngine::BundledRuntimeFolder;
			if (!CopyFiltered(python.StandardLibrary, pythonHome / "Lib", IsExcluded, result.TotalBytes))
				return fail("Could not copy the Python standard library");
			if (FileSystem::Exists(python.PlatformLibrary)
				&& !CopyFiltered(python.PlatformLibrary, pythonHome / "DLLs", IsExcluded, result.TotalBytes))
				return fail("Could not copy Python extension modules");
			for (const std::filesystem::path& library : python.SharedLibraries)
			{
				if (!CopyFile(library, output / library.filename(), result.TotalBytes))
					return fail("Could not copy " + library.filename().string());
			}
#else
			const std::filesystem::path pythonHome = output / ScriptEngine::BundledRuntimeFolder;
			if (!CopyFiltered(python.StandardLibrary, pythonHome / "lib" / ("python" + python.Version), IsExcluded, result.TotalBytes))
				return fail("Could not copy the Python standard library");
			for (const std::filesystem::path& library : python.SharedLibraries)
			{
				if (!FileSystem::Exists(library))
					return fail("Python library '" + library.string() + "' was not found");
				if (!CopyFile(library, pythonHome / "lib" / library.filename(), result.TotalBytes))
					return fail("Could not copy " + library.filename().string());
			}
#endif
			note("Python " + python.Version + " runtime bundled");
		}

		result.Success = true;
		note(fmt::format("Export complete: {:.1f} MB in '{}'", static_cast<double>(result.TotalBytes) / (1024.0 * 1024.0), output.string()));
		return result;
	}

}
