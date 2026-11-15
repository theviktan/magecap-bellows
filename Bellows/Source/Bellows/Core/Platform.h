#pragma once

#include <filesystem>
#include <string>

namespace Bellows {

	// Thin wrapper around the few operating-system services the engine needs.
	class Platform
	{
	public:
		static bool IsDebuggerAttached();

		// Absolute path of the running executable and its folder.
		static std::filesystem::path GetExecutablePath();
		static std::filesystem::path GetExecutableDirectory();

		// Platform shared-library file name for a base name ("python3" -> "python3.dll" / "libpython3.so").
		static std::string GetSharedLibraryExtension();
		static std::string GetExecutableExtension();

		static bool SetEnvVar(const std::string& name, const std::string& value);
		static std::string GetEnvVar(const std::string& name);

		// Opens a folder or file with the system's default handler (file explorer, browser, ...).
		static bool OpenWithDefaultApplication(const std::filesystem::path& path);
	};

}
