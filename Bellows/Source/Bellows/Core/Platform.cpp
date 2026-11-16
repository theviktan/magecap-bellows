#include "bwpch.h"
#include "Bellows/Core/Platform.h"

#if defined(BW_PLATFORM_WINDOWS)
	#include <Windows.h>
	#include <shellapi.h>
#else
	#include <unistd.h>
	#include <cstdlib>
	#include <climits>
#endif

namespace Bellows {

	bool Platform::IsDebuggerAttached()
	{
#if defined(BW_PLATFORM_WINDOWS)
		return ::IsDebuggerPresent() != 0;
#else
		std::ifstream status("/proc/self/status");
		std::string line;
		while (std::getline(status, line))
		{
			constexpr std::string_view tracerPrefix = "TracerPid:";
			if (line.rfind(tracerPrefix, 0) == 0)
			{
				const std::string value = line.substr(tracerPrefix.size());
				return std::strtol(value.c_str(), nullptr, 10) != 0;
			}
		}
		return false;
#endif
	}

	std::filesystem::path Platform::GetExecutablePath()
	{
#if defined(BW_PLATFORM_WINDOWS)
		std::wstring buffer(MAX_PATH, L'\0');
		while (true)
		{
			const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return {};
			if (length < buffer.size())
			{
				buffer.resize(length);
				return std::filesystem::path(buffer);
			}
			buffer.resize(buffer.size() * 2);
		}
#else
		std::error_code error;
		std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
		return error ? std::filesystem::path() : path;
#endif
	}

	std::filesystem::path Platform::GetExecutableDirectory()
	{
		return GetExecutablePath().parent_path();
	}

	std::string Platform::GetSharedLibraryExtension()
	{
#if defined(BW_PLATFORM_WINDOWS)
		return ".dll";
#else
		return ".so";
#endif
	}

	std::string Platform::GetExecutableExtension()
	{
#if defined(BW_PLATFORM_WINDOWS)
		return ".exe";
#else
		return "";
#endif
	}

	std::filesystem::path Platform::GetUserDataDirectory()
	{
		if (const std::string overrideDirectory = GetEnvVar("BELLOWS_USER_DATA_DIR"); !overrideDirectory.empty())
			return std::filesystem::path(overrideDirectory);
#if defined(BW_PLATFORM_WINDOWS)
		const std::string appData = GetEnvVar("APPDATA");
		return appData.empty() ? GetExecutableDirectory() : std::filesystem::path(appData);
#else
		if (const std::string dataHome = GetEnvVar("XDG_DATA_HOME"); !dataHome.empty())
			return std::filesystem::path(dataHome);
		const std::string home = GetEnvVar("HOME");
		return home.empty() ? GetExecutableDirectory() : std::filesystem::path(home) / ".local" / "share";
#endif
	}

	bool Platform::SetEnvVar(const std::string& name, const std::string& value)
	{
#if defined(BW_PLATFORM_WINDOWS)
		return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
		return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
	}

	std::string Platform::GetEnvVar(const std::string& name)
	{
#if defined(BW_PLATFORM_WINDOWS)
		char* value = nullptr;
		size_t length = 0;
		if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr)
			return {};
		std::string result(value);
		free(value);
		return result;
#else
		const char* value = std::getenv(name.c_str());
		return value ? std::string(value) : std::string();
#endif
	}

	bool Platform::OpenWithDefaultApplication(const std::filesystem::path& path)
	{
#if defined(BW_PLATFORM_WINDOWS)
		const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return reinterpret_cast<intptr_t>(result) > 32;
#else
		const pid_t pid = fork();
		if (pid < 0)
			return false;
		if (pid == 0)
		{
			execlp("xdg-open", "xdg-open", path.string().c_str(), static_cast<char*>(nullptr));
			_exit(127);
		}
		return true;
#endif
	}

}
