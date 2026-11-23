#include "ForgeScriptAssist.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <tuple>

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

	std::vector<ForgeScriptAssist::Reference> ForgeScriptAssist::FindReferences(const std::filesystem::path& file, const std::string& text,
		size_t line, size_t column)
	{
		const std::string uri = Sync(file, text);
		ForgeScript::Json params = Position(uri, text, line, column);
		params.Set("context", ForgeScript::Json().Set("includeDeclaration", true));
		const ForgeScript::Json result = Request("textDocument/references", std::move(params)).Get("result");
		std::vector<Reference> references;
		if (!result.IsArray())
			return references;
		for (const ForgeScript::Json& location : result.AsArray())
		{
			// The server answers for this document; another document's text isn't known here.
			if (location.GetString("uri") != uri)
				continue;
			Reference reference;
			reference.File = file;
			reference.Range = ToRange(text, location.Get("range"));
			reference.LineText = LineOf(text, reference.Range.StartLine);
			references.push_back(std::move(reference));
		}
		std::sort(references.begin(), references.end(), [](const Reference& a, const Reference& b)
		{
			return std::tie(a.Range.StartLine, a.Range.StartColumn) < std::tie(b.Range.StartLine, b.Range.StartColumn);
		});
		return references;
	}

	std::optional<ForgeScriptAssist::TextRange> ForgeScriptAssist::PrepareRename(const std::filesystem::path& file, const std::string& text,
		size_t line, size_t column, std::string& outName, std::string& outError)
	{
		outName.clear();
		outError.clear();
		const std::string uri = Sync(file, text);
		const ForgeScript::Json reply = Request("textDocument/prepareRename", Position(uri, text, line, column));
		outError = ErrorOf(reply);
		const ForgeScript::Json& result = reply.Get("result");
		if (!outError.empty() || !result.IsObject())
			return std::nullopt;
		outName = result.GetString("placeholder");
		return ToRange(text, result.Get("range"));
	}

	ForgeScriptAssist::RenameResult ForgeScriptAssist::Rename(const std::filesystem::path& file, const std::string& text, size_t line, size_t column,
		const std::string& newName)
	{
		RenameResult rename;
		const std::string uri = Sync(file, text);
		ForgeScript::Json params = Position(uri, text, line, column);
		params.Set("newName", newName);
		const ForgeScript::Json reply = Request("textDocument/rename", std::move(params));
		rename.Error = ErrorOf(reply);
		if (!rename.Error.empty())
			return rename;
		const ForgeScript::Json& changes = reply.Get("result").Get("changes");
		if (!changes.IsObject())
		{
			rename.Error = "There's no name here to rename.";
			return rename;
		}
		for (const auto& [changedUri, edits] : changes.AsObject())
		{
			// Only this document's text is known, so a rename reaching another file is refused whole.
			if (changedUri != uri)
			{
				rename.Edits.clear();
				rename.Error = "This rename would change another script too, which the Script Editor can't do yet. Rename it in each script.";
				return rename;
			}
			rename.Edits[file] = ToEdits(text, edits);
		}
		return rename;
	}

	std::optional<std::string> ForgeScriptAssist::Format(const std::filesystem::path& file, const std::string& text, std::string& outError)
	{
		outError.clear();
		const std::string uri = Sync(file, text);
		ForgeScript::Json options;
		options.Set("tabSize", 4).Set("insertSpaces", false);
		ForgeScript::Json params;
		params.Set("textDocument", ForgeScript::Json().Set("uri", uri)).Set("options", std::move(options));
		const ForgeScript::Json reply = Request("textDocument/formatting", std::move(params));
		outError = ErrorOf(reply);
		if (!outError.empty())
			return std::nullopt;
		const ForgeScript::Json& result = reply.Get("result");
		if (!result.IsArray())
			return text;
		return ApplyEdits(text, ToEdits(text, result));
	}

	std::string ForgeScriptAssist::ApplyEdits(const std::string& text, std::vector<TextEdit> edits)
	{
		// Line starts, so a line:column position becomes a byte offset.
		std::vector<size_t> lineStarts { 0 };
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '\n')
				lineStarts.push_back(i + 1);
		}
		auto offsetOf = [&](size_t line, size_t column)
		{
			if (line >= lineStarts.size())
				return text.size();
			const size_t start = lineStarts[line];
			size_t end = line + 1 < lineStarts.size() ? lineStarts[line + 1] - 1 : text.size();
			if (end > start && text[end - 1] == '\r')
				end--;
			size_t offset = start;
			for (size_t codePoints = 0; codePoints < column && offset < end; codePoints++)
			{
				const auto lead = static_cast<unsigned char>(text[offset]);
				offset += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			}
			return std::min(offset, end);
		};
		std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b)
		{
			return std::tie(a.Range.StartLine, a.Range.StartColumn) > std::tie(b.Range.StartLine, b.Range.StartColumn);
		});
		std::string result = text;
		for (const TextEdit& edit : edits)
		{
			const size_t from = offsetOf(edit.Range.StartLine, edit.Range.StartColumn);
			const size_t to = std::max(from, offsetOf(edit.Range.EndLine, edit.Range.EndColumn));
			result.replace(from, to - from, edit.NewText);
		}
		return result;
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

	size_t ForgeScriptAssist::FromUtf16Column(const std::string& lineText, size_t units)
	{
		size_t counted = 0;
		size_t codePoints = 0;
		for (size_t i = 0; i < lineText.size() && counted < units; codePoints++)
		{
			const auto lead = static_cast<unsigned char>(lineText[i]);
			const size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			counted += length == 4 ? 2 : 1;
			i += length;
		}
		return codePoints + (units > counted ? units - counted : 0);
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

	ForgeScriptAssist::TextRange ForgeScriptAssist::ToRange(const std::string& text, const ForgeScript::Json& range)
	{
		auto read = [&](const ForgeScript::Json& position, size_t& outLine, size_t& outColumn)
		{
			const double line = position.Get("line").AsNumber(0.0);
			const double character = position.Get("character").AsNumber(0.0);
			outLine = line > 0.0 ? static_cast<size_t>(line) : 0;
			outColumn = FromUtf16Column(LineOf(text, outLine), character > 0.0 ? static_cast<size_t>(character) : 0);
		};
		TextRange result;
		read(range.Get("start"), result.StartLine, result.StartColumn);
		read(range.Get("end"), result.EndLine, result.EndColumn);
		return result;
	}

	std::vector<ForgeScriptAssist::TextEdit> ForgeScriptAssist::ToEdits(const std::string& text, const ForgeScript::Json& edits)
	{
		std::vector<TextEdit> result;
		if (!edits.IsArray())
			return result;
		for (const ForgeScript::Json& edit : edits.AsArray())
			result.push_back({ ToRange(text, edit.Get("range")), edit.GetString("newText") });
		return result;
	}

	std::string ForgeScriptAssist::ErrorOf(const ForgeScript::Json& reply)
	{
		const ForgeScript::Json* error = reply.Find("error");
		return error ? error->GetString("message") : std::string();
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
