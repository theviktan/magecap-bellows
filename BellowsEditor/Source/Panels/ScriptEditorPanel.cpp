#include "ScriptEditorPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "ForgeScriptAssist.h"
#include "IconsLucide.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Scene/Components.h"

#include <TextEditor.h>
#include <languages/TextEditorC.h>
#include <languages/TextEditorMarkdown.h>
#include <languages/TextEditorPython.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>

namespace Bellows {

	namespace {

		constexpr double c_DiskCheckInterval = 1.0;		// Seconds between checks for files changed outside the editor
		constexpr int c_AnalysisDelay = 300;			// Milliseconds after the last keystroke before checking for mistakes
		constexpr size_t c_MaxSuggestions = 40;
		constexpr const char* c_ScriptFolder = "Scripts";
		constexpr size_t c_GutterGlyphs = 2;				// Width of the breakpoint column, in characters

		// The line data that marks a breakpoint line in a code editor (only its address matters).
		char s_BreakpointMark = 0;

		// Words that control the flow of a ForgeScript program or start a statement.
		constexpr const char* c_ForgeKeywords[] = {
			"if", "then", "else", "otherwise", "end", "while", "until", "do", "repeat", "times", "forever", "for", "each",
			"in", "from", "to", "by", "stop", "skip", "return", "try", "catch", "fail", "wait", "start", "use", "as",
			"with", "set", "constant", "add", "take", "multiply", "divide", "new", "say", "and", "or", "not", "is",
			"contains", "of", "above", "below", "at", "least", "most", "greater", "less", "than", "equal", "empty",
			"second", "seconds", "frame", "frames", "ms", "milliseconds", "minute", "minutes"
		};
		// Words that define something: shown in the declaration color.
		constexpr const char* c_ForgeDeclarations[] = { "function", "phrase", "kind", "extends", "when", "trigger" };
		// Built-in values and functions.
		constexpr const char* c_ForgeIdentifiers[] = {
			"self", "my", "parent", "true", "false", "yes", "no", "nothing", "pi", "infinity",
			"assert", "clamp", "copy", "distance", "length", "lerp", "log", "number", "print", "random", "round", "show",
			"sqrt", "text", "time", "type", "vector"
		};

		const TextEditor::Language* GetForgeScriptLanguage()
		{
			static TextEditor::Language language;
			static bool initialized = false;
			if (!initialized)
			{
				language.name = "ForgeScript";
				language.singleLineComment = "#";
				language.commentStart = "#[";		// Checked before the single-line comment
				language.commentEnd = "]#";
				language.hasDoubleQuotedStrings = true;
				language.stringEscape = '\\';
				for (const char* keyword : c_ForgeKeywords)
					language.keywords.insert(keyword);
				for (const char* declaration : c_ForgeDeclarations)
					language.declarations.insert(declaration);
				for (const char* identifier : c_ForgeIdentifiers)
					language.identifiers.insert(identifier);
				language.isPunctuation = TextEditorC::isPunctuation;
				language.getIdentifier = TextEditorC::getIdentifier;
				language.getNumber = TextEditorC::getNumber;
				initialized = true;
			}
			return &language;
		}

		const TextEditor::Palette& GetPalette()
		{
			static TextEditor::Palette palette = []
			{
				TextEditor::Palette result = TextEditor::GetDarkPalette();
				auto set = [&result](TextEditor::Color color, ImU32 value) { result[static_cast<size_t>(color)] = value; };
				set(TextEditor::Color::text, Theme::Text);
				set(TextEditor::Color::keyword, IM_COL32(255, 150, 92, 255));
				set(TextEditor::Color::declaration, IM_COL32(120, 176, 250, 255));
				set(TextEditor::Color::number, IM_COL32(196, 160, 250, 255));
				set(TextEditor::Color::string, IM_COL32(152, 204, 124, 255));
				set(TextEditor::Color::punctuation, Theme::TextMuted);
				set(TextEditor::Color::preprocessor, IM_COL32(120, 176, 250, 255));
				set(TextEditor::Color::identifier, Theme::Text);
				set(TextEditor::Color::knownIdentifier, IM_COL32(96, 204, 214, 255));
				set(TextEditor::Color::comment, IM_COL32(112, 120, 134, 255));
				set(TextEditor::Color::background, Theme::Panel);
				set(TextEditor::Color::cursor, Theme::Accent);
				set(TextEditor::Color::selection, IM_COL32(242, 112, 26, 64));
				set(TextEditor::Color::whitespace, IM_COL32(255, 255, 255, 22));
				set(TextEditor::Color::lineNumber, Theme::TextDisabled);
				set(TextEditor::Color::currentLineNumber, Theme::Text);
				set(TextEditor::Color::currentLineHighlight, IM_COL32(255, 255, 255, 7));
				set(TextEditor::Color::currentLineHighlightBorder, IM_COL32(255, 255, 255, 0));
				return result;
			}();
			return palette;
		}

		std::string ToLower(std::string_view text)
		{
			std::string result(text);
			for (char& c : result)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return result;
		}

		bool IsMarkdown(const std::filesystem::path& path)
		{
			return ToLower(path.extension().string()) == ".md";
		}

		bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			std::error_code error;
			return std::filesystem::equivalent(a, b, error) || a.lexically_normal() == b.lexically_normal();
		}

		std::filesystem::file_time_type GetWriteTime(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto time = std::filesystem::last_write_time(path, error);
			return error ? std::filesystem::file_time_type() : time;
		}

		const char* OutlineIcon(ScriptOutlineItem::Kind kind)
		{
			switch (kind)
			{
				case ScriptOutlineItem::Kind::Function:	return ICON_SQUARE_FUNCTION;
				case ScriptOutlineItem::Kind::Event:	return ICON_ZAP;
				case ScriptOutlineItem::Kind::Type:		return ICON_BOX;
				case ScriptOutlineItem::Kind::Phrase:	return ICON_SCROLL_TEXT;
				case ScriptOutlineItem::Kind::Heading:	return ICON_BOOK_OPEN;
			}
			return ICON_CIRCLE;
		}

		ImU32 OutlineColor(ScriptOutlineItem::Kind kind)
		{
			switch (kind)
			{
				case ScriptOutlineItem::Kind::Function:	return IM_COL32(120, 176, 250, 255);
				case ScriptOutlineItem::Kind::Event:	return Theme::Warning;
				case ScriptOutlineItem::Kind::Type:		return IM_COL32(96, 204, 214, 255);
				case ScriptOutlineItem::Kind::Phrase:	return IM_COL32(196, 160, 250, 255);
				case ScriptOutlineItem::Kind::Heading:	return Theme::TextMuted;
			}
			return Theme::TextMuted;
		}

