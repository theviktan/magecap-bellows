#pragma once

#include "ScriptDocument.h"

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <filesystem>
#include <string>
#include <vector>

class TextEditor;

namespace Bellows {

	class EditorLayer;
	class Entity;

	// The built-in code editor (the "Script" workspace): open ForgeScript and Python files in tabs,
	// with highlighting, an outline, find and replace, live error checking and saving.
	// It also owns the New Script / Attach Script dialog.
	class ScriptEditorPanel
	{
	public:
		static constexpr const char* WindowName = "Script";

		explicit ScriptEditorPanel(EditorLayer& editor);
		~ScriptEditorPanel();

		void OnImGuiRender();

		// Opens a script (asset path or absolute path) and brings the Script workspace to the front.
		// 'line' is 1-based; 0 keeps the current position. Returns false if the file cannot be read.
		bool OpenScript(const std::filesystem::path& path, size_t line = 0);
		// Opens a file for reading only, for example a guide from the Help menu.
		bool OpenReadOnly(const std::filesystem::path& path);

		// Shows the New Script dialog. With an entity, the dialog attaches the script to it.
		void ShowCreateDialog(ScriptLanguage language = ScriptLanguage::ForgeScript);
		void ShowAttachDialog(Entity entity);

		// The guides (ForgeScript, Python API, automation), opened read-only in the Script Editor.
		void DrawHelpMenuItems();

		bool SaveAll();
		bool HasUnsavedChanges() const;
		// Closes every script without asking (used when the project changes, after SaveAll).
		void CloseAll();

		bool IsVisible() const { return m_Visible; }
		bool IsFocused() const { return m_Focused; }
		void Focus() { m_FocusRequested = true; }
	private:
		struct Document
		{
			std::filesystem::path Path;
			std::string AssetPath;		// Empty for files outside the project
			std::string Name;
			ScriptLanguage Language = ScriptLanguage::None;
			Scope<TextEditor> Editor;
			size_t SavedUndoIndex = 0;
			std::filesystem::file_time_type DiskTime;
			bool UsesCrLf = false;		// Saved back with the line endings it had
			bool ReadOnly = false;
			bool ChangedOnDisk = false;	// Changed outside the editor while it had unsaved edits
			bool NeedsAnalysis = true;
			std::vector<ScriptOutlineItem> Outline;
			std::vector<ScriptDiagnostic> Diagnostics;
		};

		Document* FindDocument(const std::filesystem::path& path);
		Document* GetActiveDocument();
		bool LoadDocument(Document& document);
		bool SaveDocument(Document& document);
		bool IsDirty(const Document& document) const;
		void CloseDocument(size_t index);
		void RequestClose(size_t index);
		void Analyze(Document& document);
		void CheckDiskChanges();
		void GoToLine(Document& document, size_t line, size_t column = 0);

		void DrawMenuBar();
		void DrawSidebar();
		void DrawEditor(Document& document);
		void DrawStatusBar(Document& document);
		void DrawEmptyState();
		void DrawCloseDialog();
		void DrawCreateDialog();
		void HandleShortcuts();
		void ConfigureEditor(Document& document);
		std::filesystem::path FindDocumentation(const std::string& relativePath) const;
	private:
		EditorLayer& m_Editor;
		std::vector<Scope<Document>> m_Documents;
		size_t m_ActiveDocument = 0;
		std::string m_ScriptFilter;
		bool m_Visible = false;
		bool m_Focused = false;
		bool m_FocusRequested = false;
		bool m_FocusEditor = false;
		double m_LastDiskCheck = 0.0;

		// Closing a tab with unsaved changes asks first.
		size_t m_PendingClose = SIZE_MAX;
		bool m_OpenCloseDialog = false;

		// New Script / Attach Script dialog
		bool m_OpenCreateDialog = false;
		UUID m_AttachEntity = 0;		// 0 = only create the file
		std::string m_AttachEntityName;
		ScriptLanguage m_CreateLanguage = ScriptLanguage::ForgeScript;
		int m_CreateTemplate = 0;
		std::string m_CreatePath;
	};

}
