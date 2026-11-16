#include "ProjectList.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Project/ProjectExporter.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <algorithm>
#include <cctype>

namespace Bellows {

	namespace {

		std::string ToLower(std::string_view text)
		{
			std::string result(text);
			for (char& c : result)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return result;
		}

		bool IsSkippedFolder(const std::string& name)
		{
			return name.empty() || name.front() == '.' || name == "Assets" || name == "Build" || name == "node_modules";
		}

		void FindProjectsIn(const std::filesystem::path& folder, int depthLeft, std::vector<std::filesystem::path>& outProjects)
		{
			std::error_code error;
			std::vector<std::filesystem::path> subfolders;
			for (const auto& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, error))
			{
				std::error_code entryError;
				if (entry.is_regular_file(entryError) && entry.path().extension() == Project::FileExtension)
					outProjects.push_back(entry.path());
				else if (depthLeft > 0 && entry.is_directory(entryError) && !entry.is_symlink(entryError) && !IsSkippedFolder(entry.path().filename().string()))
					subfolders.push_back(entry.path());
			}
			std::sort(subfolders.begin(), subfolders.end());
			for (const std::filesystem::path& subfolder : subfolders)
				FindProjectsIn(subfolder, depthLeft - 1, outProjects);
		}

		std::string Plural(int64_t count, const char* unit)
		{
			return std::to_string(count) + " " + unit + (count == 1 ? "" : "s") + " ago";
		}

	}

	std::string ReadProjectName(const std::filesystem::path& projectFile)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(projectFile);
		if (!text)
			return {};
		std::string name = projectFile.stem().string();
		const Json json = Json::parse(*text, nullptr, false);
		if (!json.is_discarded())
			JsonRead(json, "Name", name);
		return name;
	}

	std::vector<std::filesystem::path> FindProjects(const std::filesystem::path& folder, int maxDepth)
	{
		std::vector<std::filesystem::path> projects;
		FindProjectsIn(folder, maxDepth, projects);
		return projects;
	}

	std::string FormatTimeAgo(int64_t then, int64_t now)
	{
		if (then <= 0)
			return "never";
		const int64_t seconds = std::max<int64_t>(0, now - then);
		constexpr int64_t minute = 60, hour = 60 * minute, day = 24 * hour, week = 7 * day, month = 30 * day, year = 365 * day;
		if (seconds < minute)	return "just now";
		if (seconds < hour)		return Plural(seconds / minute, "minute");
		if (seconds < day)		return Plural(seconds / hour, "hour");
		if (seconds < 2 * day)	return "yesterday";
		if (seconds < week)		return Plural(seconds / day, "day");
		if (seconds < month)	return Plural(seconds / week, "week");
		if (seconds < year)		return Plural(seconds / month, "month");
		return Plural(seconds / year, "year");
	}

	std::vector<ProjectListItem> SortProjects(std::vector<ProjectListItem> items, ProjectSort sort, std::string_view filter)
	{
		if (!filter.empty())
		{
			const std::string lowerFilter = ToLower(filter);
			std::erase_if(items, [&lowerFilter](const ProjectListItem& item)
			{
				return ToLower(item.Name).find(lowerFilter) == std::string::npos && ToLower(item.Entry.Path).find(lowerFilter) == std::string::npos;
			});
		}

		std::stable_sort(items.begin(), items.end(), [sort](const ProjectListItem& a, const ProjectListItem& b)
		{
			if (a.Entry.Favorite != b.Entry.Favorite)
				return a.Entry.Favorite;
			switch (sort)
			{
				case ProjectSort::LastOpened:
					if (a.Entry.LastOpened != b.Entry.LastOpened)
						return a.Entry.LastOpened > b.Entry.LastOpened;
					break;
				case ProjectSort::Name:
				{
					const std::string nameA = ToLower(a.Name), nameB = ToLower(b.Name);
					if (nameA != nameB)
						return nameA < nameB;
					break;
				}
				case ProjectSort::Path:
					break;
			}
			return ToLower(a.Entry.Path) < ToLower(b.Entry.Path);
		});
		return items;
	}

	NewProjectCheck CheckNewProjectLocation(std::string_view name, const std::filesystem::path& location, bool createFolder)
	{
		NewProjectCheck check;
		std::string trimmed(name);
		while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
			trimmed.pop_back();
		while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
			trimmed.erase(trimmed.begin());

		if (trimmed.empty())
		{
			check.Message = "Give the project a name.";
			return check;
		}
		if (location.empty())
		{
			check.Message = "Choose where to put the project.";
			return check;
		}
		if (!location.is_absolute())
		{
			check.Message = "Use a full folder path (Browse... picks one).";
			return check;
		}

		check.ProjectFolder = createFolder ? location / ProjectExporter::SanitizeName(trimmed) : location;
		std::error_code error;
		if (std::filesystem::exists(check.ProjectFolder, error) && !std::filesystem::is_directory(check.ProjectFolder, error))
		{
			check.Message = "A file with that name is already there.";
			return check;
		}

		if (!std::filesystem::is_directory(check.ProjectFolder, error))
		{
			check.Status = NewProjectCheck::Level::Ok;
			check.Message = "The project folder will be created.";
			return check;
		}

		bool empty = true;
		for (const auto& entry : std::filesystem::directory_iterator(check.ProjectFolder, error))
		{
			if (entry.path().extension() == Project::FileExtension)
			{
				check.Message = "This folder already has a project. Open it from the list instead.";
				return check;
			}
			empty = false;
		}
		if (empty)
		{
			check.Status = NewProjectCheck::Level::Ok;
			check.Message = "The project will be created in this empty folder.";
		}
		else
		{
			check.Status = NewProjectCheck::Level::Warning;
			check.Message = "This folder is not empty. An empty folder is recommended.";
		}
		return check;
	}

}