		const char* LanguageIcon(ScriptLanguage language)
		{
			return language == ScriptLanguage::None ? ICON_FILE_TEXT : ICON_FILE_CODE;
		}

		ImU32 LanguageColor(ScriptLanguage language)
		{
			switch (language)
			{
				case ScriptLanguage::ForgeScript:	return Theme::Accent;
				case ScriptLanguage::Python:		return IM_COL32(120, 210, 230, 255);
				case ScriptLanguage::None:			break;
			}
			return Theme::TextMuted;
		}

		// ForgeScript suggestions from the language server, with each one's signature beside it.
		void SuggestForgeScript(ForgeScriptAssist& assist, const std::filesystem::path& path, const TextEditor& editor, TextEditor::AutoCompleteState& state)
		{
			// The editor keeps the last list between calls; start from an empty one.
			state.suggestions.clear();
			if (state.inComment || state.inString)
				return;
			// Right after a "." members are wanted; elsewhere wait for two letters, as for other words.
			const auto codePoints = editor.GetLineCodePoints(state.searchTermStart.line);
			const size_t start = state.searchTermStart.index;
			const bool afterDot = start > 0 && start <= codePoints.size() && codePoints[start - 1] == '.';
			if (state.searchTerm.size() < 2 && !afterDot)
				return;

			constexpr size_t c_MaxForgeScriptSuggestions = 80;
			const std::vector<ForgeScriptAssist::Suggestion> suggestions = assist.Complete(path, editor.GetText(),
				state.searchTermEnd.line, state.searchTermEnd.index, state.searchTerm);
			for (const ForgeScriptAssist::Suggestion& suggestion : suggestions)
			{
				if (state.suggestions.size() == c_MaxForgeScriptSuggestions)
					break;
				state.suggestions.push_back(suggestion.Insert);
				// "play_sound   (path: str, ...) -> bool": the signature without repeating the name.
				std::string detail = suggestion.Detail;
				if (detail.rfind(suggestion.Label, 0) == 0)
					detail.erase(0, suggestion.Label.size());
				std::string label = suggestion.Label;
				if (!detail.empty())
					label += "   " + detail;
				state.labels.push_back(std::move(label));
			}
		}

