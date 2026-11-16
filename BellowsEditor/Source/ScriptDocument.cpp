#include "ScriptDocument.h"

#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/Scripting/ScriptEngine.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <ForgeScript/VM/VM.h>
#include <pybind11/pybind11.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace py = pybind11;

namespace Bellows {

	namespace {

		constexpr const char* c_ForgeScriptDefault =
R"(# {Name} - a ForgeScript script. Attach it to an entity to give the entity behavior.
# Variables set at the top become settings you can change in the Inspector.
# Write 2.0 rather than 2 so the Inspector accepts decimals like 2.5.
set speed to 2.0

when created
	# Runs once, when the game starts.
	say "{Name} is ready"
end

when update with delta
	# Runs every frame. delta is the time since the last frame, in seconds.
end
)";

		constexpr const char* c_ForgeScriptEmpty =
R"(# {Name}

when update with delta
end
)";

		constexpr const char* c_PythonDefault =
R"(import bellows


class {Class}(bellows.Script):
    """Attach this script to an entity with a Script component."""

    # Annotated class attributes become editable fields in the Inspector.
    speed: float = 2.0

    def on_create(self):
        pass

    def on_update(self, dt: float):
        pass
)";

		constexpr const char* c_PythonEmpty =
R"(import bellows


class {Class}(bellows.Script):
    pass
)";

		ForgeScriptCheckSetup s_ForgeScriptCheckSetup = nullptr;
		std::unique_ptr<ForgeScript::VM> s_CheckVM;

		std::string ToLower(std::string_view text)
		{
			std::string result(text);
			for (char& c : result)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return result;
		}

		void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
		{
			size_t position = 0;
			while ((position = text.find(from, position)) != std::string::npos)
			{
				text.replace(position, from.size(), to);
				position += to.size();
			}
		}

		// Number of indentation levels at the start of a line. A tab is one level; spaces count in fours.
		int IndentationDepth(std::string_view line)
		{
			int tabs = 0;
			int spaces = 0;
			for (char c : line)
			{
				if (c == '\t')
					tabs++;
				else if (c == ' ')
					spaces++;
				else
					break;
			}
			return tabs + spaces / 4;
		}

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);
			return text;
		}

		bool StartsWithWord(std::string_view text, std::string_view word)
		{
			return text.size() > word.size() && text.substr(0, word.size()) == word
				&& std::isspace(static_cast<unsigned char>(text[word.size()]));
		}

		// The identifier at the start of 'text' (letters, digits, underscores).
		std::string_view LeadingIdentifier(std::string_view text)
		{
			size_t length = 0;
			while (length < text.size() && (std::isalnum(static_cast<unsigned char>(text[length])) || text[length] == '_'))
				length++;
			return text.substr(0, length);
		}

		// ForgeScript comments start with '#'. Text inside strings is not a comment.
		std::string_view StripForgeComment(std::string_view line)
		{
			bool inString = false;
			for (size_t i = 0; i < line.size(); i++)
			{
				if (line[i] == '"' && (i == 0 || line[i - 1] != '\\'))
					inString = !inString;
				else if (line[i] == '#' && !inString)
					return line.substr(0, i);
			}
			return line;
		}

		void ParseForgeScriptLine(std::string_view rawLine, size_t lineNumber, std::vector<ScriptOutlineItem>& outItems)
		{
			const std::string_view line = Trim(StripForgeComment(rawLine));
			ScriptOutlineItem item;
			item.Line = lineNumber;
			item.Depth = IndentationDepth(rawLine);

			if (StartsWithWord(line, "function"))
			{
				item.ItemKind = ScriptOutlineItem::Kind::Function;
				item.Name = LeadingIdentifier(Trim(line.substr(8)));
			}
			else if (StartsWithWord(line, "kind"))
			{
				item.ItemKind = ScriptOutlineItem::Kind::Type;
				item.Name = LeadingIdentifier(Trim(line.substr(4)));
			}
			else if (StartsWithWord(line, "when"))
			{
				// The event name is every word up to "with" or "by".
				item.ItemKind = ScriptOutlineItem::Kind::Event;
				std::string_view rest = Trim(line.substr(4));
				std::string name;
				while (!rest.empty())
				{
					const size_t space = rest.find_first_of(" \t");
					const std::string_view word = rest.substr(0, space);
					if (word == "with" || word == "by")
						break;
					if (!name.empty())
						name += ' ';
					name += word;
					rest = space == std::string_view::npos ? std::string_view() : Trim(rest.substr(space));
				}
				item.Name = name;
			}
			else if (StartsWithWord(line, "phrase"))
			{
				item.ItemKind = ScriptOutlineItem::Kind::Phrase;
				item.Name = Trim(line.substr(6));
			}

			if (!item.Name.empty())
				outItems.push_back(std::move(item));
		}

		void ParsePythonLine(std::string_view rawLine, size_t lineNumber, std::vector<ScriptOutlineItem>& outItems)
		{
			std::string_view line = Trim(rawLine);
			ScriptOutlineItem item;
			item.Line = lineNumber;
			item.Depth = IndentationDepth(rawLine);

			if (StartsWithWord(line, "async"))
				line = Trim(line.substr(5));
			if (StartsWithWord(line, "def"))
			{
				item.ItemKind = ScriptOutlineItem::Kind::Function;
				item.Name = LeadingIdentifier(Trim(line.substr(3)));
			}
			else if (StartsWithWord(line, "class"))
			{
				item.ItemKind = ScriptOutlineItem::Kind::Type;
				item.Name = LeadingIdentifier(Trim(line.substr(5)));
			}

			if (!item.Name.empty())
				outItems.push_back(std::move(item));
		}

		std::vector<ScriptDiagnostic> CheckForgeScript(std::string_view source, const std::string& fileName)
		{
			if (!s_CheckVM)
			{
				s_CheckVM = std::make_unique<ForgeScript::VM>();
				if (s_ForgeScriptCheckSetup)
					s_ForgeScriptCheckSetup(*s_CheckVM);
			}

			// Scripts pulled in with 'use' may have been edited since the last check.
			s_CheckVM->ClearModuleCache();
			std::vector<ScriptDiagnostic> diagnostics;
			for (const ForgeScript::CompileError& error : s_CheckVM->FindMistakes(source, fileName))
			{
				ScriptDiagnostic diagnostic;
				diagnostic.Message = error.Message;
				if (error.File.empty() || error.File == fileName)
				{
					diagnostic.Line = error.Line > 0 ? static_cast<size_t>(error.Line) : 0;
					diagnostic.Column = error.Column > 0 ? static_cast<size_t>(error.Column) : 0;
				}
				else
				{
					// A mistake inside a script pulled in with 'use': its line numbers belong to that file.
					diagnostic.Message = error.File + " line " + std::to_string(error.Line) + ": " + error.Message;
				}
				diagnostics.push_back(std::move(diagnostic));
			}
			return diagnostics;
		}

		std::vector<ScriptDiagnostic> CheckPython(std::string_view source, const std::string& fileName)
		{
			if (!ScriptEngine::IsInitialized())
				return {};

			try
			{
				py::module_::import("builtins").attr("compile")(py::str(source.data(), source.size()), fileName, "exec", 0, true);
				return {};
			}
			catch (py::error_already_set& exception)
			{
				ScriptDiagnostic diagnostic;
				if (exception.matches(PyExc_SyntaxError))
				{
					const py::object value = exception.value();
					diagnostic.Message = py::str(value.attr("msg")).cast<std::string>();
					if (!value.attr("lineno").is_none())
						diagnostic.Line = value.attr("lineno").cast<size_t>();
					if (!value.attr("offset").is_none())
						diagnostic.Column = value.attr("offset").cast<size_t>();
				}
				else
				{
					// For example a null character in the text.
					diagnostic.Message = py::str(exception.value()).cast<std::string>();
				}
				return { diagnostic };
			}
		}

	}

	ScriptLanguage GetScriptLanguage(const std::filesystem::path& path)
	{
		const std::string extension = ToLower(path.extension().string());
		if (extension == ".forge")
			return ScriptLanguage::ForgeScript;
		if (extension == ".py")
			return ScriptLanguage::Python;
		return ScriptLanguage::None;
	}

	const char* GetScriptLanguageName(ScriptLanguage language)
	{
		switch (language)
		{
			case ScriptLanguage::ForgeScript:	return "ForgeScript";
			case ScriptLanguage::Python:		return "Python";
			case ScriptLanguage::None:			break;
		}
		return "Text";
	}

	const char* GetScriptExtension(ScriptLanguage language)
	{
		switch (language)
		{
			case ScriptLanguage::ForgeScript:	return ".forge";
			case ScriptLanguage::Python:		return ".py";
			case ScriptLanguage::None:			break;
		}
		return "";
	}

	std::string MakeScriptClassName(std::string_view name)
	{
		std::string result;
		bool upper = true;
		for (char c : name)
		{
			if (std::isalnum(static_cast<unsigned char>(c)))
			{
				result += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				upper = false;
			}
			else
			{
				upper = true;
			}
		}
		if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())))
			result = "Script" + result;
		return result;
	}

	std::string MakeScriptSource(ScriptLanguage language, ScriptTemplate scriptTemplate, std::string_view name)
	{
		const bool empty = scriptTemplate == ScriptTemplate::Empty;
		std::string source;
		switch (language)
		{
			case ScriptLanguage::ForgeScript:	source = empty ? c_ForgeScriptEmpty : c_ForgeScriptDefault; break;
			case ScriptLanguage::Python:		source = empty ? c_PythonEmpty : c_PythonDefault; break;
			case ScriptLanguage::None:			return {};
		}

		// ForgeScript reads {braces} inside text as interpolation, so names are inserted without them.
		std::string displayName;
		for (char c : name)
		{
			if (c != '{' && c != '}' && c != '"' && c != '\n' && c != '\r')
				displayName += c;
		}
		if (displayName.empty())
			displayName = "Script";

		ReplaceAll(source, "{Class}", MakeScriptClassName(name));
		ReplaceAll(source, "{Name}", displayName);
		return source;
	}

	std::vector<ScriptOutlineItem> ParseScriptOutline(ScriptLanguage language, std::string_view source)
	{
		std::vector<ScriptOutlineItem> items;
		if (language == ScriptLanguage::None)
			return items;

		size_t lineNumber = 1;
		bool inBlockComment = false;	// ForgeScript #[ ... ]#
		size_t start = 0;
		while (start <= source.size())
		{
			const size_t end = source.find('\n', start);
			std::string_view line = source.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);

			if (language == ScriptLanguage::ForgeScript)
			{
				if (inBlockComment)
				{
					const size_t close = line.find("]#");
					inBlockComment = close == std::string_view::npos;
				}
				else if (Trim(line).substr(0, 2) == "#[")
				{
					inBlockComment = line.find("]#", line.find("#[") + 2) == std::string_view::npos;
				}
				else
				{
					ParseForgeScriptLine(line, lineNumber, items);
				}
			}
			else
			{
				ParsePythonLine(line, lineNumber, items);
			}

			if (end == std::string_view::npos)
				break;
			start = end + 1;
			lineNumber++;
		}
		return items;
	}

	std::vector<ScriptOutlineItem> ParseMarkdownOutline(std::string_view source)
	{
		std::vector<ScriptOutlineItem> items;
		size_t lineNumber = 1;
		bool inCodeBlock = false;	// '#' inside ``` fences is code, not a heading
		size_t start = 0;
		while (start <= source.size())
		{
			const size_t end = source.find('\n', start);
			std::string_view line = source.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);

			if (line.substr(0, 3) == "```")
			{
				inCodeBlock = !inCodeBlock;
			}
			else if (!inCodeBlock && !line.empty() && line.front() == '#')
			{
				size_t level = 0;
				while (level < line.size() && line[level] == '#')
					level++;
				const std::string_view title = Trim(line.substr(level));
				if (level <= 6 && level < line.size() && line[level] == ' ' && !title.empty())
				{
					ScriptOutlineItem item;
					item.ItemKind = ScriptOutlineItem::Kind::Heading;
					item.Name = title;
					item.Line = lineNumber;
					item.Depth = static_cast<int>(level) - 1;
					items.push_back(std::move(item));
				}
			}

			if (end == std::string_view::npos)
				break;
			start = end + 1;
			lineNumber++;
		}
		return items;
	}

	std::string FormatScriptDiagnostic(const std::string& fileName, const ScriptDiagnostic& diagnostic)
	{
		return fileName + ":" + std::to_string(diagnostic.Line) + ":" + std::to_string(diagnostic.Column) + ": " + diagnostic.Message;
	}

	std::string FormatScriptDiagnosticsJson(const std::string& fileName, std::string_view source, const std::vector<ScriptDiagnostic>& diagnostics)
	{
		// Line lengths, so each mistake can be marked to the end of its line.
		std::vector<size_t> lineLengths;
		size_t start = 0;
		while (start <= source.size())
		{
			size_t end = source.find('\n', start);
			const size_t lineEnd = end == std::string_view::npos ? source.size() : end;
			size_t length = lineEnd - start;
			if (length > 0 && source[lineEnd - 1] == '\r')
				length--;
			lineLengths.push_back(length);
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}

		Json items = Json::array();
		for (const ScriptDiagnostic& diagnostic : diagnostics)
		{
			const size_t lineLength = diagnostic.Line > 0 && diagnostic.Line <= lineLengths.size() ? lineLengths[diagnostic.Line - 1] : 0;
			items.push_back({
				{ "line", diagnostic.Line },
				{ "column", diagnostic.Column },
				{ "endColumn", std::max(lineLength + 1, diagnostic.Column + 1) },
				{ "severity", "error" },
				{ "message", diagnostic.Message },
			});
		}
		Json json;
		json["file"] = fileName;
		json["diagnostics"] = items;
		return json.dump();
	}

	std::vector<ScriptDiagnostic> CheckScript(ScriptLanguage language, std::string_view source, const std::string& fileName)
	{
		switch (language)
		{
			case ScriptLanguage::ForgeScript:	return CheckForgeScript(source, fileName);
			case ScriptLanguage::Python:		return CheckPython(source, fileName);
			case ScriptLanguage::None:			break;
		}
		return {};
	}

	void SetUpEngineForgeScriptCheck(ForgeScript::VM& vm)
	{
		Scripting::RegisterForgeScriptApi(vm);
		Scripting::ForgeScriptRuntime::InstallModuleLoader(vm);
	}

	void SetForgeScriptCheckSetup(ForgeScriptCheckSetup setup)
	{
		s_ForgeScriptCheckSetup = setup;
		s_CheckVM.reset();	// The next check builds a machine with the new setup.
	}

}
