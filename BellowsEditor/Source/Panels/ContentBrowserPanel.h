#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
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

		// The panel is drawn as a closable tab; closing it clears *open (View > Panels shows it again).
		void OnImGuiRender(bool* open);

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
		void DrawFolderNode(const std::filesystem::path& folder, const std::string& label);
		const std::vector<std::filesystem::path>& GetSubfolders(const std::filesystem::path& folder);
		void MoveAsset(const std::string& assetPath, const std::filesystem::path& folder);
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
		std::unordered_map<std::string, std::vector<std::filesystem::path>> m_Subfolders;	// Folder tree cache, cleared on rescan
		bool m_RevealCurrentFolder = true;

		std::filesystem::path m_RenamePath;
		std::string m_RenameBuffer;
		std::string m_RenameError;		// Why renaming to m_RenameFailedName failed, shown while that name is typed
		std::string m_RenameFailedName;
		bool m_OpenRename = false;
		std::filesystem::path m_DeletePath;
		bool m_OpenDelete = false;
	};

}
