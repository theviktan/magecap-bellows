#pragma once

#include "ScriptDocument.h"

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <filesystem>
#include <string>
#include <vector>

class TextEditor;

namespace Bellows { class ForgeScriptAssist; }

namespace Bellows {

	class EditorLayer;
	class Entity;

	// The built-in code editor (the "Script" workspace): open ForgeScript and Python files in tabs,
	// with highlighting, an outline, find and replace, live error checking and saving. ForgeScript also
	// gets rename, find-all-references, format and quick fixes (Edit menu, right-click, F2, Shift+F12,
	// Shift+Alt+F, Ctrl+.).
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

		// Opens a ForgeScript asset at a line (1-based) and shows the Edit Breakpoint dialog for it: a
		// condition, and a message to log instead of stopping. Used by the Debugger panel.
		void EditBreakpoint(const std::string& assetPath, int line);

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
		// What a breakpoint line holds in its code editor (the line's user data), so the condition and
		// log message move with the line while the script is edited.
		struct BreakpointMark
		{
			std::string Condition;
			std::string LogMessage;
		};

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
			// Every breakpoint line's user data points at one of these; the only user data the editor sets.
			std::vector<Scope<BreakpointMark>> BreakpointMarks;

			// The last hover help asked for (the hover popup draws every frame).
			size_t HoverLine = SIZE_MAX;
			size_t HoverColumn = SIZE_MAX;
			std::string HoverSource;
			std::string HoverText;
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
		// The current marks of a document: its mistakes and, while a script is stopped, the stop line.
		void UpdateMarkers(Document& document);

		// Breakpoints live in the editor's ScriptDebugger; open documents mirror them in their lines,
		// so a breakpoint moves with its line while the script is edited.
		bool CanHaveBreakpoints(const Document& document) const;
		void ToggleBreakpoint(Document& document, size_t line);
		void ShowBreakpoints(Document& document);
		void SaveBreakpoints(Document& document);
		void DrawBreakpointGutter(Document& document, size_t line, float width, float height);
		BreakpointMark* GetBreakpointMark(const Document& document, size_t line) const;
		// Opens the Edit Breakpoint dialog for a zero-based line (a new breakpoint when it has none).
		void OpenBreakpointDialog(Document& document, size_t line);
		void DrawBreakpointDialog();
		bool IsStoppedAt(const Document& document, size_t line) const;
		// ForgeScript completion and hover help come from the language server (ForgeScriptAssist).
		ForgeScriptAssist& GetForgeScriptAssist();
		void DrawForgeScriptHover(Document& document, size_t line, size_t column);

		// The ForgeScript tools, at a zero-based line and column (code points). Each says in the status
		// bar what it did, or why it couldn't.
		void StartRename(Document& document, size_t line, size_t column);
		void FindReferences(Document& document, size_t line, size_t column);
		void FormatDocument(Document& document);
		void ShowQuickFixes(Document& document, size_t line);
		// Replaces a document's text as one undoable step, keeping the cursor at the same line and column.
		void ReplaceText(Document& document, const std::string& text);
		void DrawForgeScriptToolsMenuItems(Document& document, size_t line, size_t column);
		void DrawRenameDialog();
		void DrawQuickFixPopup();
		void DrawReferences(Document& document);
		void SetNotice(std::string text);
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
		Scope<ForgeScriptAssist> m_ForgeScriptAssist;
		// The debugger state the documents show, to redraw them only when it changes.
		uint64_t m_ShownBreakpoints = 0;
		uint64_t m_ShownStop = 0;
		bool m_ShownStopped = false;

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

		// Rename dialog
		bool m_OpenRenameDialog = false;
		std::filesystem::path m_RenamePath;
		size_t m_RenameLine = 0;
		size_t m_RenameColumn = 0;
		std::string m_RenameOldName;
		std::string m_RenameNewName;
		std::string m_RenameError;

		// Find All References: the uses of one name, listed under its document's code.
		struct ReferenceItem
		{
			size_t Line = 0;			// Zero-based
			size_t Column = 0;
			size_t EndColumn = 0;
			std::string LineText;
		};
		std::filesystem::path m_ReferencesPath;
		std::string m_ReferencesName;
		std::vector<ReferenceItem> m_References;

		// Edit Breakpoint dialog
		bool m_OpenBreakpointDialog = false;
		std::filesystem::path m_BreakpointPath;
		size_t m_BreakpointLine = 0;		// Zero-based
		std::string m_BreakpointCondition;
		bool m_BreakpointLogs = false;
		std::string m_BreakpointLogMessage;

		// Quick fixes for the cursor's line
		bool m_OpenQuickFixes = false;
		std::filesystem::path m_QuickFixPath;
		std::vector<ScriptFix> m_QuickFixes;

		// What the last tool did, shown in the status bar for a few seconds.
		std::string m_Notice;
		double m_NoticeTime = 0.0;
	};

}
