#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace Bellows {

	class EditorLayer;

	// Browses the project's asset folder: navigation, drag-and-drop into the scene, asset creation and import.
	class ContentBrowserPanel
	{
	public:
		explicit ContentBrowserPanel(EditorLayer& editor)
			: m_Editor(editor) {}

		void OnImGuiRender();

		void SetCurrentDirectory(const std::filesystem::path& directory);
		const std::filesystem::path& GetCurrentDirectory() const { return m_CurrentDirectory; }

		// Copies external files into the current folder.
		void ImportFiles(const std::vector<std::filesystem::path>& paths);

		// Asset path of the selected item, or empty.
		const std::string& GetSelectedAsset() const { return m_SelectedAsset; }
	private:
		struct Item
		{
			std::filesystem::path Path;
			std::string Name;
			std::string AssetPath;
			bool IsDirectory = false;
		};

		void Refresh();
		void DrawBreadcrumbs();
		void DrawItem(const Item& item, float size);
		void DrawCreateMenu();
		void DrawDialogs();
		void OpenItem(const Item& item);
		std::filesystem::path MakeUniquePath(const std::string& stem, const std::string& extension) const;
	private:
		EditorLayer& m_Editor;
		std::filesystem::path m_CurrentDirectory;
		std::vector<Item> m_Items;
		double m_LastRefreshTime = -1.0;
		std::string m_Filter;
		std::string m_SelectedAsset;
		float m_ThumbnailSize = 84.0f;
		std::unordered_set<std::string> m_FailedThumbnails;	// Not retried until the folder is rescanned

		std::filesystem::path m_RenamePath;
		std::string m_RenameBuffer;
		bool m_OpenRename = false;
		std::filesystem::path m_DeletePath;
		bool m_OpenDelete = false;
	};

}
