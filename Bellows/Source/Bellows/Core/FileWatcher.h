#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bellows {

	// Notices files that were saved (created or modified) under a folder, by comparing modification
	// times between polls. Polling keeps it portable and cheap enough for a few hundred script files.
	class FileWatcher
	{
	public:
		// Remembers the current state of every file under 'directory' whose extension is listed
		// (with the dot, case-sensitive, e.g. ".py"). Changes before this call are not reported.
		void Start(const std::filesystem::path& directory, std::vector<std::string> extensions);
		void Stop();
		bool IsWatching() const { return !m_Directory.empty(); }

		// Files saved since the last poll, as paths relative to the folder with '/' separators, sorted.
		// Deleted files are forgotten silently.
		std::vector<std::string> Poll();
	private:
		std::unordered_map<std::string, std::filesystem::file_time_type> Scan() const;
	private:
		std::filesystem::path m_Directory;
		std::vector<std::string> m_Extensions;
		std::unordered_map<std::string, std::filesystem::file_time_type> m_Files;
	};

}
