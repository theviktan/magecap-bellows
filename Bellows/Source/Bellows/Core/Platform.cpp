#include "bwpch.h"
#include "Bellows/Core/Platform.h"

#if defined(BW_PLATFORM_WINDOWS)
	#include <Windows.h>
	#include <shellapi.h>
#else
	#include <unistd.h>
	#include <cctype>
	#include <climits>
	#include <cstdlib>
	#include <ctime>
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

	bool Platform::OpenForEditing(const std::filesystem::path& path)
	{
#if defined(BW_PLATFORM_WINDOWS)
		const std::wstring file = path.wstring();
		if (reinterpret_cast<intptr_t>(::ShellExecuteW(nullptr, L"edit", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32)
			return true;
		// No "edit" verb (often .py and .forge): let the user pick, rather than "open", which can run it.
		return reinterpret_cast<intptr_t>(::ShellExecuteW(nullptr, L"openas", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
		// xdg-open opens a file in the application for its type; it doesn't run scripts.
		return OpenWithDefaultApplication(path);
#endif
	}

	bool Platform::MoveToTrash(const std::filesystem::path& path, std::string& outError)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(path, error);
		if (error || !std::filesystem::exists(absolute, error))
		{
			outError = "'" + path.string() + "' doesn't exist";
			return false;
		}
#if defined(BW_PLATFORM_WINDOWS)
		// The shell takes a list of paths, each ending in a null, and a second null at the end.
		std::wstring from = absolute.wstring();
		from.push_back(L'\0');
		SHFILEOPSTRUCTW operation{};
		operation.wFunc = FO_DELETE;
		operation.pFrom = from.c_str();
		operation.fFlags = static_cast<FILEOP_FLAGS>(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);
		const int result = ::SHFileOperationW(&operation);
		if (result != 0 || operation.fAnyOperationsAborted)
		{
			outError = "the Recycle Bin refused it (error " + std::to_string(result) + ")";
			return false;
		}
		return true;
#else
		// The freedesktop.org trash: the item in files/, and a note in info/ of where it came from, which
		// file managers use to restore it.
		const std::string dataHome = GetEnvVar("XDG_DATA_HOME");
		const std::string home = GetEnvVar("HOME");
		if (dataHome.empty() && home.empty())
		{
			outError = "there is no home folder for the Trash";
			return false;
		}
		const std::filesystem::path trash = (dataHome.empty() ? std::filesystem::path(home) / ".local" / "share" : std::filesystem::path(dataHome)) / "Trash";
		std::filesystem::create_directories(trash / "files", error);
		std::filesystem::create_directories(trash / "info", error);
		if (error)
		{
			outError = "the Trash folder can't be made: " + error.message();
			return false;
		}

		// A name not yet in the Trash: "Name", "Name.2", "Name.3", ...
		const std::string name = absolute.filename().string();
		std::string trashName = name;
		std::filesystem::path info;
		for (int attempt = 1;; attempt++)
		{
			if (attempt > 1)
				trashName = name + "." + std::to_string(attempt);
			info = trash / "info" / (trashName + ".trashinfo");
			if (!std::filesystem::exists(trash / "files" / trashName, error) && !std::filesystem::exists(info, error))
				break;
			if (attempt == 10000)
			{
				outError = "the Trash has too many items named '" + name + "'";
				return false;
			}
		}

		// The original path, percent-encoded except for unreserved characters and '/'.
		std::string encoded;
		for (const unsigned char c : absolute.string())
		{
			if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
				encoded.push_back(static_cast<char>(c));
			else
			{
				static constexpr char c_Hex[] = "0123456789ABCDEF";
				encoded += { '%', c_Hex[c >> 4], c_Hex[c & 0x0F] };
			}
		}
		char date[32] = {};
		const std::time_t now = std::time(nullptr);
		std::tm local{};
		localtime_r(&now, &local);
		std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local);
		{
			std::ofstream note(info);
			note << "[Trash Info]\nPath=" << encoded << "\nDeletionDate=" << date << "\n";
			if (!note)
			{
				outError = "the Trash's note can't be written";
				return false;
			}
		}

		std::filesystem::rename(absolute, trash / "files" / trashName, error);
		if (error)
		{
			std::error_code ignored;
			std::filesystem::remove(info, ignored);
			outError = error == std::errc::cross_device_link ? "it is on another drive than the Trash" : error.message();
			return false;
		}
		return true;
#endif
	}

}
