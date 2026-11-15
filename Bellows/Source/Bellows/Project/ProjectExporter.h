#pragma once

#include "Bellows/Project/Project.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	struct ExportOptions
	{
		std::filesystem::path OutputDirectory;
		bool IncludePythonRuntime = true;	// Bundles the Python standard library and runtime library
	};

	struct ExportResult
	{
		bool Success = false;
		std::filesystem::path ExecutablePath;
		std::vector<std::string> Messages;
		uint64_t TotalBytes = 0;
	};

	// Produces a standalone, distributable build of a project for the current platform:
	//   <Output>/<Game>[.exe]      the runtime (no editor code)
	//   <Output>/<Game>.bproject   project settings
	//   <Output>/Assets/           project assets
	//   <Output>/Shaders/, Python/ engine data
	//   <Output>/PythonRuntime/    bundled Python runtime
	class ProjectExporter
	{
	public:
		static ExportResult Export(const Project& project, const ExportOptions& options);

		// File-system safe version of a project name ("My Game!" -> "My Game").
		static std::string SanitizeName(const std::string& name);
	};

}
