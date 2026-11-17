#include "ForgeScriptAssist.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Bellows {

	namespace {

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		std::string LineOf(const std::string& text, size_t line)
		{
			size_t start = 0;
			for (size_t i = 0; i < line; i++)
			{
				start = text.find('\n', start);
				if (start == std::string::npos)
					return {};
				start++;
			}
			size_t end = text.find('\n', start);
			std::string result = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (!result.empty() && result.back() == '\r')
				result.pop_back();
			return result;
		}

	}

	ForgeScriptAssist::ForgeScriptAssist(const std::filesystem::path& apiFile)
		: m_Server(ForgeScript::LanguageServer::Options { apiFile })
	{
		ForgeScript::Json params;
		params.Set("capabilities", ForgeScript::Json(ForgeScript::Json::Object {}));
		Request("initialize", std::move(params));
		ForgeScript::Json initialized;
		initialized.Set("jsonrpc", "2.0").Set("method", "initialized").Set("params", ForgeScript::Json(ForgeScript::Json::Object {}));
		m_Server.Handle(initialized);
	}

	std::vector<ForgeScriptAssist::Suggestion> ForgeScriptAssist::Complete(const std::filesystem::path& file, const std::string& text,
		size_t line, size_t column, const std::string& prefix)
	{
		const std::string uri = Sync(file, text);
		const ForgeScript::Json result = Request("textDocument/completion", Position(uri, text, line, column)).Get("result");
		const ForgeScript::Json& items = result.IsArray() ? result : result.Get("items");
		if (!items.IsArray())
			return {};

		const std::string term = Lower(prefix);
		std::vector<Suggestion> starts;
		std::vector<Suggestion> contains;
		for (const ForgeScript::Json& item : items.AsArray())
		{
			Suggestion suggestion;
			suggestion.Label = item.GetString("label");
			const ForgeScript::Json* insert = item.Find("insertText");
			suggestion.Insert = insert && insert->IsString() ? insert->AsString() : suggestion.Label;
			if (const ForgeScript::Json* detail = item.Find("detail"); detail && detail->IsString())
				suggestion.Detail = detail->AsString();
			if (suggestion.Label.empty())
				continue;
			const std::string label = Lower(suggestion.Label);
			if (label.rfind(term, 0) == 0)
				starts.push_back(std::move(suggestion));
			else if (!term.empty() && label.find(term) != std::string::npos)
				contains.push_back(std::move(suggestion));
		}
		starts.insert(starts.end(), std::make_move_iterator(contains.begin()), std::make_move_iterator(contains.end()));
		return starts;
	}

	std::string ForgeScriptAssist::Hover(const std::filesystem::path& file, const std::string& text, size_t line, size_t column)
	{
		const std::string uri = Sync(file, text);
		const ForgeScript::Json result = Request("textDocument/hover", Position(uri, text, line, column)).Get("result");
		const ForgeScript::Json& contents = result.Get("contents");
		if (contents.IsString())
			return MarkdownToText(contents.AsString());
		const ForgeScript::Json* value = contents.Find("value");
		return value && value->IsString() ? MarkdownToText(value->AsString()) : std::string();
	}

	void ForgeScriptAssist::Close(const std::filesystem::path& file)
	{
		auto it = m_Documents.find(file);
		if (it == m_Documents.end())
			return;
		ForgeScript::Json params;
		params.Set("textDocument", ForgeScript::Json().Set("uri", ForgeScript::LanguageServer::PathToUri(file)));
		ForgeScript::Json message;
		message.Set("jsonrpc", "2.0").Set("method", "textDocument/didClose").Set("params", std::move(params));
		m_Server.Handle(message);
		m_Documents.erase(it);
	}

	size_t ForgeScriptAssist::ToUtf16Column(const std::string& lineText, size_t column)
	{
		size_t units = 0;
		size_t codePoints = 0;
		for (size_t i = 0; i < lineText.size() && codePoints < column; codePoints++)
		{
			const auto lead = static_cast<unsigned char>(lineText[i]);
			const size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			units += length == 4 ? 2 : 1;		// Code points past U+FFFF take a surrogate pair
			i += length;
		}
		return units + (column > codePoints ? column - codePoints : 0);
	}

	std::string ForgeScriptAssist::MarkdownToText(const std::string& markdown)
	{
		std::istringstream lines(markdown);
		std::string line;
		std::string text;
		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.rfind("```", 0) == 0)
				continue;
			if (line == "---")
				line.clear();
			if (line.empty() && (text.empty() || text.ends_with("\n\n")))
				continue;
			text += line + "\n";
		}
		while (!text.empty() && text.back() == '\n')
			text.pop_back();
		return text;
	}

	std::string ForgeScriptAssist::Sync(const std::filesystem::path& file, const std::string& text)
	{
		const std::string uri = ForgeScript::LanguageServer::PathToUri(file);
		auto it = m_Documents.find(file);
		if (it != m_Documents.end() && it->second.first == text)
			return uri;

		ForgeScript::Json params;
		ForgeScript::Json message;
		message.Set("jsonrpc", "2.0");
		if (it == m_Documents.end())
		{
			params.Set("textDocument", ForgeScript::Json().Set("uri", uri).Set("languageId", "forgescript").Set("version", 1).Set("text", text));
			message.Set("method", "textDocument/didOpen");
			m_Documents[file] = { text, 1 };
		}
		else
		{
			const int version = ++it->second.second;
			it->second.first = text;
			params.Set("textDocument", ForgeScript::Json().Set("uri", uri).Set("version", version));
			params.Set("contentChanges", ForgeScript::Json(ForgeScript::Json::Array { ForgeScript::Json().Set("text", text) }));
			message.Set("method", "textDocument/didChange");
		}
		message.Set("params", std::move(params));
		// The diagnostics it sends back are not needed: the editor checks scripts with the engine itself.
		m_Server.Handle(message);
		return uri;
	}

	ForgeScript::Json ForgeScriptAssist::Request(const std::string& method, ForgeScript::Json params)
	{
		const int id = m_NextRequest++;
		ForgeScript::Json message;
		message.Set("jsonrpc", "2.0").Set("id", id).Set("method", method).Set("params", std::move(params));
		for (ForgeScript::Json& reply : m_Server.Handle(message))
		{
			if (const ForgeScript::Json* replyId = reply.Find("id"); replyId && replyId->AsNumber(-1.0) == id)
				return reply;
		}
		return {};
	}

	ForgeScript::Json ForgeScriptAssist::Position(const std::string& uri, const std::string& text, size_t line, size_t column)
	{
		ForgeScript::Json position;
		position.Set("line", line).Set("character", ToUtf16Column(LineOf(text, line), column));
		ForgeScript::Json params;
		params.Set("textDocument", ForgeScript::Json().Set("uri", uri)).Set("position", std::move(position));
		return params;
	}

}
