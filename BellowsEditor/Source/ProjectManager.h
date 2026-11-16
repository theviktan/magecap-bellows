#pragma once

#include "ProjectList.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	class EditorLayer;

	// The window shown when no project is open, like Godot's Project Manager: every known project
	// with search, sorting and favorites, plus creating, importing and scanning for projects.
	// It also owns the New Project dialog, which the File menu opens too.
	class ProjectManager
	{
	public:
		explicit ProjectManager(EditorLayer& editor);

		void OnImGuiRender();
		void DrawDialogs();

		void ShowNewProjectDialog();
		// Rebuilds the list on the next frame (after projects were added or removed elsewhere).
		void Invalidate() { m_LastRefresh = -1.0; }
	private:
		void Refresh();
		void DrawHeader(float height);
		void DrawToolbar();
		void DrawList();
		void DrawEmptyState();
		void DrawActions();
		void DrawNewProjectDialog();

		void OpenProject(const std::string& path);
		void ImportProject();
		void ScanFolder();
		void SetStatus(const std::string& message, bool error = false);
		const ProjectListItem* GetSelected() const;
		std::filesystem::path FindSampleProject() const;
		std::filesystem::path GetDefaultLocation() const;
	private:
		EditorLayer& m_Editor;
		std::vector<ProjectListItem> m_Items;		// Sorted and filtered, as shown
		double m_LastRefresh = -1.0;
		std::string m_Filter;
		ProjectSort m_Sort = ProjectSort::LastOpened;
		std::string m_Selected;						// Path of the selected project
		std::string m_Status;
		bool m_StatusIsError = false;
		double m_StatusTime = 0.0;
		std::filesystem::path m_SampleProject;

		// New Project dialog
		bool m_OpenNewProject = false;
		std::string m_NewName;
		std::string m_NewLocation;
		bool m_NewCreateFolder = true;
		bool m_NewEmptyScene = false;
	};

}
