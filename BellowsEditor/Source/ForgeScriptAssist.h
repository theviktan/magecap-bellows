#pragma once

#include <ForgeScript/Tools/LanguageServer.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Bellows {

	// Completion and hover help for ForgeScript in the Script Editor. It runs the same language server
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
		// Forgets a closed document.
		void Close(const std::filesystem::path& file);

		// A code-point column as the UTF-16 offset LSP positions use.
		static size_t ToUtf16Column(const std::string& lineText, size_t column);
		// Markdown from the server (```forge blocks, "---" separators) as plain lines.
		static std::string MarkdownToText(const std::string& markdown);
	private:
		std::string Sync(const std::filesystem::path& file, const std::string& text);
		ForgeScript::Json Request(const std::string& method, ForgeScript::Json params);
		static ForgeScript::Json Position(const std::string& uri, const std::string& text, size_t line, size_t column);
	private:
		ForgeScript::LanguageServer m_Server;
		std::map<std::filesystem::path, std::pair<std::string, int>> m_Documents;	// text, version
		int m_NextRequest = 1;
	};

}
