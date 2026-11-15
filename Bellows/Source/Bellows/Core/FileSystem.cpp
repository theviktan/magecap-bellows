#include "bwpch.h"
#include "Bellows/Core/FileSystem.h"

namespace Bellows {

	std::optional<std::string> FileSystem::ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
			return std::nullopt;

		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	Buffer FileSystem::ReadBinaryFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary | std::ios::ate);
		if (!stream)
			return {};

		const std::streamoff size = stream.tellg();
		if (size <= 0)
			return {};

		Buffer buffer(static_cast<uint64_t>(size));
		stream.seekg(0, std::ios::beg);
		stream.read(reinterpret_cast<char*>(buffer.Data()), size);
		if (!stream)
			return {};
		return buffer;
	}

	bool FileSystem::WriteTextFile(const std::filesystem::path& path, std::string_view text)
	{
		return WriteBinaryFile(path, text.data(), text.size());
	}

	bool FileSystem::WriteBinaryFile(const std::filesystem::path& path, const void* data, uint64_t size)
	{
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::filesystem::path temporaryPath = path;
		temporaryPath += ".tmp";
		{
			std::ofstream stream(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!stream)
				return false;
			if (size > 0)
				stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if (!stream)
				return false;
		}

		std::filesystem::rename(temporaryPath, path, error);
		if (error)
		{
			// rename() cannot replace an existing file on some platforms/filesystems.
			std::filesystem::remove(path, error);
			std::filesystem::rename(temporaryPath, path, error);
			if (error)
			{
				std::filesystem::remove(temporaryPath, error);
				return false;
			}
		}
		return true;
	}

	bool FileSystem::Exists(const std::filesystem::path& path)
	{
		std::error_code error;
		return std::filesystem::exists(path, error);
	}

	bool FileSystem::CreateDirectories(const std::filesystem::path& path)
	{
		std::error_code error;
		std::filesystem::create_directories(path, error);
		return !error && std::filesystem::is_directory(path, error);
	}

	std::string FileSystem::MakeRelativeGenericPath(const std::filesystem::path& path, const std::filesystem::path& base)
	{
		std::error_code error;
		const std::filesystem::path absolutePath = std::filesystem::weakly_canonical(path, error);
		const std::filesystem::path absoluteBase = std::filesystem::weakly_canonical(base, error);
		const std::filesystem::path relative = absolutePath.lexically_relative(absoluteBase);
		if (relative.empty())
			return {};

		const std::string generic = relative.generic_string();
		if (generic.rfind("..", 0) == 0)
			return {};
		return generic;
	}

	bool FileSystem::CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination)
	{
		std::error_code error;
		std::filesystem::create_directories(destination, error);
		std::filesystem::copy(source, destination,
			std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
		if (error)
			BW_CORE_ERROR("Failed to copy '{}' to '{}': {}", source.string(), destination.string(), error.message());
		return !error;
	}

}