		// Suggests language words and names already used in the script.
		void SuggestWords(TextEditor::AutoCompleteState& state)
		{
			if (state.inComment || state.inString || state.searchTerm.size() < 2)
				return;

			const std::string term = ToLower(state.searchTerm);
			std::vector<std::string> words;
			auto consider = [&](const std::string& word)
			{
				if (word.size() > term.size() && ToLower(word).compare(0, term.size(), term) == 0
					&& std::find(words.begin(), words.end(), word) == words.end())
					words.push_back(word);
			};
			if (state.language)
			{
				for (const std::string& word : state.language->keywords) consider(word);
				for (const std::string& word : state.language->declarations) consider(word);
				for (const std::string& word : state.language->identifiers) consider(word);
			}
			if (const TextEditor* editor = static_cast<const TextEditor*>(state.userData))
				editor->IterateIdentifiers(consider);

			std::sort(words.begin(), words.end(), [](const std::string& a, const std::string& b)
			{
				return a.size() != b.size() ? a.size() < b.size() : a < b;
			});
			if (words.size() > c_MaxSuggestions)
				words.resize(c_MaxSuggestions);
			state.suggestions = std::move(words);
		}

	}

	ScriptEditorPanel::ScriptEditorPanel(EditorLayer& editor)
		: m_Editor(editor)
	{
		// The checker must know the engine API, or every engine call would be reported as a mistake.
		SetForgeScriptCheckSetup(SetUpEngineForgeScriptCheck);
	}

	ScriptEditorPanel::~ScriptEditorPanel() = default;

	// -------------------------------------------------------------------------
	// Documents
	// -------------------------------------------------------------------------

	bool ScriptEditorPanel::OpenScript(const std::filesystem::path& path, size_t line)
	{
		const std::filesystem::path absolute = path.is_absolute() ? path : AssetManager::GetAbsolutePath(path.generic_string());
		Document* document = FindDocument(absolute);
		if (!document)
		{
			auto created = CreateScope<Document>();
			created->Path = absolute;
			created->AssetPath = AssetManager::GetAssetPath(absolute);
			created->Name = absolute.filename().string();
			created->Language = GetScriptLanguage(absolute);
			created->Editor = CreateScope<TextEditor>();
			if (!LoadDocument(*created))
			{
				BW_ERROR("Could not open '{}'", absolute.string());
				return false;
			}
			ConfigureEditor(*created);
			m_Documents.push_back(std::move(created));
			document = m_Documents.back().get();
		}

		for (size_t i = 0; i < m_Documents.size(); i++)
		{
			if (m_Documents[i].get() == document)
				m_ActiveDocument = i;
		}
		if (line > 0)
			GoToLine(*document, line);
		m_FocusRequested = true;
		m_FocusEditor = true;
		return true;
	}

	bool ScriptEditorPanel::OpenReadOnly(const std::filesystem::path& path)
	{
		if (!OpenScript(path))
			return false;
		if (Document* document = GetActiveDocument())
		{
			document->ReadOnly = true;
			document->Editor->SetReadOnlyEnabled(true);
		}
		return true;
	}

	ScriptEditorPanel::Document* ScriptEditorPanel::FindDocument(const std::filesystem::path& path)
	{
		for (const Scope<Document>& document : m_Documents)
		{
			if (SamePath(document->Path, path))
				return document.get();
		}
		return nullptr;
	}

	ScriptEditorPanel::Document* ScriptEditorPanel::GetActiveDocument()
	{
		return m_ActiveDocument < m_Documents.size() ? m_Documents[m_ActiveDocument].get() : nullptr;
	}

	bool ScriptEditorPanel::LoadDocument(Document& document)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(document.Path);
		if (!text)
			return false;

		document.UsesCrLf = text->find("\r\n") != std::string::npos;
		document.Editor->SetText(*text);
		ShowBreakpoints(document);
		document.SavedUndoIndex = document.Editor->GetUndoIndex();
		document.DiskTime = GetWriteTime(document.Path);
		document.ChangedOnDisk = false;
		document.NeedsAnalysis = true;
		return true;
	}

	bool ScriptEditorPanel::SaveDocument(Document& document)
	{
		if (document.ReadOnly)
			return true;

		std::string text = document.Editor->GetText();
		if (document.UsesCrLf)
		{
			std::string converted;
			converted.reserve(text.size() + text.size() / 32);
			for (char c : text)
			{
				if (c == '\n')
					converted += '\r';
				converted += c;
			}
			text = std::move(converted);
		}

		if (!FileSystem::WriteTextFile(document.Path, text))
		{
			BW_ERROR("Could not save '{}'", document.Path.string());
			return false;
		}
		document.SavedUndoIndex = document.Editor->GetUndoIndex();
		document.DiskTime = GetWriteTime(document.Path);
		document.ChangedOnDisk = false;
		BW_INFO("Saved {}", document.AssetPath.empty() ? document.Path.string() : document.AssetPath);
		return true;
	}

	bool ScriptEditorPanel::IsDirty(const Document& document) const
	{
		return !document.ReadOnly && document.Editor->GetUndoIndex() != document.SavedUndoIndex;
	}

	bool ScriptEditorPanel::SaveAll()
	{
		bool success = true;
		for (const Scope<Document>& document : m_Documents)
		{
			if (IsDirty(*document))
				success &= SaveDocument(*document);
		}
		return success;
	}

	bool ScriptEditorPanel::HasUnsavedChanges() const
	{
		return std::any_of(m_Documents.begin(), m_Documents.end(), [this](const Scope<Document>& document) { return IsDirty(*document); });
	}

	void ScriptEditorPanel::CloseAll()
	{
		m_Documents.clear();
		m_ActiveDocument = 0;
		m_PendingClose = SIZE_MAX;
	}

	void ScriptEditorPanel::CloseDocument(size_t index)
	{
		if (index >= m_Documents.size())
			return;
		if (m_ForgeScriptAssist && m_Documents[index]->Language == ScriptLanguage::ForgeScript)
			m_ForgeScriptAssist->Close(m_Documents[index]->Path);
		m_Documents.erase(m_Documents.begin() + static_cast<std::ptrdiff_t>(index));
		if (m_ActiveDocument > index || m_ActiveDocument >= m_Documents.size())
			m_ActiveDocument = m_ActiveDocument > 0 ? m_ActiveDocument - 1 : 0;
	}

	void ScriptEditorPanel::RequestClose(size_t index)
	{
		if (index >= m_Documents.size())
			return;
		if (IsDirty(*m_Documents[index]))
		{
			m_PendingClose = index;
			m_OpenCloseDialog = true;
			return;
		}
		CloseDocument(index);
	}

	void ScriptEditorPanel::ConfigureEditor(Document& document)
	{
		TextEditor& editor = *document.Editor;
		switch (document.Language)
		{
			case ScriptLanguage::ForgeScript:
				editor.SetLanguage(GetForgeScriptLanguage());
				editor.SetInsertSpacesOnTabs(false);
				break;
			case ScriptLanguage::Python:
				editor.SetLanguage(TextEditorPython::getDefinition());
				editor.SetInsertSpacesOnTabs(true);	// PEP 8
				break;
			case ScriptLanguage::None:
				if (IsMarkdown(document.Path))
					editor.SetLanguage(TextEditorMarkdown::getDefinition());
				editor.SetWordWrapEnabled(true);
				break;
		}
		editor.SetTabSize(4);
		editor.SetPalette(GetPalette());
		editor.SetShowWhitespacesEnabled(false);
		editor.SetShowLineNumbersEnabled(true);
		editor.SetLineFoldingEnabled(document.Language != ScriptLanguage::None);
		editor.SetShowScrollbarMiniMapEnabled(true);
		editor.SetLineSpacing(1.15f);

		Document* target = &document;
		editor.SetChangeCallback([target] { target->NeedsAnalysis = true; }, c_AnalysisDelay);

		if (document.Language == ScriptLanguage::ForgeScript)
		{
			TextEditor::AutoCompleteConfig autoComplete;
			autoComplete.triggerCharacters = ".";
			autoComplete.suggestionWidth = 48;
			autoComplete.callback = [this, target](TextEditor::AutoCompleteState& state)
			{
				SuggestForgeScript(GetForgeScriptAssist(), target->Path, *target->Editor, state);
			};
			editor.SetAutoCompleteConfig(&autoComplete);
			editor.SetTextHoverCallback([this, target](TextEditor::PopupData& data) { DrawForgeScriptHover(*target, data.pos.line, data.pos.index); });
			if (CanHaveBreakpoints(document))
			{
				editor.SetLineDecorator(c_GutterGlyphs, [this, target](TextEditor::Decorator& decorator)
				{
					DrawBreakpointGutter(*target, decorator.line, decorator.width, decorator.height);
				});
			}
		}
		else if (document.Language == ScriptLanguage::Python)
		{
			TextEditor::AutoCompleteConfig autoComplete;
			autoComplete.callback = SuggestWords;
			autoComplete.userData = document.Editor.get();
			editor.SetAutoCompleteConfig(&autoComplete);
		}
	}

	ForgeScriptAssist& ScriptEditorPanel::GetForgeScriptAssist()
	{
		// The engine API description sits next to the editor (copied there by the build and the package).
		if (!m_ForgeScriptAssist)
			m_ForgeScriptAssist = CreateScope<ForgeScriptAssist>(Platform::GetExecutableDirectory() / "forgescript-api.json");
		return *m_ForgeScriptAssist;
	}

	void ScriptEditorPanel::DrawForgeScriptHover(Document& document, size_t line, size_t column)
	{
		const std::string text = document.Editor->GetText();
		if (line != document.HoverLine || column != document.HoverColumn || text != document.HoverSource)
		{
			document.HoverLine = line;
			document.HoverColumn = column;
			document.HoverSource = text;
			document.HoverText = GetForgeScriptAssist().Hover(document.Path, text, line, column);
		}
		if (document.HoverText.empty())
		{
			ImGui::CloseCurrentPopup();
			return;
		}

		// The signature (up to the first blank line) in the code font's colors, then the documentation.
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
		bool signature = true;
		size_t start = 0;
		while (start <= document.HoverText.size())
		{
			size_t end = document.HoverText.find('\n', start);
			if (end == std::string::npos)
				end = document.HoverText.size();
			const std::string_view part(document.HoverText.data() + start, end - start);
			if (part.empty())
			{
				signature = false;
				ImGui::Spacing();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, signature ? Theme::Accent : Theme::Text);
				ImGui::TextUnformatted(part.data(), part.data() + part.size());
				ImGui::PopStyleColor();
			}
			start = end + 1;
		}
		ImGui::PopTextWrapPos();
	}

	void ScriptEditorPanel::Analyze(Document& document)
	{
		document.NeedsAnalysis = false;
		const std::string text = document.Editor->GetText();
		if (document.Language == ScriptLanguage::None)
		{
			document.Outline = IsMarkdown(document.Path) ? ParseMarkdownOutline(text) : std::vector<ScriptOutlineItem>();
			return;
		}

		document.Outline = ParseScriptOutline(document.Language, text);
		document.Diagnostics = CheckScript(document.Language, text, document.Name);
		UpdateMarkers(document);
		// Edits move breakpoints with their lines (or remove them with a deleted line).
		SaveBreakpoints(document);
	}

	void ScriptEditorPanel::UpdateMarkers(Document& document)
	{
		TextEditor& editor = *document.Editor;
		editor.ClearMarkers();
		editor.ClearSquiggles();
		for (const ScriptDiagnostic& diagnostic : document.Diagnostics)
		{
			if (diagnostic.Line == 0 || diagnostic.Line > editor.GetLineCount())
				continue;
			const size_t line = diagnostic.Line - 1;
			const ImU32 color = diagnostic.IsWarning ? Theme::Warning : Theme::Error;
			const ImU32 lineColor = diagnostic.IsWarning ? IM_COL32(240, 185, 64, 30) : IM_COL32(236, 84, 84, 36);
			editor.AddMarker(line, color, lineColor, diagnostic.Message, diagnostic.Message);

			const size_t lineLength = editor.GetLineCodePoints(line).size();
			const size_t start = diagnostic.Column > 0 ? std::min(diagnostic.Column - 1, lineLength) : 0;
			const size_t end = std::max(start + 1, lineLength);
			editor.AddSquiggle(TextEditor::DocPos(line, start), TextEditor::DocPos(line, end), 0, color, diagnostic.Message);
		}

		const ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		if (debugger.IsStopped())
		{
			const int stopLine = debugger.GetStop().Line;
			if (stopLine > 0 && IsStoppedAt(document, static_cast<size_t>(stopLine - 1)))
			{
				constexpr const char* message = "The script is stopped before this line. Continue with F5, or step with F10 and F11.";
				editor.AddMarker(static_cast<size_t>(stopLine - 1), Theme::Warning, IM_COL32(240, 185, 64, 46), message, message);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Breakpoints
	// -------------------------------------------------------------------------

	bool ScriptEditorPanel::CanHaveBreakpoints(const Document& document) const
	{
		// The game knows scripts by asset path, so only the project's ForgeScript files can stop.
		return document.Language == ScriptLanguage::ForgeScript && !document.AssetPath.empty() && !document.ReadOnly;
	}

	void ScriptEditorPanel::ToggleBreakpoint(Document& document, size_t line)
	{
		if (!CanHaveBreakpoints(document) || line >= document.Editor->GetLineCount())
			return;
		TextEditor& editor = *document.Editor;
		editor.SetUserData(line, editor.GetUserData(line) == &s_BreakpointMark ? nullptr : &s_BreakpointMark);
		SaveBreakpoints(document);
	}

	void ScriptEditorPanel::ShowBreakpoints(Document& document)
	{
		TextEditor& editor = *document.Editor;
		const std::vector<int>& lines = m_Editor.GetScriptDebugger().GetBreakpoints(document.AssetPath);
		const size_t lineCount = editor.GetLineCount();
		for (size_t line = 0; line < lineCount; line++)
		{
			const bool marked = CanHaveBreakpoints(document) && std::binary_search(lines.begin(), lines.end(), static_cast<int>(line + 1));
			editor.SetUserData(line, marked ? &s_BreakpointMark : nullptr);
		}
	}

	void ScriptEditorPanel::SaveBreakpoints(Document& document)
	{
		if (!CanHaveBreakpoints(document))
			return;
		std::vector<int> lines;
		document.Editor->IterateUserData([&lines](size_t line, void* data)
		{
			if (data == &s_BreakpointMark)
				lines.push_back(static_cast<int>(line + 1));
		});
		m_Editor.GetScriptDebugger().SetBreakpoints(document.AssetPath, std::move(lines));
	}

	bool ScriptEditorPanel::IsStoppedAt(const Document& document, size_t line) const
	{
		const ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		return debugger.IsStopped() && debugger.GetStop().Line == static_cast<int>(line + 1)
			&& !document.AssetPath.empty() && debugger.GetStop().File == document.AssetPath;
	}

	void ScriptEditorPanel::DrawBreakpointGutter(Document& document, size_t line, float width, float height)
	{
		const ImVec2 position = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##Breakpoint", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			ToggleBreakpoint(document, line);

		const bool marked = document.Editor->GetUserData(line) == &s_BreakpointMark;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 center(position.x + width * 0.5f, position.y + height * 0.5f);
		const float radius = std::min(width, height) * 0.3f;
		if (marked)
			drawList->AddCircleFilled(center, radius, Theme::Error);
		else if (hovered)
			drawList->AddCircleFilled(center, radius, IM_COL32(236, 84, 84, 90));

		if (IsStoppedAt(document, line))
		{
			// An arrow over the dot: where the script is held.
			const float half = height * 0.32f;
			drawList->AddTriangleFilled(ImVec2(center.x - half * 0.8f, center.y - half), ImVec2(center.x - half * 0.8f, center.y + half),
				ImVec2(center.x + half, center.y), Theme::Warning);
		}

		if (hovered)
			UI::Tooltip(marked ? "Breakpoint: the game stops before this line runs. Click to remove (F9)."
				: "Click to add a breakpoint (F9). The game stops before this line runs.");
	}

	void ScriptEditorPanel::CheckDiskChanges()
	{
		const double now = ImGui::GetTime();
		if (now - m_LastDiskCheck < c_DiskCheckInterval)
			return;
		m_LastDiskCheck = now;

		for (const Scope<Document>& document : m_Documents)
		{
			const auto time = GetWriteTime(document->Path);
			if (time == document->DiskTime || time == std::filesystem::file_time_type())
				continue;
			if (IsDirty(*document))
			{
				document->ChangedOnDisk = true;
				document->DiskTime = time;	// Ask once per outside change
				continue;
			}

			// Unchanged here, so take the new version and stay where the cursor was.
			const TextEditor::DocPos cursor = document->Editor->GetMainCursorPosition();
			if (LoadDocument(*document))
			{
				document->Editor->SetCursor(cursor);
				BW_INFO("Reloaded {} (changed outside the editor)", document->Name);
			}
		}
	}

	void ScriptEditorPanel::GoToLine(Document& document, size_t line, size_t column)
	{
		if (line == 0)
			return;
		const size_t index = std::min(line - 1, document.Editor->GetLineCount() - 1);
		document.Editor->SetCursor(TextEditor::DocPos(index, column > 0 ? column - 1 : 0));
		document.Editor->ScrollToLine(index);
		m_FocusEditor = true;
	}

	std::filesystem::path ScriptEditorPanel::FindDocumentation(const std::string& relativePath) const
	{
		// Packages keep the guides in "Docs" next to the editor; a build tree has them in the repository's "docs".
		std::filesystem::path directory = Platform::GetExecutableDirectory();
		for (int depth = 0; depth < 5 && !directory.empty(); depth++)
		{
			for (const char* folder : { "Docs", "docs" })
			{
				const std::filesystem::path candidate = directory / folder / relativePath;
				if (FileSystem::Exists(candidate))
					return candidate;
			}
			if (directory == directory.parent_path())
				break;
			directory = directory.parent_path();
		}
		return {};
	}

	// -------------------------------------------------------------------------
	// Dialogs
	// -------------------------------------------------------------------------

	void ScriptEditorPanel::ShowCreateDialog(ScriptLanguage language)
	{
		m_AttachEntity = 0;
		m_AttachEntityName.clear();
		m_CreateLanguage = language == ScriptLanguage::None ? ScriptLanguage::ForgeScript : language;
		m_CreateTemplate = 0;
		m_CreatePath = std::string(c_ScriptFolder) + "/NewScript" + GetScriptExtension(m_CreateLanguage);
		m_OpenCreateDialog = true;
	}

	void ScriptEditorPanel::ShowAttachDialog(Entity entity)
	{
		if (!entity)
			return;
		m_AttachEntity = entity.GetUUID();
		m_AttachEntityName = entity.GetName();
		m_CreateLanguage = ScriptLanguage::ForgeScript;
		m_CreateTemplate = 0;
		m_CreatePath = std::string(c_ScriptFolder) + "/" + MakeScriptClassName(entity.GetName()) + GetScriptExtension(m_CreateLanguage);
		m_OpenCreateDialog = true;
	}

	void ScriptEditorPanel::DrawCreateDialog()
	{
		const char* title = m_AttachEntity ? "Attach Script" : "New Script";
		const std::string popupId = std::string(title) + "###ScriptCreateDialog";
		if (m_OpenCreateDialog)
		{
			ImGui::OpenPopup(popupId.c_str());
			m_OpenCreateDialog = false;
		}

		const float scale = Theme::GetScale();
		ImGui::SetNextWindowSize(ImVec2(480.0f * scale, 0.0f));
		if (!ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_AttachEntity)
		{
			ImGui::AlignTextToFramePadding();
			UI::TextMuted("Entity");
			ImGui::SameLine(110.0f * scale);
			ImGui::TextUnformatted(m_AttachEntityName.c_str());
		}

		// Language: switching keeps the file name and swaps the extension.
		ImGui::AlignTextToFramePadding();
		UI::TextMuted("Language");
		ImGui::SameLine(110.0f * scale);
		for (ScriptLanguage language : { ScriptLanguage::ForgeScript, ScriptLanguage::Python })
		{
			if (ImGui::RadioButton(GetScriptLanguageName(language), m_CreateLanguage == language) && m_CreateLanguage != language)
			{
				m_CreateLanguage = language;
				std::filesystem::path path(m_CreatePath);
				path.replace_extension(GetScriptExtension(language));
				m_CreatePath = path.generic_string();
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();

		const std::string trimmedPath = [this]
		{
			std::string path = m_CreatePath;
			while (!path.empty() && (path.back() == ' ' || path.back() == '/'))
				path.pop_back();
			while (!path.empty() && (path.front() == ' ' || path.front() == '/'))
				path.erase(path.begin());
			return path;
		}();
		const std::filesystem::path absolute = AssetManager::GetAbsolutePath(trimmedPath);
		const bool inProject = !trimmedPath.empty() && !AssetManager::GetAssetPath(absolute).empty() && !std::filesystem::path(trimmedPath).is_absolute();
		const bool rightExtension = GetScriptLanguage(absolute) == m_CreateLanguage;
		const bool exists = inProject && FileSystem::Exists(absolute);

		ImGui::AlignTextToFramePadding();
		UI::TextMuted("Template");
		ImGui::SameLine(110.0f * scale);
		ImGui::BeginDisabled(exists);
		const char* templates[] = { "Default (common events)", "Empty" };
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::Combo("##template", &m_CreateTemplate, templates, IM_ARRAYSIZE(templates));
		ImGui::EndDisabled();

		ImGui::AlignTextToFramePadding();
		UI::TextMuted("Path");
		ImGui::SameLine(110.0f * scale);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText("##path", &m_CreatePath, ImGuiInputTextFlags_EnterReturnsTrue);

		// Explains what the button will do, like Godot's Attach Node Script dialog.
		ImGui::Spacing();
		const bool valid = inProject && rightExtension;
		const char* message;
		ImU32 color = Theme::TextMuted;
		if (!inProject)
		{
			message = "Enter a path inside the project's Assets folder, like Scripts/Player.forge.";
			color = Theme::Error;
		}
		else if (!rightExtension)
		{
			message = m_CreateLanguage == ScriptLanguage::ForgeScript ? "ForgeScript files end in .forge." : "Python files end in .py.";
			color = Theme::Error;
		}
		else if (exists)
		{
			message = m_AttachEntity ? "This script already exists. It will be attached as it is." : "This script already exists. It will be opened.";
			color = Theme::Info;
		}
		else
		{
			message = m_AttachEntity ? "A new script will be created and attached." : "A new script will be created.";
			color = Theme::Success;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(color));
		ImGui::TextWrapped("%s", message);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		const char* action = exists ? (m_AttachEntity ? "Load" : "Open") : "Create";
		ImGui::BeginDisabled(!valid);
		const bool confirmed = ImGui::Button(action, ImVec2(110.0f * scale, 0.0f)) || (enter && valid);
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(110.0f * scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();

		if (confirmed && valid)
		{
			bool ready = exists;
			if (!exists)
			{
				const ScriptTemplate scriptTemplate = m_CreateTemplate == 1 ? ScriptTemplate::Empty : ScriptTemplate::Default;
				ready = FileSystem::WriteTextFile(absolute, MakeScriptSource(m_CreateLanguage, scriptTemplate, absolute.stem().string()));
				if (!ready)
					BW_ERROR("Could not create '{}'", trimmedPath);
			}

			if (ready)
			{
				const std::string assetPath = AssetManager::GetAssetPath(absolute);
				if (m_AttachEntity)
				{
					const Ref<Scene>& scene = m_Editor.GetEditorScene();
					if (Entity entity = scene ? scene->TryGetEntityByUUID(m_AttachEntity) : Entity())
						m_Editor.AttachScript(entity, assetPath);
					else
						BW_WARN("The entity no longer exists, so the script was only created");
				}
				OpenScript(absolute);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ScriptEditorPanel::DrawCloseDialog()
	{
		if (m_OpenCloseDialog)
		{
			ImGui::OpenPopup("Unsaved Changes");
			m_OpenCloseDialog = false;
		}

		const float scale = Theme::GetScale();
		ImGui::SetNextWindowSize(ImVec2(420.0f * scale, 0.0f));
		if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_PendingClose >= m_Documents.size())
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		Document& document = *m_Documents[m_PendingClose];
		ImGui::TextWrapped("Save changes to '%s' before closing?", document.Name.c_str());
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::Accent));
		const bool save = ImGui::Button(ICON_SAVE "  Save", ImVec2(110.0f * scale, 0.0f));
		ImGui::PopStyleColor();
		ImGui::SameLine();
		const bool discard = ImGui::Button("Don't Save", ImVec2(110.0f * scale, 0.0f));
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel", ImVec2(110.0f * scale, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);

		if ((save && SaveDocument(document)) || discard)
		{
			CloseDocument(m_PendingClose);
			m_PendingClose = SIZE_MAX;
			ImGui::CloseCurrentPopup();
		}
		else if (cancel)
		{
			m_PendingClose = SIZE_MAX;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// -------------------------------------------------------------------------
	// UI
	// -------------------------------------------------------------------------

	void ScriptEditorPanel::OnImGuiRender()
	{
		// Share the viewport's dock node, so the Scene and Script workspaces are tabs of the same area.
		// Layouts saved before the Script workspace existed hid that node's tab bar; show it again.
		if (ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport"); viewport && viewport->DockNode)
		{
			viewport->DockNode->LocalFlags &= ~ImGuiDockNodeFlags_NoTabBar;
			ImGui::SetNextWindowDockID(viewport->DockId, ImGuiCond_FirstUseEver);
		}

		if (m_FocusRequested)
		{
			ImGui::SetNextWindowFocus();
			m_FocusRequested = false;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		m_Visible = ImGui::Begin(WindowName, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
			| ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::PopStyleVar();
		m_Focused = m_Visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		CheckDiskChanges();

		// Breakpoints changed elsewhere (the Debugger panel, automation) are shown before any analysis
		// below saves the documents' breakpoint lines, so an edit in the same frame cannot undo them.
		const ScriptDebugger& debugger = m_Editor.GetScriptDebugger();
		const bool breakpointsChanged = debugger.GetBreakpointRevision() != m_ShownBreakpoints;
		const bool stopChanged = debugger.IsStopped() != m_ShownStopped || debugger.GetStopCount() != m_ShownStop;
		m_ShownBreakpoints = debugger.GetBreakpointRevision();
		m_ShownStopped = debugger.IsStopped();
		m_ShownStop = debugger.GetStopCount();
		for (const Scope<Document>& document : m_Documents)
		{
			if (breakpointsChanged)
				ShowBreakpoints(*document);
			if (document->NeedsAnalysis)
				Analyze(*document);
			else if (stopChanged)
				UpdateMarkers(*document);
		}

		if (m_Visible)
		{
			DrawMenuBar();
			HandleShortcuts();

			if (m_Documents.empty())
			{
				DrawEmptyState();
			}
			else if (ImGui::BeginTable("##ScriptLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX))
			{
				const float scale = Theme::GetScale();
				ImGui::TableSetupColumn("Scripts", ImGuiTableColumnFlags_WidthFixed, 210.0f * scale);
				ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawSidebar();
				ImGui::TableSetColumnIndex(1);
				if (Document* document = GetActiveDocument())
					DrawEditor(*document);
				ImGui::EndTable();
			}
		}
		ImGui::End();

		DrawCloseDialog();
		DrawCreateDialog();
	}

	void ScriptEditorPanel::DrawMenuBar()
	{
		if (!ImGui::BeginMenuBar())
			return;

		Document* document = GetActiveDocument();
		const bool canEdit = document && !document->ReadOnly;
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem(ICON_FILE_PLUS "  New Script..."))
				ShowCreateDialog(ScriptLanguage::ForgeScript);
			if (ImGui::MenuItem(ICON_FOLDER_OPEN "  Open..."))
			{
				const std::string file = UI::OpenFileDialog("Open script", { "Scripts", "*.forge *.py", "All files", "*" },
					AssetManager::GetAssetDirectory().string());
				if (!file.empty())
					OpenScript(file);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_SAVE "  Save", "Ctrl+S", false, canEdit))
				SaveDocument(*document);
			if (ImGui::MenuItem(ICON_SAVE_ALL "  Save All", "Ctrl+Shift+S", false, HasUnsavedChanges()))
				SaveAll();
			if (ImGui::MenuItem(ICON_REFRESH_CW "  Reload from Disk", nullptr, false, document != nullptr))
				LoadDocument(*document);
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_COPY "  Copy Script Path", nullptr, false, document != nullptr))
				ImGui::SetClipboardText(document->AssetPath.empty() ? document->Path.string().c_str() : document->AssetPath.c_str());
			if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Open in External Editor", nullptr, false, document != nullptr))
				Platform::OpenForEditing(document->Path);
			ImGui::Separator();
			if (ImGui::MenuItem(ICON_X "  Close", "Ctrl+W", false, document != nullptr))
				RequestClose(m_ActiveDocument);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			TextEditor* editor = document ? document->Editor.get() : nullptr;
			if (ImGui::MenuItem(ICON_UNDO_2 "  Undo", "Ctrl+Z", false, editor && editor->CanUndo()))
				editor->Undo();
			if (ImGui::MenuItem(ICON_REDO_2 "  Redo", "Ctrl+Y", false, editor && editor->CanRedo()))
				editor->Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "Ctrl+X", false, canEdit))
				editor->Cut();
			if (ImGui::MenuItem(ICON_COPY "  Copy", "Ctrl+C", false, editor != nullptr))
				editor->Copy();
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, canEdit))
				editor->Paste();
			if (ImGui::MenuItem("Select All", "Ctrl+A", false, editor != nullptr))
				editor->SelectAll();
			ImGui::Separator();
			if (ImGui::MenuItem("Toggle Comment", "Ctrl+/", false, canEdit))
				editor->ToggleComments();
			if (ImGui::MenuItem("Indent", "Ctrl+]", false, canEdit))
				editor->IndentLines();
			if (ImGui::MenuItem("Unindent", "Ctrl+[", false, canEdit))
				editor->DeindentLines();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Search"))
		{
			if (ImGui::MenuItem(ICON_SEARCH "  Find and Replace...", "Ctrl+F", false, document != nullptr))
				document->Editor->OpenFindReplaceWindow();
			if (ImGui::MenuItem("Find Next", "Ctrl+G", false, document && document->Editor->HasFindString()))
				document->Editor->FindNext();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			DrawHelpMenuItems();
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void ScriptEditorPanel::DrawHelpMenuItems()
	{
		struct Guide { const char* Label; const char* Path; };
		const Guide guides[] = {
			{ ICON_BOOK_OPEN "  ForgeScript Language Guide", "ForgeScript/LanguageGuide.md" },
			{ ICON_SCROLL_TEXT "  ForgeScript Reference", "ForgeScript/Reference.md" },
			{ ICON_BOOK_OPEN "  Python Scripting API", "ScriptingAPI.md" },
			{ ICON_BOT "  Editor Automation API", "EditorAutomation.md" },
		};
		for (const Guide& guide : guides)
		{
			const std::filesystem::path path = FindDocumentation(guide.Path);
			if (ImGui::MenuItem(guide.Label, nullptr, false, !path.empty()))
				OpenReadOnly(path);
		}
	}

	void ScriptEditorPanel::DrawSidebar()
	{
		const float scale = Theme::GetScale();
		const float available = ImGui::GetContentRegionAvail().y;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * scale, 6.0f * scale));

		// Open scripts, filterable like Godot's script list.
		ImGui::BeginChild("##ScriptList", ImVec2(0.0f, available * 0.45f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Filter scripts", &m_ScriptFilter);
		const std::string filter = ToLower(m_ScriptFilter);
		size_t closeIndex = SIZE_MAX;
		for (size_t i = 0; i < m_Documents.size(); i++)
		{
			Document& document = *m_Documents[i];
			if (!filter.empty() && ToLower(document.Name).find(filter) == std::string::npos)
				continue;

			ImGui::PushID(static_cast<int>(i));
			const bool dirty = IsDirty(document);
			const bool hasErrors = HasScriptMistakes(document.Diagnostics);

			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			if (ImGui::Selectable("##script", i == m_ActiveDocument, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, ImGui::GetFrameHeight())))
			{
				m_ActiveDocument = i;
				m_FocusEditor = true;
			}
			if (ImGui::IsItemHovered())
			{
				const std::string tooltip = (document.AssetPath.empty() ? document.Path.string() : document.AssetPath) + (dirty ? "  (unsaved changes)" : "");
				UI::Tooltip(tooltip.c_str());
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
					closeIndex = i;
			}
			if (ImGui::BeginPopupContextItem("##ScriptContext"))
			{
				if (ImGui::MenuItem(ICON_SAVE "  Save", nullptr, false, dirty))
					SaveDocument(document);
				if (ImGui::MenuItem(ICON_COPY "  Copy Script Path"))
					ImGui::SetClipboardText(document.AssetPath.empty() ? document.Path.string().c_str() : document.AssetPath.c_str());
				if (ImGui::MenuItem(ICON_EXTERNAL_LINK "  Open in External Editor"))
					Platform::OpenForEditing(document.Path);
				ImGui::Separator();
				if (ImGui::MenuItem(ICON_X "  Close"))
					closeIndex = i;
				ImGui::EndPopup();
			}

			// Icon, name, and an error dot - drawn over the selectable.
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float textY = start.y + (ImGui::GetFrameHeight() - ImGui::GetFontSize()) * 0.5f;
			drawList->AddText(ImVec2(start.x + 4.0f * scale, textY), hasErrors ? Theme::Error : LanguageColor(document.Language), LanguageIcon(document.Language));
			// Long names are clipped before the unsaved-changes dot and the close button, so both stay visible.
			const float rightEdge = ImGui::GetWindowPos().x - ImGui::GetScrollX() + rowRight - ImGui::GetFrameHeight();
			const ImVec4 clip(start.x, start.y, rightEdge - 14.0f * scale, start.y + ImGui::GetFrameHeight());
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(start.x + 24.0f * scale, textY), dirty ? Theme::Text : Theme::TextMuted,
				document.Name.c_str(), nullptr, 0.0f, &clip);
			if (dirty)
				drawList->AddCircleFilled(ImVec2(rightEdge - 7.0f * scale, start.y + ImGui::GetFrameHeight() * 0.5f), 3.5f * scale, Theme::Accent);

			// Close button on the right.
			ImGui::SameLine(rowRight - ImGui::GetFrameHeight());
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			if (ImGui::SmallButton(ICON_X))
				closeIndex = i;
			ImGui::PopStyleColor();
			if (ImGui::IsItemHovered())
				UI::Tooltip("Close (middle-click)");
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::Separator();

		// Outline of the current script: click to jump.
		ImGui::BeginChild("##Outline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		UI::SectionHeader("Outline");
		if (Document* document = GetActiveDocument())
		{
			if (document->Outline.empty())
				UI::TextMuted(document->Language == ScriptLanguage::None ? "No headings in this file." : "No functions or events yet.");
			for (size_t i = 0; i < document->Outline.size(); i++)
			{
				const ScriptOutlineItem& item = document->Outline[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::Indent(static_cast<float>(item.Depth) * 12.0f * scale + 1.0f);
				const std::string text = (item.ItemKind == ScriptOutlineItem::Kind::Event ? "when " : "") + item.Name;
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(OutlineColor(item.ItemKind)));
				ImGui::TextUnformatted(OutlineIcon(item.ItemKind));
				ImGui::PopStyleColor();
				ImGui::SameLine();
				if (ImGui::Selectable(text.c_str()))
					GoToLine(*document, item.Line);
				ImGui::Unindent(static_cast<float>(item.Depth) * 12.0f * scale + 1.0f);
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();

		if (closeIndex != SIZE_MAX)
			RequestClose(closeIndex);
	}

	void ScriptEditorPanel::DrawEditor(Document& document)
	{
		const float scale = Theme::GetScale();

		if (document.ChangedOnDisk)
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(IM_COL32(240, 185, 64, 30)));
			ImGui::BeginChild("##DiskBanner", ImVec2(0.0f, ImGui::GetFrameHeight() + 10.0f * scale), ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
			ImGui::Text(ICON_TRIANGLE_ALERT "  %s changed outside Bellows.", document.Name.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::SmallButton("Reload"))
				LoadDocument(document);
			ImGui::SameLine();
			if (ImGui::SmallButton("Keep my version"))
				document.ChangedOnDisk = false;
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		const float statusHeight = ImGui::GetFrameHeight() + 4.0f * scale;
		if (m_FocusEditor)
		{
			document.Editor->SetFocus();
			m_FocusEditor = false;
		}
		ImGui::PushFont(Theme::GetFonts().Mono, 0.0f);
		document.Editor->Render("##code", ImVec2(0.0f, -statusHeight));
		ImGui::PopFont();

		DrawStatusBar(document);
	}

	void ScriptEditorPanel::DrawStatusBar(Document& document)
	{
		const float scale = Theme::GetScale();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::ToVec4(Theme::Background));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 2.0f * scale));
		ImGui::BeginChild("##ScriptStatus", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
		ImGui::AlignTextToFramePadding();

		// Problems first: clicking one jumps to it.
		if (document.Language == ScriptLanguage::None)
		{
			UI::TextMuted(document.ReadOnly ? ICON_LOCK "  Read only" : ICON_FILE_TEXT "  Text");
		}
		else if (document.Diagnostics.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Success));
			ImGui::TextUnformatted(ICON_CIRCLE_CHECK "  No problems");
			ImGui::PopStyleColor();
		}
		else
		{
			// Mistakes and warnings never come together: a script with mistakes isn't checked for warnings.
			const ScriptDiagnostic& first = document.Diagnostics.front();
			std::string text = fmt::format("{}  Line {}: {}{}", first.IsWarning ? ICON_TRIANGLE_ALERT : ICON_CIRCLE_X, first.Line,
				first.IsWarning ? "Warning: " : "", first.Message);
			if (document.Diagnostics.size() > 1)
				text += fmt::format("  (+{} more)", document.Diagnostics.size() - 1);
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(first.IsWarning ? Theme::Warning : Theme::Error));
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
			const float maxWidth = ImGui::GetContentRegionAvail().x - 300.0f * scale;
			if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_None, ImVec2(std::max(maxWidth, 80.0f * scale), 0.0f)))
				GoToLine(document, first.Line, first.Column);
			ImGui::PopStyleColor(2);
			if (ImGui::IsItemHovered())
				UI::Tooltip(first.Message.c_str());
		}

		const TextEditor::DocPos cursor = document.Editor->GetMainCursorPosition();
		std::string right = fmt::format("Ln {}, Col {}", cursor.line + 1, cursor.index + 1);
		if (document.Language != ScriptLanguage::None)
			right += fmt::format("    {}    {}", document.Editor->IsInsertSpacesOnTabs() ? "Spaces" : "Tabs", GetScriptLanguageName(document.Language));
		const float width = ImGui::CalcTextSize(right.c_str()).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - width - 12.0f * scale);
		UI::TextMuted(right.c_str());

		ImGui::PopFont();
		ImGui::EndChild();
	}

	void ScriptEditorPanel::DrawEmptyState()
	{
		const float scale = Theme::GetScale();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float blockWidth = 420.0f * scale;
		const float blockHeight = 210.0f * scale;
		ImGui::SetCursorPos(ImVec2(std::max((available.x - blockWidth) * 0.5f, 10.0f), ImGui::GetCursorPosY() + std::max((available.y - blockHeight) * 0.5f, 10.0f)));
		ImGui::BeginGroup();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::TextUnformatted("No script open");
		ImGui::PopFont();
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + blockWidth);
		UI::TextMuted("Scripts give entities behavior. Write them here in ForgeScript or Python, "
			"then attach them to entities from the Hierarchy (right-click an entity > Attach Script) "
			"or by dragging a script onto an entity.");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::Accent));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::AccentHovered));
		if (ImGui::Button(ICON_FILE_PLUS "  New ForgeScript"))
			ShowCreateDialog(ScriptLanguage::ForgeScript);
		ImGui::PopStyleColor(2);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FILE_PLUS "  New Python Script"))
			ShowCreateDialog(ScriptLanguage::Python);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_OPEN "  Open..."))
		{
			const std::string file = UI::OpenFileDialog("Open script", { "Scripts", "*.forge *.py", "All files", "*" },
				AssetManager::GetAssetDirectory().string());
			if (!file.empty())
				OpenScript(file);
		}

		ImGui::Spacing();
		const std::filesystem::path guide = FindDocumentation("ForgeScript/LanguageGuide.md");
		if (!guide.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Info));
			if (ImGui::Selectable(ICON_BOOK_OPEN "  New to ForgeScript? Read the Language Guide", false, ImGuiSelectableFlags_None, ImVec2(blockWidth, 0.0f)))
				OpenReadOnly(guide);
			ImGui::PopStyleColor();
		}
		ImGui::EndGroup();
	}

	void ScriptEditorPanel::HandleShortcuts()
	{
		if (!m_Focused)
			return;

		// Checked directly rather than through shortcut routing: the code editor's child window owns the keyboard.
		Document* document = GetActiveDocument();
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
			SaveAll();
		else if (document && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
			SaveDocument(*document);
		else if (document && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W))
			RequestClose(m_ActiveDocument);
		else if (m_Documents.size() > 1 && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Tab))
			m_ActiveDocument = (m_ActiveDocument + 1) % m_Documents.size();
		else if (document && ImGui::IsKeyPressed(ImGuiKey_F9, false))
			ToggleBreakpoint(*document, document->Editor->GetMainCursorPosition().line);
	}

}
