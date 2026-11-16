#pragma once

#include "EditorSettings.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	// Project Manager logic without UI: reading project files, finding projects, sorting the list
	// and checking where a new project would go. Kept separate so it can be unit-tested.

	// The project's display name from its .bproject file ("Name"), or the file name without
	// extension. Empty if the file cannot be read.
	std::string ReadProjectName(const std::filesystem::path& projectFile);

	// Every .bproject file in 'folder' and its subfolders, up to 'maxDepth' levels down.
	// Hidden folders and the folders a project generates (Assets, Build, .bellows) are skipped.
	std::vector<std::filesystem::path> FindProjects(const std::filesystem::path& folder, int maxDepth = 4);

	// "just now", "5 minutes ago", "yesterday", "3 weeks ago" ... or "never" for 0.
	std::string FormatTimeAgo(int64_t then, int64_t now);

	enum class ProjectSort
	{
		LastOpened = 0,
		Name,
		Path
	};

	struct ProjectListItem
	{
		ProjectEntry Entry;
		std::string Name;
		bool Missing = false;
	};

	// Favorites first, then by 'sort'. Items whose name or path does not contain 'filter'
	// (case-insensitive) are left out.
	std::vector<ProjectListItem> SortProjects(std::vector<ProjectListItem> items, ProjectSort sort, std::string_view filter);

	// Where "Create" in the New Project dialog would put a project, and whether that is a good idea.
	struct NewProjectCheck
	{
		enum class Level
		{
			Ok = 0,		// Fine
			Warning,	// Allowed, but worth a second look
			Error		// Not allowed
		};

		Level Status = Level::Error;
		std::filesystem::path ProjectFolder;
		std::string Message;
	};

	NewProjectCheck CheckNewProjectLocation(std::string_view name, const std::filesystem::path& location, bool createFolder);

}
