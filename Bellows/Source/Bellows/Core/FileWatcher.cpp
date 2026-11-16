#include "bwpch.h"
#include "Bellows/Core/FileWatcher.h"

namespace Bellows {

	void FileWatcher::Start(const std::filesystem::path& directory, std::vector<std::string> extensions)
	{
		m_Directory = directory;
		m_Extensions = std::move(extensions);
		m_Files = Scan();
	}

	void FileWatcher::Stop()
	{
		m_Directory.clear();
		m_Extensions.clear();
		m_Files.clear();
	}

	std::vector<std::string> FileWatcher::Poll()
	{
		if (!IsWatching())
			return {};
		std::unordered_map<std::string, std::filesystem::file_time_type> files = Scan();
		std::vector<std::string> changed;
		for (const auto& [path, writeTime] : files)
		{
			auto it = m_Files.find(path);
			if (it == m_Files.end() || it->second != writeTime)
				changed.push_back(path);
		}
		std::sort(changed.begin(), changed.end());
		m_Files = std::move(files);
		return changed;
	}

	std::unordered_map<std::string, std::filesystem::file_time_type> FileWatcher::Scan() const
	{
		std::unordered_map<std::string, std::filesystem::file_time_type> files;
		std::error_code error;
		auto it = std::filesystem::recursive_directory_iterator(m_Directory, std::filesystem::directory_options::skip_permission_denied, error);
		for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			const std::filesystem::path& path = it->path();
			// Editor data and Python's bytecode caches never hold scripts.
			const std::string name = path.filename().string();
			if (it->is_directory(error) && (name == "__pycache__" || name == ".bellows"))
			{
				it.disable_recursion_pending();
				continue;
			}
			const std::string extension = path.extension().string();
			if (!it->is_regular_file(error) || std::find(m_Extensions.begin(), m_Extensions.end(), extension) == m_Extensions.end())
				continue;
			std::error_code timeError;
			const auto writeTime = std::filesystem::last_write_time(path, timeError);
			if (!timeError)
				files[std::filesystem::relative(path, m_Directory, timeError).generic_string()] = writeTime;
		}
		return files;
	}

}
