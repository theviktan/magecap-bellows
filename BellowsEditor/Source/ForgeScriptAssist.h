#pragma once

#include <ForgeScript/Tools/LanguageServer.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	// Completion, hover help, find-all-references, rename and format for ForgeScript in the Script Editor. It runs the same language server
	// as 'forgescript --lsp' (ForgeScript::LanguageServer) inside the editor, so the editor and other
	// editors (MageCap Forge's Chisel) give the same answers, and it knows the engine API from
	// forgescript-api.json. UI-free, so it is unit tested.
	class ForgeScriptAssist
	{
	public:
		struct Suggestion
		{
			std::string Label;		// What the list shows: "play_sound", "save screenshot to (path)"
			std::string Insert;		// What picking it types (the label when the server gives nothing else)
			std::string Detail;		// A short signature or type, may be empty
		};

		// A span of a document; lines and columns are zero-based, columns in code points, the end not included.
		struct TextRange
		{
			size_t StartLine = 0;
			size_t StartColumn = 0;
			size_t EndLine = 0;
			size_t EndColumn = 0;
			bool operator==(const TextRange&) const = default;
		};

		struct TextEdit
		{
			TextRange Range;
			std::string NewText;
		};

		// A place a name is used, with its line's text for a list of results.
		struct Reference
		{
			std::filesystem::path File;
			TextRange Range;
			std::string LineText;
		};

		// A rename's edits for each file, or why it can't be done (and then no edits).
		struct RenameResult
		{
			std::string Error;
			std::map<std::filesystem::path, std::vector<TextEdit>> Edits;
		};

		// 'apiFile' is forgescript-api.json; without it only the language itself is known.
		explicit ForgeScriptAssist(const std::filesystem::path& apiFile);

		// Lines and columns are zero-based; columns count code points, as the code editor does.
		// 'prefix' is the word being typed: suggestions are those that start with it (ignoring case),
		// then those that contain it, each group in the server's order.
		std::vector<Suggestion> Complete(const std::filesystem::path& file, const std::string& text, size_t line, size_t column,
			const std::string& prefix);
		// Plain-text help for the name at the position: a signature line, then its documentation.
		// Empty when there is nothing to say.
		std::string Hover(const std::filesystem::path& file, const std::string& text, size_t line, size_t column);
		// Every use of the name at the position, its own definition included, in document order.
		std::vector<Reference> FindReferences(const std::filesystem::path& file, const std::string& text, size_t line, size_t column);
		// The name at the position and where it is, when it can be renamed; otherwise nothing, with the
		// server's reason in 'outError' (empty when there's no name there at all).
		std::optional<TextRange> PrepareRename(const std::filesystem::path& file, const std::string& text, size_t line, size_t column,
			std::string& outName, std::string& outError);
		// Renames the name at the position everywhere it means the same thing. The server refuses (with a
		// reason) whenever it can't be sure, and checks the renamed script means what it did.
		RenameResult Rename(const std::filesystem::path& file, const std::string& text, size_t line, size_t column, const std::string& newName);
		// The document formatted the way 'forgescript --format' does; nothing, with the reason in
		// 'outError', for a script with mistakes. The same text back when it's already formatted.
		std::optional<std::string> Format(const std::filesystem::path& file, const std::string& text, std::string& outError);

		// 'edits' made to 'text', from the last to the first, so earlier ranges stay right. Overlapping
		// edits aren't expected (the server never sends them); positions past a line or the text are held to it.
		static std::string ApplyEdits(const std::string& text, std::vector<TextEdit> edits);

		// Forgets a closed document.
		void Close(const std::filesystem::path& file);

		// A code-point column as the UTF-16 offset LSP positions use, and back.
		static size_t ToUtf16Column(const std::string& lineText, size_t column);
		static size_t FromUtf16Column(const std::string& lineText, size_t units);
		// Markdown from the server (```forge blocks, "---" separators) as plain lines.
		static std::string MarkdownToText(const std::string& markdown);
	private:
		std::string Sync(const std::filesystem::path& file, const std::string& text);
		ForgeScript::Json Request(const std::string& method, ForgeScript::Json params);
		static ForgeScript::Json Position(const std::string& uri, const std::string& text, size_t line, size_t column);
		// An LSP range in the document 'text' as code-point columns.
		static TextRange ToRange(const std::string& text, const ForgeScript::Json& range);
		static std::vector<TextEdit> ToEdits(const std::string& text, const ForgeScript::Json& edits);
		// The text of a request's error reply, or empty.
		static std::string ErrorOf(const ForgeScript::Json& reply);
	private:
		ForgeScript::LanguageServer m_Server;
		std::map<std::filesystem::path, std::pair<std::string, int>> m_Documents;	// text, version
		int m_NextRequest = 1;
	};

}
