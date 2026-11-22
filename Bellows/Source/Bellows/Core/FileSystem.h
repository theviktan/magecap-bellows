#pragma once

#include "Bellows/Core/Buffer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Bellows {

	class FileSystem
	{
	public:
		static std::optional<std::string> ReadTextFile(const std::filesystem::path& path);
		static Buffer ReadBinaryFile(const std::filesystem::path& path);

		// Writes atomically: data goes to a temporary file that then replaces the target.
		static bool WriteTextFile(const std::filesystem::path& path, std::string_view text);
		static bool WriteBinaryFile(const std::filesystem::path& path, const void* data, uint64_t size);

		static bool Exists(const std::filesystem::path& path);
		static bool CreateDirectories(const std::filesystem::path& path);

		// Returns 'path' relative to 'base' using forward slashes, or an empty string if 'path' is not inside 'base'.
		static std::string MakeRelativeGenericPath(const std::filesystem::path& path, const std::filesystem::path& base);

		// True for the names Windows keeps for devices (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9), in
		// any case and with any extension ("nul.forge"). Windows 10, Explorer, zip tools and some Git
		// setups can't open, run or delete such files, so projects never use them, on any system.
		static bool IsWindowsDeviceName(std::string_view fileName);

		// Copies a directory tree. Existing files are overwritten.
		static bool CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination);
	};

}
