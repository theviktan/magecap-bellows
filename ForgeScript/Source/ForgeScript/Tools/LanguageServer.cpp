#include "ForgeScript/Tools/LanguageServer.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Compiler/ScriptVariables.h"
#include "ForgeScript/ForgeScript.h"
#include "ForgeScript/Tools/Help.h"
#include "ForgeScript/Tools/ProjectPaths.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <istream>
#include <ostream>
#include <set>

namespace ForgeScript {

	namespace {

		// The positions of the names in "set x, y to ...", from the first name; empty when it isn't that.
		// 'shared count is 0' at the start of a line (inside a kind, the compiler's shared value).
		bool IsSharedLine(const std::vector<Token>& tokens, size_t i)
		{
			return i + 2 < tokens.size() && (i == 0 || tokens[i - 1].Type == TokenType::Newline) &&
				tokens[i].Type == TokenType::Identifier && tokens[i].Lexeme == "shared" && tokens[i + 1].Type == TokenType::Identifier &&
				(tokens[i + 2].Type == TokenType::Is || tokens[i + 2].Type == TokenType::To);
		}

		// A parameter written 'numbers...', which gets the values left over.
		bool CollectsTheRest(const std::vector<Token>& tokens, size_t i)
		{
			return i + 3 < tokens.size() && tokens[i + 1].Type == TokenType::Dot && tokens[i + 2].Type == TokenType::Dot &&
				tokens[i + 3].Type == TokenType::Dot;
		}

		// 'choices State are idle, running' at the start of a line (the compiler's IsChoicesAt).
		bool IsChoicesLine(const std::vector<Token>& tokens, size_t i)
		{
			return i + 2 < tokens.size() && (i == 0 || tokens[i - 1].Type == TokenType::Newline) &&
				tokens[i].Type == TokenType::Identifier && tokens[i].Lexeme == "choices" &&
				tokens[i + 1].Type == TokenType::Identifier && tokens[i + 2].Type == TokenType::Identifier && tokens[i + 2].Lexeme == "are";
		}

		// The words of a line from tokens[i] to its end, joined by spaces.
		std::string RestOfLine(const std::vector<Token>& tokens, size_t i)
		{
			std::string text;
			for (size_t j = i; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				text += (text.empty() || tokens[j].Type == TokenType::Comma ? "" : " ") + std::string(tokens[j].Lexeme);
			return text;
		}

		std::vector<size_t> UnpackedNames(const std::vector<Token>& tokens, size_t first)
		{
			std::vector<size_t> names;
			size_t i = first;
			while (i < tokens.size() && tokens[i].Type == TokenType::Identifier)
			{
				names.push_back(i);
				if (i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Comma)
				{
					i += 2;
					continue;
				}
				if (names.size() >= 2 && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::To)
					return names;
				break;
			}
			return {};
		}

		// The words of a line from 'start', for a symbol's signature.
		std::string LineText(const std::vector<Token>& tokens, size_t start)
		{
			std::string text;
			for (size_t j = start; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof && text.size() < 60; j++)
				text += (text.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
			return text;
		}

		// JSON-RPC and LSP error codes.
		constexpr int c_ParseError = -32700;
		constexpr int c_InvalidRequest = -32600;
		constexpr int c_MethodNotFound = -32601;
		constexpr int c_ServerNotInitialized = -32002;
		constexpr int c_RequestFailed = -32803;

		// LSP enumerations.
		constexpr int c_SeverityError = 1;
		constexpr int c_SeverityWarning = 2;
		constexpr int c_CompletionMethod = 2;
		constexpr int c_CompletionFunction = 3;
		constexpr int c_CompletionField = 5;
		constexpr int c_CompletionVariable = 6;
		constexpr int c_CompletionClass = 7;
		constexpr int c_CompletionModule = 9;
		constexpr int c_CompletionProperty = 10;
		constexpr int c_CompletionKeyword = 14;
		constexpr int c_CompletionConstant = 21;
		// LSP SymbolKind values for the outline.
		constexpr int c_SymbolClass = 5;
		constexpr int c_SymbolMethod = 6;
		constexpr int c_SymbolField = 8;
		constexpr int c_SymbolFunction = 12;
		constexpr int c_SymbolVariable = 13;
		constexpr int c_SymbolConstant = 14;
		constexpr int c_SymbolEvent = 24;

		// A message larger than this is refused: it is almost certainly not an editor talking.
		constexpr size_t c_MaxMessageBytes = 64 * 1024 * 1024;

		Json Response(const Json& id, Json result)
		{
			Json message;
			message.Set("jsonrpc", "2.0").Set("id", id).Set("result", std::move(result));
			return message;
		}

		Json ErrorResponse(const Json& id, int code, const std::string& text)
		{
			Json error;
			error.Set("code", code).Set("message", text);
			Json message;
			message.Set("jsonrpc", "2.0").Set("id", id).Set("error", std::move(error));
			return message;
		}

		Json Notification(const std::string& method, Json params)
		{
			Json message;
			message.Set("jsonrpc", "2.0").Set("method", method).Set("params", std::move(params));
			return message;
		}

		// ---------------------------------------------------------------- Positions

		std::vector<std::string_view> SplitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			size_t start = 0;
			while (true)
			{
				const size_t end = text.find('\n', start);
				std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
				if (!line.empty() && line.back() == '\r')
					line.remove_suffix(1);
				lines.push_back(line);
				if (end == std::string_view::npos)
					break;
				start = end + 1;
			}
			return lines;
		}

		// How many bytes a UTF-8 sequence starting with this byte takes (1 for broken bytes).
		size_t Utf8Length(unsigned char lead)
		{
			if (lead >= 0xF0 && lead < 0xF8) return 4;
			if (lead >= 0xE0) return lead < 0xF0 ? 3 : 1;
			if (lead >= 0xC0) return 2;
			return 1;
		}

		// UTF-16 code units in the first 'bytes' bytes of a line.
		int ToUtf16(std::string_view line, size_t bytes)
		{
			int units = 0;
			size_t i = 0;
			bytes = std::min(bytes, line.size());
			while (i < bytes)
			{
				const size_t length = Utf8Length(static_cast<unsigned char>(line[i]));
				units += length == 4 ? 2 : 1;
				i += length;
			}
			return units;
		}

		// The byte offset in a line of a UTF-16 position (clamped to the line).
		size_t FromUtf16(std::string_view line, int units)
		{
			size_t i = 0;
			int counted = 0;
			while (i < line.size() && counted < units)
			{
				const size_t length = Utf8Length(static_cast<unsigned char>(line[i]));
				counted += length == 4 ? 2 : 1;
				i += length;
			}
			return std::min(i, line.size());
		}

		Json Position(int line, int character)
		{
			Json position;
			position.Set("line", line).Set("character", character);
			return position;
		}

		// A range from 1-based lexer line and byte column, 'length' bytes long, inside 'text'.
		Json RangeOf(std::string_view text, int line, int column, int length)
		{
			const std::vector<std::string_view> lines = SplitLines(text);
			const int index = std::clamp(line - 1, 0, static_cast<int>(lines.size()) - 1);
			const std::string_view lineText = lines[static_cast<size_t>(index)];
			const size_t start = static_cast<size_t>(std::max(column - 1, 0));
			Json range;
			range.Set("start", Position(index, ToUtf16(lineText, start)));
			range.Set("end", Position(index, ToUtf16(lineText, start + static_cast<size_t>(std::max(length, 0)))));
			return range;
		}

		bool IsWordChar(char c)
		{
			return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
		}

		// The word around a byte offset in a line, and where it starts.
		std::string WordAt(std::string_view line, size_t offset, size_t& outStart, size_t& outEnd)
		{
			size_t start = std::min(offset, line.size());
			while (start > 0 && IsWordChar(line[start - 1]))
				start--;
			size_t end = std::min(offset, line.size());
			while (end < line.size() && IsWordChar(line[end]))
				end++;
			outStart = start;
			outEnd = end;
			return std::string(line.substr(start, end - start));
		}

		// What comes just before a word: the word before a '.', or 'my'.
		std::string ReceiverBefore(std::string_view line, size_t wordStart, bool& outIsMember)
		{
			outIsMember = false;
			size_t i = wordStart;
			if (i > 0 && line[i - 1] == '.')
			{
				outIsMember = true;
				size_t start = i - 1;
				while (start > 0 && IsWordChar(line[start - 1]))
					start--;
				return std::string(line.substr(start, i - 1 - start));
			}
			while (i > 0 && line[i - 1] == ' ')
				i--;
			if (i >= 2 && line.substr(i - 2, 2) == "my" && (i == 2 || !IsWordChar(line[i - 3])))
			{
				outIsMember = true;
				return "my";
			}
			return "";
		}

		// ---------------------------------------------------------------- Help text

		std::string CodeBlock(const std::string& code)
		{
			return "```forge\n" + code + "\n```";
		}

		std::string HelpMarkdown(const std::string& signature, const std::string& doc)
		{
			return doc.empty() ? CodeBlock(signature) : CodeBlock(signature) + "\n\n" + doc;
		}

		std::string PhraseFirstWord(const std::string& pattern)
		{
			const size_t space = pattern.find(' ');
			return pattern.substr(0, space);
		}

		// True when 'word' is one of a phrase's own words and the line also has the phrase's first word, so
		// hovering "screenshot" in "save screenshot to x" explains the phrase.
		bool LineUsesPhrase(const std::string& pattern, const std::string& word, std::string_view line)
		{
			std::vector<std::string> words;
			size_t start = 0;
			while (start < pattern.size())
			{
				size_t end = pattern.find(' ', start);
				if (end == std::string::npos)
					end = pattern.size();
				const std::string part = pattern.substr(start, end - start);
				if (!part.empty() && part.front() != '(')
					words.push_back(part);
				start = end + 1;
			}
			if (words.empty() || std::find(words.begin(), words.end(), word) == words.end())
				return false;
			for (size_t at = line.find(words.front()); at != std::string_view::npos; at = line.find(words.front(), at + 1))
			{
				const size_t after = at + words.front().size();
				if ((at == 0 || !IsWordChar(line[at - 1])) && (after >= line.size() || !IsWordChar(line[after])))
					return true;
			}
			return false;
		}

		// What to type for a phrase: its words up to the first value, "random item from ".
		std::string PhraseInsertText(const std::string& pattern)
		{
			const size_t slot = pattern.find('(');
			return slot == std::string::npos ? pattern : pattern.substr(0, slot);
		}

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
				text.remove_prefix(1);
			while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
				text.remove_suffix(1);
			return text;
		}

		// The alternatives of a type text, split at the '|'s outside brackets: "Vec4 | list[float]".
		std::vector<std::string_view> TypeAlternatives(std::string_view type)
		{
			std::vector<std::string_view> parts;
			int depth = 0;
			size_t start = 0;
			for (size_t i = 0; i <= type.size(); i++)
			{
				const char c = i < type.size() ? type[i] : '|';
				if (c == '[')
					depth++;
				else if (c == ']')
					depth--;
				else if (c == '|' && depth == 0)
				{
					const std::string_view part = Trim(type.substr(start, i - start));
					if (!part.empty())
						parts.push_back(part);
					start = i + 1;
				}
			}
			return parts;
		}

		// forgescript-api.json names types the way Python does (it shares _bellows.pyi's names); people
		// reading ForgeScript help see ForgeScript's names, as the engine's binder converts the values.
		std::string ForgeTypeName(std::string_view type)
		{
			std::vector<std::string> names;
			for (std::string_view part : TypeAlternatives(type))
			{
				std::string name;
				if (part == "str" || part == "Key" || part == "MouseButton")
					name = "text";
				else if (part == "float" || part == "int")
					name = "number";
				else if (part == "bool")
					name = "yes/no";
				else if (part == "None")
					name = "nothing";
				else if (part == "Vec2" || part == "Vec3")
					name = "vector";
				else if (part == "Vec4")
					name = "color";
				else if (part == "Quat" || part.starts_with("tuple["))
					name = "list";
				else if (part.starts_with("list[") && part.ends_with("]"))
					name = "list of " + ForgeTypeName(part.substr(5, part.size() - 6));
				else
					name = std::string(part);
				if (std::find(names.begin(), names.end(), name) == names.end())
					names.push_back(name);
			}
			std::string text;
			for (const std::string& name : names)
				text += (text.empty() ? "" : " or ") + name;
			return text;
		}

		std::string ParamsSignature(const Json& params)
		{
			std::string text;
			for (const Json& param : params.AsArray())
			{
				if (!text.empty())
					text += ", ";
				text += param.GetString("name");
				if (const Json* type = param.Find("type"); type && type->IsString())
					text += ": " + ForgeTypeName(type->AsString());
				if (const Json* value = param.Find("default"))
				{
					if (value->IsString())
						text += " = \"" + value->AsString() + "\"";
					else if (value->IsNull())
						text += " = nothing";
					else
						text += " = " + value->Dump();
				}
			}
			return text;
		}

		// ---------------------------------------------------------------- URIs

		std::string PercentDecode(std::string_view text)
		{
			std::string out;
			for (size_t i = 0; i < text.size(); i++)
			{
				if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
					std::isxdigit(static_cast<unsigned char>(text[i + 2])))
				{
					out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
					i += 2;
				}
				else
				{
					out += text[i];
				}
			}
			return out;
		}

	}

	// =================================================================== Symbols

	std::vector<LanguageServer::Symbol> FindScriptSymbols(const std::string& source)
	{
		using Kind = LanguageServer::Symbol::Kind;
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();
		const std::vector<std::string_view> lines = SplitLines(source);

		// The '#' comment lines right above a line, as documentation.
		auto docAbove = [&lines](int line)
		{
			std::vector<std::string> comments;
			for (int i = line - 2; i >= 0; i--)
			{
				std::string_view text = lines[static_cast<size_t>(i)];
				const size_t first = text.find_first_not_of(" \t");
				if (first == std::string_view::npos || text[first] != '#')
					break;
				text.remove_prefix(first + 1);
				if (!text.empty() && text.front() == ' ')
					text.remove_prefix(1);
				comments.emplace_back(text);
			}
			std::string doc;
			for (auto it = comments.rbegin(); it != comments.rend(); ++it)
				doc += (doc.empty() ? "" : "\n") + *it;
			return doc;
		};

		std::vector<LanguageServer::Symbol> symbols;
		std::set<std::string> variables;
		auto add = [&symbols](Kind kind, const Token& token, std::string name, std::string signature, std::string doc, std::string owner)
		{
			LanguageServer::Symbol symbol;
			symbol.SymbolKind = kind;
			symbol.Name = std::move(name);
			symbol.Signature = std::move(signature);
			symbol.Doc = std::move(doc);
			symbol.Owner = std::move(owner);
			symbol.Line = token.Line;
			symbol.Column = token.Column;
			symbol.Length = static_cast<int>(token.Lexeme.size());
			symbols.push_back(std::move(symbol));
		};

		// Kinds currently open, with the block depth they started at, so functions get their owner.
		std::vector<std::pair<std::string, int>> kinds;
		int depth = 0;
		for (size_t i = 0; i < tokens.size() && tokens[i].Type != TokenType::Eof; i++)
		{
			const Token& token = tokens[i];
			const bool opens = TokenOpensBlock(tokens, i);
			const std::string owner = kinds.empty() ? std::string() : kinds.back().first;

			if (token.Type == TokenType::Function && opens && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier)
			{
				const Token& name = tokens[i + 1];
				std::string params;
				size_t j = i + 2;
				if (j < tokens.size() && tokens[j].Type == TokenType::LeftParen)
				{
					// A parameter is the name after '(' or ','; a default value ("damage is 10") runs to the
					// next ',' or ')' outside brackets and shows in the label as written.
					int brackets = 0;
					bool expectName = true;
					size_t defaultStart = 0;
					auto endDefault = [&](size_t end)
					{
						if (defaultStart && end > defaultStart)
						{
							const char* first = tokens[defaultStart].Lexeme.data();
							const char* last = tokens[end - 1].Lexeme.data() + tokens[end - 1].Lexeme.size();
							params += " is " + std::string(first, static_cast<size_t>(last - first));
						}
						defaultStart = 0;
					};
					for (j++; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
					{
						const TokenType type = tokens[j].Type;
						const bool afterBrace = !tokens[j].Lexeme.empty() && tokens[j].Lexeme.front() == '}';
						if (brackets == 0 && (type == TokenType::RightParen || type == TokenType::Comma))
						{
							endDefault(j);
							if (type == TokenType::RightParen)
								break;
							expectName = true;
							continue;
						}
						if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace ||
							(type == TokenType::Interpolation && !afterBrace))
							brackets++;
						else if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace ||
							(type == TokenType::String && afterBrace))
							brackets--;
						if (!expectName && brackets == 0 && !defaultStart && type == TokenType::Identifier && tokens[j].Lexeme == "as" &&
							j + 1 < tokens.size())
						{
							params += " as " + std::string(tokens[j + 1].Lexeme);
							continue;
						}
						if (expectName && type == TokenType::Identifier)
						{
							params += (params.empty() ? "" : ", ") + std::string(tokens[j].Lexeme) + (CollectsTheRest(tokens, j) ? "..." : "");
							add(Kind::Parameter, tokens[j], std::string(tokens[j].Lexeme), std::string(tokens[j].Lexeme), "", std::string(name.Lexeme));
							expectName = false;
						}
						else if (brackets == 0 && type == TokenType::Is && !defaultStart)
						{
							defaultStart = j + 1;
						}
					}
				}
				const std::string label = std::string(name.Lexeme);
				add(Kind::Function, name, label, "function " + (owner.empty() ? "" : owner + ".") + label + "(" + params + ")", docAbove(token.Line), owner);
			}
			else if (token.Type == TokenType::Phrase && opens)
			{
				std::string pattern;
				size_t j = i + 1;
				for (; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				{
					if (tokens[j].Type == TokenType::LeftParen && j + 2 < tokens.size() && tokens[j + 2].Type == TokenType::RightParen)
					{
						pattern += (pattern.empty() ? "(" : " (") + std::string(tokens[j + 1].Lexeme) + ")";
						add(Kind::Parameter, tokens[j + 1], std::string(tokens[j + 1].Lexeme), std::string(tokens[j + 1].Lexeme), "", "");
						j += 2;
					}
					else if (tokens[j].IsWord())
					{
						pattern += (pattern.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
					}
				}
				if (!pattern.empty() && i + 1 < tokens.size())
					add(Kind::Phrase, tokens[i + 1], pattern, "phrase " + pattern, docAbove(token.Line), "");
			}
			else if (token.Type == TokenType::Kind && opens && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier)
			{
				const Token& name = tokens[i + 1];
				std::string signature = "kind " + std::string(name.Lexeme);
				if (i + 3 < tokens.size() && tokens[i + 2].Type == TokenType::Extends)
					signature += " extends " + std::string(tokens[i + 3].Lexeme);
				add(Kind::KindType, name, std::string(name.Lexeme), signature, docAbove(token.Line), "");
				kinds.emplace_back(std::string(name.Lexeme), depth);
			}
			else if (token.Type == TokenType::Constant && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier)
			{
				const Token& name = tokens[i + 1];
				std::string value;
				for (size_t j = i + 3; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
					value += (value.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
				add(Kind::Constant, name, std::string(name.Lexeme), "constant " + std::string(name.Lexeme) + " is " + value, docAbove(token.Line), "");
			}
			else if (IsChoicesLine(tokens, i))
			{
				const Token& name = tokens[i + 1];
				add(Kind::Constant, name, std::string(name.Lexeme), RestOfLine(tokens, i), docAbove(token.Line), "");
			}
			else if (!owner.empty() && IsSharedLine(tokens, i))
			{
				const Token& name = tokens[i + 1];
				if (variables.insert(owner + "." + std::string(name.Lexeme)).second)
					add(Kind::Variable, name, std::string(name.Lexeme), RestOfLine(tokens, i), docAbove(token.Line), owner);
			}
			else if (token.Type == TokenType::Set && i + 2 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier && tokens[i + 2].Type == TokenType::To)
			{
				// The first 'set' of a name defines it; 'set my x' inside a kind defines a field.
				const Token& name = tokens[i + 1];
				const std::string key = owner + "." + std::string(name.Lexeme);
				if (variables.insert(key).second)
				{
					std::string value;
					for (size_t j = i + 3; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof && value.size() < 60; j++)
						value += (value.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
					add(Kind::Variable, name, std::string(name.Lexeme), "set " + std::string(name.Lexeme) + " to " + value, docAbove(token.Line), owner);
				}
			}
			else if (token.Type == TokenType::Set && !UnpackedNames(tokens, i + 1).empty())
			{
				// set x, y to ...: each name is defined by its first 'set'.
				for (size_t nameIndex : UnpackedNames(tokens, i + 1))
				{
					const Token& name = tokens[nameIndex];
					if (variables.insert(owner + "." + std::string(name.Lexeme)).second)
						add(Kind::Variable, name, std::string(name.Lexeme), "set " + std::string(name.Lexeme) + " from " + LineText(tokens, i), docAbove(token.Line), owner);
				}
			}
			else if (token.Type == TokenType::Set && i + 3 < tokens.size() && tokens[i + 1].Type == TokenType::My && tokens[i + 2].Type == TokenType::Identifier && !owner.empty())
			{
				const Token& name = tokens[i + 2];
				if (variables.insert(owner + "." + std::string(name.Lexeme)).second)
					add(Kind::Variable, name, std::string(name.Lexeme), "set my " + std::string(name.Lexeme), docAbove(token.Line), owner);
			}
			else if ((token.Type == TokenType::Each || token.Type == TokenType::Catch) && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier)
			{
				add(Kind::Parameter, tokens[i + 1], std::string(tokens[i + 1].Lexeme), std::string(tokens[i + 1].Lexeme), "", "");
			}
			else if (token.Type == TokenType::For && i + 2 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier && tokens[i + 2].Type == TokenType::From)
			{
				add(Kind::Parameter, tokens[i + 1], std::string(tokens[i + 1].Lexeme), std::string(tokens[i + 1].Lexeme), "", "");
			}
			else if (token.Type == TokenType::When && opens)
			{
				// Names after 'with' or 'by' receive the event's values.
				bool values = false;
				for (size_t j = i + 1; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				{
					if (tokens[j].Type == TokenType::With || tokens[j].Type == TokenType::By)
						values = true;
					else if (values && tokens[j].Type == TokenType::Identifier)
						add(Kind::Parameter, tokens[j], std::string(tokens[j].Lexeme), std::string(tokens[j].Lexeme), "", "");
				}
			}
			else if (token.Type == TokenType::Use && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::String)
			{
				const Token& path = tokens[i + 1];
				std::string alias;
				if (i + 3 < tokens.size() && tokens[i + 2].Type == TokenType::Identifier && tokens[i + 2].Lexeme == "as" && tokens[i + 3].Type == TokenType::Identifier)
					alias = std::string(tokens[i + 3].Lexeme);
				LanguageServer::Symbol symbol;
				symbol.SymbolKind = Kind::Module;
				symbol.Name = alias.empty() ? path.Text : alias;
				symbol.Signature = "use \"" + path.Text + "\"" + (alias.empty() ? "" : " as " + alias);
				symbol.Owner = alias;
				symbol.File = path.Text;
				symbol.Line = path.Line;
				symbol.Column = path.Column;
				symbol.Length = static_cast<int>(path.Lexeme.size());
				symbols.push_back(std::move(symbol));
			}

			if (opens)
				depth++;
			else if (token.Type == TokenType::End && depth > 0)
			{
				depth--;
				if (!kinds.empty() && kinds.back().second == depth)
					kinds.pop_back();
			}
		}
		return symbols;
	}

	// =================================================================== Framing and URIs

	bool LanguageServer::ReadMessage(std::istream& in, std::string& outBody)
	{
		size_t length = 0;
		bool haveLength = false;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty())
			{
				if (!haveLength)
					continue;
				break;
			}
			const size_t colon = line.find(':');
			if (colon == std::string::npos)
				return false;
			std::string name = line.substr(0, colon);
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (name == "content-length")
			{
				try
				{
					length = std::stoull(line.substr(colon + 1));
				}
				catch (const std::exception&)
				{
					return false;
				}
				haveLength = true;
			}
		}
		if (!haveLength || length > c_MaxMessageBytes)
			return false;
		outBody.assign(length, '\0');
		in.read(outBody.data(), static_cast<std::streamsize>(length));
		return static_cast<size_t>(in.gcount()) == length;
	}

	void LanguageServer::WriteMessage(std::ostream& out, const std::string& body)
	{
		out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
		out.flush();
	}

	bool LanguageServer::UriToPath(const std::string& uri, std::filesystem::path& outPath)
	{
		const std::string prefix = "file://";
		if (uri.rfind(prefix, 0) != 0)
			return false;
		std::string rest = uri.substr(prefix.size());
		// file://host/share/x is a network path; file:///x has an empty host.
		std::string path;
		if (!rest.empty() && rest.front() != '/')
		{
			path = "//" + PercentDecode(rest);
		}
		else
		{
			path = PercentDecode(rest);
			// file:///C:/Games → C:/Games
			if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
				path.erase(0, 1);
		}
		outPath = PathFromUtf8(path).lexically_normal();
		return true;
	}

	std::string LanguageServer::PathToUri(const std::filesystem::path& path)
	{
		std::string text = GenericPathToUtf8(path);
		if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':')
			text = "/" + text;
		std::string uri = "file://";
		const char* hex = "0123456789ABCDEF";
		for (size_t i = 0; i < text.size(); i++)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			const bool driveColon = c == ':' && i == 2 && text[0] == '/';
			if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || driveColon)
			{
				uri += static_cast<char>(c);
			}
			else
			{
				uri += '%';
				uri += hex[c >> 4];
				uri += hex[c & 0xF];
			}
		}
		return uri;
	}

	// =================================================================== Server

	LanguageServer::LanguageServer(Options options)
		: m_Options(std::move(options))
	{
		if (!m_Options.ApiFile.empty())
			LoadApi(m_Options.ApiFile);
	}

	void LanguageServer::LoadApi(const std::filesystem::path& file)
	{
		std::string text;
		Json api;
		std::string error;
		if (!ReadTextFile(file, text))
		{
			m_ApiError = "Could not read the engine API description '" + PathToUtf8(file) + "'";
			return;
		}
		if (!Json::Parse(text, api, error))
		{
			m_ApiError = "The engine API description '" + PathToUtf8(file) + "' is not valid JSON: " + error;
			return;
		}

		for (const Json& function : api.Get("functions").AsArray())
		{
			ApiFunction entry;
			entry.Name = function.GetString("name");
			entry.Returns = function.GetString("returns");
			entry.Doc = function.GetString("doc");
			entry.Signature = entry.Name + "(" + ParamsSignature(function.Get("params")) + ")" + (entry.Returns.empty() ? "" : " -> " + ForgeTypeName(entry.Returns));
			if (!entry.Name.empty())
				m_ApiFunctions.push_back(std::move(entry));
		}
		for (const Json& type : api.Get("types").AsArray())
		{
			ApiType entry;
			entry.Name = type.GetString("name");
			entry.Doc = type.GetString("doc");
			for (const Json& member : type.Get("members").AsArray())
			{
				ApiMember item;
				item.Name = member.GetString("name");
				item.Doc = member.GetString("doc");
				const std::string& kind = member.GetString("kind");
				item.IsProperty = kind == "property";
				item.Type = item.IsProperty ? member.GetString("type") : member.GetString("returns");
				if (item.IsProperty)
				{
					item.Signature = entry.Name + "." + item.Name + (item.Type.empty() ? "" : ": " + ForgeTypeName(item.Type)) + (member.Get("readonly").AsBool() ? " (read-only)" : "");
				}
				else
				{
					item.Signature = entry.Name + "." + item.Name + "(" + ParamsSignature(member.Get("params")) + ")" + (item.Type.empty() ? "" : " -> " + ForgeTypeName(item.Type));
				}
				entry.Members.push_back(std::move(item));
			}
			if (!entry.Name.empty())
				m_ApiTypes.push_back(std::move(entry));
		}
		for (const Json& phrase : api.Get("phrases").AsArray())
		{
			ApiPhrase entry;
			entry.Pattern = phrase.GetString("pattern");
			entry.Returns = phrase.GetString("returns");
			entry.Doc = phrase.GetString("doc");
			entry.YieldsToCalls = phrase.Get("yields_to_calls").IsBool() && phrase.Get("yields_to_calls").AsBool();
			if (!entry.Pattern.empty())
				m_ApiPhrases.push_back(std::move(entry));
		}
	}

	int LanguageServer::Run(std::istream& in, std::ostream& out)
	{
		std::string body;
		while (!m_ExitRequested && ReadMessage(in, body))
		{
			Json message;
			std::string error;
			if (!Json::Parse(body, message, error))
			{
				WriteMessage(out, ErrorResponse(Json(), c_ParseError, "The message is not valid JSON: " + error).Dump());
				continue;
			}
			for (const Json& reply : Handle(message))
				WriteMessage(out, reply.Dump());
		}
		return ExitCode();
	}

	const LanguageServer::Document* LanguageServer::FindDocument(const std::string& uri) const
	{
		auto it = m_Documents.find(uri);
		return it == m_Documents.end() ? nullptr : &it->second;
	}

	std::vector<Json> LanguageServer::Handle(const Json& message)
	{
		const std::string& method = message.GetString("method");
		const Json* id = message.Find("id");
		const Json& params = message.Get("params");
		std::vector<Json> replies;

		// Requests have an id; notifications do not and never get a reply.
		if (id && method.empty())
			return replies;

		if (method == "exit")
		{
			m_ExitRequested = true;
			return replies;
		}
		if (!m_Initialized && method != "initialize")
		{
			if (id)
				replies.push_back(ErrorResponse(*id, c_ServerNotInitialized, "The server has not been initialized yet."));
			return replies;
		}
		if (m_ShutdownRequested)
		{
			if (id)
				replies.push_back(ErrorResponse(*id, c_InvalidRequest, "The server is shutting down."));
			return replies;
		}

		if (method == "initialize")
		{
			if (!id)
				return replies;
			replies.push_back(Response(*id, Initialize(params)));
			if (!m_ApiError.empty())
			{
				Json log;
				log.Set("type", 2).Set("message", m_ApiError + "; engine functions and phrases are unknown.");
				replies.push_back(Notification("window/logMessage", std::move(log)));
			}
			return replies;
		}
		if (method == "shutdown")
		{
			m_ShutdownRequested = true;
			if (id)
				replies.push_back(Response(*id, Json()));
			return replies;
		}
		if (method == "textDocument/didOpen" || method == "textDocument/didChange")
		{
			const Json& document = params.Get("textDocument");
			const std::string& uri = document.GetString("uri");
			if (uri.empty())
				return replies;
			Document& stored = m_Documents[uri];
			if (method == "textDocument/didOpen")
			{
				stored.Text = document.GetString("text");
			}
			else
			{
				// Full sync: the last change holds the whole text.
				const Json::Array& changes = params.Get("contentChanges").AsArray();
				if (!changes.empty())
					stored.Text = changes.back().GetString("text");
			}
			stored.Version = document.Get("version").AsNumber(stored.Version);
			if (!UriToPath(uri, stored.Path))
				stored.Path = PathFromUtf8(uri).filename();
			replies.push_back(PublishDiagnostics(uri));
			PublishDependents(stored.Path, uri, replies);
			return replies;
		}
		if (method == "textDocument/didClose")
		{
			const std::string& uri = params.Get("textDocument").GetString("uri");
			std::filesystem::path closed;
			if (auto it = m_Documents.find(uri); it != m_Documents.end())
				closed = it->second.Path;
			m_Documents.erase(uri);
			Json cleared;
			cleared.Set("uri", uri).Set("diagnostics", Json(Json::Array()));
			replies.push_back(Notification("textDocument/publishDiagnostics", std::move(cleared)));
			// Scripts that use it read the file on disk from now on, which may differ from the unsaved text.
			if (!closed.empty())
				PublishDependents(closed, uri, replies);
			return replies;
		}
		if (!id)
			return replies;

		if (method == "textDocument/completion")
			replies.push_back(Response(*id, Completion(params)));
		else if (method == "textDocument/hover")
			replies.push_back(Response(*id, Hover(params)));
		else if (method == "textDocument/definition")
			replies.push_back(Response(*id, Definition(params)));
		else if (method == "textDocument/documentSymbol")
			replies.push_back(Response(*id, DocumentSymbols(params)));
		else if (method == "textDocument/signatureHelp")
			replies.push_back(Response(*id, SignatureHelp(params)));
		else if (method == "textDocument/references")
			replies.push_back(Response(*id, References(params)));
		else if (method == "textDocument/prepareRename" || method == "textDocument/rename")
		{
			std::string error;
			Json result = method == "textDocument/rename" ? Rename(params, error) : PrepareRename(params, error);
			replies.push_back(error.empty() ? Response(*id, std::move(result)) : ErrorResponse(*id, c_RequestFailed, error));
		}
		else
			replies.push_back(ErrorResponse(*id, c_MethodNotFound, "'" + method + "' is not something this server does."));
		return replies;
	}

	Json LanguageServer::Initialize(const Json&)
	{
		m_Initialized = true;
		Json sync;
		sync.Set("openClose", true).Set("change", 1);
		Json completion;
		completion.Set("triggerCharacters", Json(Json::Array { Json(".") }));
		Json capabilities;
		capabilities.Set("positionEncoding", "utf-16");
		capabilities.Set("textDocumentSync", std::move(sync));
		capabilities.Set("completionProvider", std::move(completion));
		capabilities.Set("hoverProvider", true);
		capabilities.Set("definitionProvider", true);
		capabilities.Set("documentSymbolProvider", true);
		capabilities.Set("referencesProvider", true);
		Json rename;
		rename.Set("prepareProvider", true);
		capabilities.Set("renameProvider", std::move(rename));
		Json signatures;
		signatures.Set("triggerCharacters", Json(Json::Array { Json("("), Json(",") }));
		capabilities.Set("signatureHelpProvider", std::move(signatures));
		Json info;
		info.Set("name", "forgescript").Set("version", FORGESCRIPT_VERSION);
		Json result;
		result.Set("capabilities", std::move(capabilities)).Set("serverInfo", std::move(info));
		return result;
	}

	bool LanguageServer::ReadScriptForUse(const std::filesystem::path& path, std::string& outSource) const
	{
		// An open document's unsaved text is what the user sees, so it wins over the file on disk.
		const std::filesystem::path wanted = path.lexically_normal();
		for (const auto& [uri, document] : m_Documents)
		{
			if (document.Path == wanted)
			{
				outSource = document.Text;
				return true;
			}
		}
		return ReadTextFile(wanted, outSource);
	}

	void LanguageServer::SetUpChecker(VM& vm, const std::filesystem::path& path) const
	{
		// Engine phrases must be known, or every line that uses one would be a mistake. Their code never
		// runs here: the checks only compile.
		for (const ApiPhrase& phrase : m_ApiPhrases)
			vm.RegisterPhrase(phrase.Pattern, [](CallContext&) -> Value { throw RuntimeError("Engine phrases only run inside MageCap Bellows."); },
				nullptr, phrase.YieldsToCalls);
		const std::filesystem::path useFolder = FindUseFolder(path);
		vm.SetModuleLoader([this, useFolder](const std::string& name, std::string& outName, std::string& outSource)
		{
			const std::filesystem::path file = ResolveUsePath(useFolder, name);
			outName = PathToUtf8(file.filename());
			return ReadScriptForUse(file, outSource);
		});
	}

	std::vector<CompileError> LanguageServer::FindMistakesIn(const std::filesystem::path& path, const std::string& text) const
	{
		VM vm;
		SetUpChecker(vm, path);
		return vm.FindMistakes(text, PathToUtf8(path.filename()));
	}

	std::vector<CompileError> LanguageServer::FindWarningsIn(const std::filesystem::path& path, const std::string& text) const
	{
		VM vm;
		SetUpChecker(vm, path);
		std::vector<std::string> engineFunctions;
		for (const ApiFunction& function : m_ApiFunctions)
			engineFunctions.push_back(function.Name);
		return vm.FindWarnings(text, PathToUtf8(path.filename()), engineFunctions);
	}

	Json LanguageServer::PublishDiagnostics(const std::string& uri)
	{
		const Document& document = m_Documents[uri];
		const std::vector<std::string_view> lines = SplitLines(document.Text);
		Json diagnostics = Json(Json::Array());
		auto add = [&diagnostics](Json range, const std::string& message, int severity = c_SeverityError)
		{
			Json diagnostic;
			diagnostic.Set("range", std::move(range)).Set("severity", severity).Set("source", "forgescript").Set("message", message);
			diagnostics.Add(std::move(diagnostic));
		};

		for (const CompileError& mistake : FindMistakesIn(document.Path, document.Text))
		{
			// The checker reports where a mistake starts; mark from there to the end of the line, the same
			// span as the JSON check output's endColumn.
			Json range = RangeOf(document.Text, mistake.Line, mistake.Column, 0);
			const size_t index = static_cast<size_t>(std::clamp(mistake.Line - 1, 0, static_cast<int>(lines.size()) - 1));
			const int start = static_cast<int>(range.Get("start").Get("character").AsNumber());
			const int end = ToUtf16(lines[index], lines[index].size());
			range.Set("end", Position(static_cast<int>(index), std::max(end, start)));
			add(std::move(range), mistake.Message);
		}

		// Warnings mark the word they are about. A script with mistakes gets none: fix those first.
		if (diagnostics.AsArray().empty())
		{
			for (const CompileError& warning : FindWarningsIn(document.Path, document.Text))
			{
				const size_t index = static_cast<size_t>(std::clamp(warning.Line - 1, 0, static_cast<int>(lines.size()) - 1));
				const size_t at = std::min(lines[index].size(), static_cast<size_t>(std::max(warning.Column - 1, 0)));
				size_t length = 0;
				while (at + length < lines[index].size() && (std::isalnum(static_cast<unsigned char>(lines[index][at + length])) || lines[index][at + length] == '_'))
					length++;
				add(RangeOf(document.Text, warning.Line, warning.Column, static_cast<int>(std::max<size_t>(length, 1))), warning.Message, c_SeverityWarning);
			}
		}

		// A script this one uses that has mistakes of its own: shown on the 'use' line, because the script
		// stops there when it runs. One level deep; a script that cannot be found is already reported above.
		const std::filesystem::path useFolder = FindUseFolder(document.Path);
		for (const Symbol& symbol : FindScriptSymbols(document.Text))
		{
			if (symbol.SymbolKind != Symbol::Kind::Module)
				continue;
			const std::filesystem::path file = ResolveUsePath(useFolder, symbol.File).lexically_normal();
			std::string text;
			if (file == document.Path || !ReadScriptForUse(file, text))
				continue;
			const std::vector<CompileError> mistakes = FindMistakesIn(file, text);
			if (mistakes.empty())
				continue;
			const CompileError& first = mistakes.front();
			std::string message = PathToUtf8(file.filename()) + " line " + std::to_string(first.Line) + ": " + first.Message;
			if (mistakes.size() > 1)
				message += " (and " + std::to_string(mistakes.size() - 1) + (mistakes.size() == 2 ? " more mistake)" : " more mistakes)");
			add(RangeOf(document.Text, symbol.Line, symbol.Column, symbol.Length), message);
		}

		Json params;
		params.Set("uri", uri).Set("diagnostics", std::move(diagnostics));
		if (document.Version != 0)
			params.Set("version", document.Version);
		return Notification("textDocument/publishDiagnostics", std::move(params));
	}

	void LanguageServer::PublishDependents(const std::filesystem::path& changed, const std::string& skipUri, std::vector<Json>& replies)
	{
		const std::filesystem::path wanted = changed.lexically_normal();
		std::vector<std::string> dependents;
		for (const auto& [uri, document] : m_Documents)
		{
			if (uri == skipUri)
				continue;
			const std::filesystem::path useFolder = FindUseFolder(document.Path);
			for (const Symbol& symbol : FindScriptSymbols(document.Text))
			{
				if (symbol.SymbolKind == Symbol::Kind::Module && ResolveUsePath(useFolder, symbol.File).lexically_normal() == wanted)
				{
					dependents.push_back(uri);
					break;
				}
			}
		}
		for (const std::string& uri : dependents)
			replies.push_back(PublishDiagnostics(uri));
	}

	const LanguageServer::ApiType* LanguageServer::FindApiType(std::string_view name) const
	{
		for (const ApiType& type : m_ApiTypes)
		{
			if (type.Name == name)
				return &type;
		}
		return nullptr;
	}

	std::string LanguageServer::TypeFromApi(std::string_view type) const
	{
		for (std::string_view part : TypeAlternatives(type))
		{
			if (FindApiType(part))
				return std::string(part);
			if (part == "str" || part == "Key" || part == "MouseButton")
				return "text";
			if (part == "float" || part == "int")
				return "number";
			if (part == "Vec2" || part == "Vec3")
				return "vector";
			if (part.starts_with("list[") || part.starts_with("tuple[") || part == "Quat")
				return "list";
		}
		return "";
	}

	std::string LanguageServer::GuessType(const std::vector<std::string_view>& lines, const std::vector<Symbol>& symbols, const std::string& name, int lineIndex, int depth) const
	{
		if (name == "my" || name == "self")
			return FindApiType("Entity") ? "Entity" : "";
		if (name.empty() || depth > 4)
			return "";
		// The nearest assignment above wins; a script that assigns it in a branch may still be wrong,
		// which only means the list is narrower than it could be.
		const std::string prefix = "set " + name + " to ";
		for (int i = std::min(lineIndex, static_cast<int>(lines.size())) - 1; i >= 0; i--)
		{
			const std::string_view line = Trim(lines[static_cast<size_t>(i)]);
			if (line.starts_with(prefix))
				return ExpressionType(Trim(line.substr(prefix.size())), lines, symbols, i, depth + 1);
		}
		return "";
	}

	std::string LanguageServer::ExpressionType(std::string_view expression, const std::vector<std::string_view>& lines, const std::vector<Symbol>& symbols, int lineIndex, int depth) const
	{
		if (expression.empty())
			return "";
		const char first = expression.front();
		if (first == '"')
			return "text";
		if (first == '[')
			return "list";
		if (first == '{')
			return "map";
		if (std::isdigit(static_cast<unsigned char>(first)) || (first == '-' && expression.size() > 1 && std::isdigit(static_cast<unsigned char>(expression[1]))))
			return "number";

		// A name, perhaps followed by a call or a member: "find_entity(...)", "new Door()", "my transform", "e.transform".
		size_t end = 0;
		while (end < expression.size() && IsWordChar(expression[end]))
			end++;
		const std::string word(expression.substr(0, end));
		std::string_view rest = Trim(expression.substr(end));
		if (word.empty())
			return "";
		if (word == "new")
		{
			size_t kindEnd = 0;
			while (kindEnd < rest.size() && IsWordChar(rest[kindEnd]))
				kindEnd++;
			const std::string kind(rest.substr(0, kindEnd));
			for (const Symbol& symbol : symbols)
			{
				if (symbol.SymbolKind == Symbol::Kind::KindType && symbol.Name == kind)
					return "kind:" + kind;
			}
			return "";
		}
		if (word == "vector" && rest.starts_with("("))
			return "vector";

		std::string type;
		if (rest.starts_with("("))
		{
			for (const ApiFunction& function : m_ApiFunctions)
			{
				if (function.Name == word)
					return TypeFromApi(function.Returns);
			}
			return "";
		}
		if (word == "my")
		{
			type = "Entity";
		}
		else
		{
			type = GuessType(lines, symbols, word, lineIndex, depth);
			if (!rest.starts_with("."))
				return rest.empty() ? type : "";
			rest = Trim(rest.substr(1));
		}

		// One member of an engine type: "my transform", "e.transform".
		size_t memberEnd = 0;
		while (memberEnd < rest.size() && IsWordChar(rest[memberEnd]))
			memberEnd++;
		const std::string_view member = rest.substr(0, memberEnd);
		const std::string_view after = Trim(rest.substr(memberEnd));
		if (member.empty() || (!after.empty() && !after.starts_with("(")))
			return "";
		if (const ApiType* apiType = FindApiType(type))
		{
			for (const ApiMember& entry : apiType->Members)
			{
				if (entry.Name == member)
					return TypeFromApi(entry.Type);
			}
		}
		return "";
	}

	std::vector<LanguageServer::Symbol> LanguageServer::UsedSymbols(const std::filesystem::path& scriptPath, const std::string& source) const
	{
		// One level deep: the definitions of the scripts this one uses, tagged with their alias.
		std::vector<Symbol> used;
		const std::filesystem::path useFolder = FindUseFolder(scriptPath);
		for (const Symbol& module : FindScriptSymbols(source))
		{
			if (module.SymbolKind != Symbol::Kind::Module)
				continue;
			const std::filesystem::path file = ResolveUsePath(useFolder, module.File);
			std::string moduleSource;
			if (!ReadScriptForUse(file, moduleSource))
				continue;
			for (Symbol symbol : FindScriptSymbols(moduleSource))
			{
				const bool exported = symbol.SymbolKind == Symbol::Kind::Function || symbol.SymbolKind == Symbol::Kind::Phrase ||
					symbol.SymbolKind == Symbol::Kind::KindType || symbol.SymbolKind == Symbol::Kind::Constant ||
					(symbol.SymbolKind == Symbol::Kind::Variable && symbol.Owner.empty());
				if (!exported || !symbol.Owner.empty())
					continue;
				symbol.Owner = module.Owner;
				symbol.File = PathToUtf8(file.lexically_normal());
				used.push_back(std::move(symbol));
			}
		}
		return used;
	}

	Json LanguageServer::Completion(const Json& params)
	{
		const Document* document = FindDocument(params.Get("textDocument").GetString("uri"));
		Json items = Json(Json::Array());
		if (!document)
			return items;

		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const int lineIndex = static_cast<int>(params.Get("position").Get("line").AsNumber());
		if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size()))
			return items;
		const std::string_view line = lines[static_cast<size_t>(lineIndex)];
		const size_t offset = FromUtf16(line, static_cast<int>(params.Get("position").Get("character").AsNumber()));
		size_t wordStart = 0;
		size_t wordEnd = 0;
		WordAt(line, offset, wordStart, wordEnd);
		bool member = false;
		const std::string receiver = ReceiverBefore(line, wordStart, member);

		std::set<std::string> seen;
		auto item = [&items, &seen](const std::string& label, int kind, const std::string& detail, const std::string& doc, const std::string& insert = "")
		{
			if (!seen.insert(label + "\n" + detail).second)
				return;
			Json entry;
			entry.Set("label", label).Set("kind", kind);
			if (!detail.empty())
				entry.Set("detail", detail);
			if (!doc.empty())
			{
				Json markup;
				markup.Set("kind", "markdown").Set("value", doc);
				entry.Set("documentation", std::move(markup));
			}
			if (!insert.empty() && insert != label)
				entry.Set("insertText", insert);
			items.Add(std::move(entry));
		};

		const std::vector<Symbol> symbols = FindScriptSymbols(document->Text);
		const std::vector<Symbol> used = UsedSymbols(document->Path, document->Text);

		if (member)
		{
			// Members of a 'use ... as alias' script.
			bool alias = false;
			for (const Symbol& symbol : used)
			{
				if (!receiver.empty() && symbol.Owner == receiver)
				{
					alias = true;
					item(symbol.Name, symbol.SymbolKind == Symbol::Kind::Function ? c_CompletionFunction : c_CompletionVariable, symbol.Signature, symbol.Doc);
				}
			}
			if (alias)
				return items;

			// When the script shows what the value is, offer only what that kind of value has.
			const std::string known = GuessType(lines, symbols, receiver, lineIndex, 0);
			if (const ApiType* apiType = FindApiType(known))
			{
				for (const ApiMember& memberEntry : apiType->Members)
					item(memberEntry.Name, memberEntry.IsProperty ? c_CompletionProperty : c_CompletionMethod, memberEntry.Signature, memberEntry.Doc);
				return items;
			}
			if (known == "text" || known == "list" || known == "map" || known == "vector" || known == "number")
			{
				for (const HelpEntry& help : GetLanguageHelp())
				{
					if (help.EntryKind == HelpEntry::Kind::Method && help.Owner == known)
						item(help.Name, help.IsProperty ? c_CompletionProperty : c_CompletionMethod, help.Signature, help.Doc);
				}
				return items;
			}
			if (known.starts_with("kind:"))
			{
				// The kind's own fields and functions, then those of the kinds it extends.
				std::string kind = known.substr(5);
				for (int level = 0; level < 16 && !kind.empty(); level++)
				{
					std::string parent;
					for (const Symbol& symbol : symbols)
					{
						if (symbol.Owner == kind && (symbol.SymbolKind == Symbol::Kind::Function || symbol.SymbolKind == Symbol::Kind::Variable))
							item(symbol.Name, symbol.SymbolKind == Symbol::Kind::Function ? c_CompletionMethod : c_CompletionField, symbol.Signature, symbol.Doc);
						const std::string extends = " extends ";
						if (symbol.SymbolKind == Symbol::Kind::KindType && symbol.Name == kind && symbol.Signature.find(extends) != std::string::npos)
							parent = symbol.Signature.substr(symbol.Signature.find(extends) + extends.size());
					}
					kind = parent;
				}
				return items;
			}

			// Otherwise the type is not known before the script runs, so offer what any value could have:
			// the engine's members (Entity first for 'my' and 'self'), built-in methods and kinds' fields.
			const bool entity = receiver == "my" || receiver == "self";
			for (int pass = 0; pass < 2; pass++)
			{
				for (const ApiType& type : m_ApiTypes)
				{
					if ((type.Name == "Entity") != (pass == 0))
						continue;
					if (pass == 1 && entity)
						break;
					for (const ApiMember& memberEntry : type.Members)
						item(memberEntry.Name, memberEntry.IsProperty ? c_CompletionProperty : c_CompletionMethod, memberEntry.Signature, memberEntry.Doc);
				}
			}
			if (receiver == "my")
				return items;
			for (const HelpEntry& help : GetLanguageHelp())
			{
				if (help.EntryKind == HelpEntry::Kind::Method)
					item(help.Name, help.IsProperty ? c_CompletionProperty : c_CompletionMethod, help.Signature, help.Doc);
			}
			for (const Symbol& symbol : symbols)
			{
				if (!symbol.Owner.empty() && (symbol.SymbolKind == Symbol::Kind::Function || symbol.SymbolKind == Symbol::Kind::Variable))
					item(symbol.Name, symbol.SymbolKind == Symbol::Kind::Function ? c_CompletionMethod : c_CompletionField, symbol.Signature, symbol.Doc);
			}
			return items;
		}

		// The script's own names first, then what it uses, the engine, and the language.
		for (const Symbol& symbol : symbols)
		{
			// A kind's functions and fields are offered after a '.', not here.
			if (!symbol.Owner.empty() && symbol.SymbolKind != Symbol::Kind::Parameter && symbol.SymbolKind != Symbol::Kind::Module)
				continue;
			switch (symbol.SymbolKind)
			{
				case Symbol::Kind::Function: item(symbol.Name, c_CompletionFunction, symbol.Signature, symbol.Doc); break;
				case Symbol::Kind::Phrase: item(symbol.Name, c_CompletionFunction, symbol.Signature, symbol.Doc, PhraseInsertText(symbol.Name)); break;
				case Symbol::Kind::KindType: item(symbol.Name, c_CompletionClass, symbol.Signature, symbol.Doc); break;
				case Symbol::Kind::Constant: item(symbol.Name, c_CompletionConstant, symbol.Signature, symbol.Doc); break;
				case Symbol::Kind::Variable:
				case Symbol::Kind::Parameter: item(symbol.Name, c_CompletionVariable, symbol.Signature, symbol.Doc); break;
				case Symbol::Kind::Module:
					if (!symbol.Owner.empty())
						item(symbol.Owner, c_CompletionModule, symbol.Signature, "");
					break;
			}
		}
		for (const Symbol& symbol : used)
		{
			if (!symbol.Owner.empty())
				continue;
			const bool phrase = symbol.SymbolKind == Symbol::Kind::Phrase;
			item(symbol.Name, symbol.SymbolKind == Symbol::Kind::KindType ? c_CompletionClass : c_CompletionFunction, symbol.Signature, symbol.Doc,
				phrase ? PhraseInsertText(symbol.Name) : "");
		}
		for (const ApiFunction& function : m_ApiFunctions)
			item(function.Name, c_CompletionFunction, function.Signature, function.Doc);
		for (const ApiType& type : m_ApiTypes)
			item(type.Name, c_CompletionClass, "engine type " + type.Name, type.Doc);
		for (const ApiPhrase& phrase : m_ApiPhrases)
			item(phrase.Pattern, c_CompletionFunction, phrase.Pattern + (phrase.Returns.empty() ? "" : " -> " + ForgeTypeName(phrase.Returns)), phrase.Doc, PhraseInsertText(phrase.Pattern));
		for (const HelpEntry& help : GetLanguageHelp())
		{
			if (help.EntryKind == HelpEntry::Kind::Keyword)
				item(help.Name, c_CompletionKeyword, help.Signature, help.Doc);
			else if (help.EntryKind == HelpEntry::Kind::Function)
				item(help.Name, c_CompletionFunction, help.Signature, help.Doc);
			else if (help.EntryKind == HelpEntry::Kind::Phrase)
				item(help.Name, c_CompletionFunction, help.Signature, help.Doc, PhraseInsertText(help.Name));
		}
		return items;
	}

	Json LanguageServer::Hover(const Json& params)
	{
		const Document* document = FindDocument(params.Get("textDocument").GetString("uri"));
		if (!document)
			return Json();
		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const int lineIndex = static_cast<int>(params.Get("position").Get("line").AsNumber());
		if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size()))
			return Json();
		const std::string_view line = lines[static_cast<size_t>(lineIndex)];
		const size_t offset = FromUtf16(line, static_cast<int>(params.Get("position").Get("character").AsNumber()));
		size_t wordStart = 0;
		size_t wordEnd = 0;
		const std::string word = WordAt(line, offset, wordStart, wordEnd);
		if (word.empty())
			return Json();
		bool member = false;
		const std::string receiver = ReceiverBefore(line, wordStart, member);

		std::vector<std::string> parts;
		const std::vector<Symbol> symbols = FindScriptSymbols(document->Text);
		const std::vector<Symbol> used = UsedSymbols(document->Path, document->Text);
		if (member)
		{
			for (const Symbol& symbol : used)
			{
				if (symbol.Owner == receiver && symbol.Name == word)
					parts.push_back(HelpMarkdown(symbol.Signature, symbol.Doc));
			}
			for (const Symbol& symbol : symbols)
			{
				if (!symbol.Owner.empty() && symbol.Name == word && symbol.SymbolKind != Symbol::Kind::Parameter)
					parts.push_back(HelpMarkdown(symbol.Signature, symbol.Doc));
			}
			for (const ApiType& type : m_ApiTypes)
			{
				for (const ApiMember& entry : type.Members)
				{
					if (entry.Name == word)
						parts.push_back(HelpMarkdown(entry.Signature, entry.Doc));
				}
			}
			for (const HelpEntry& help : GetLanguageHelp())
			{
				if (help.EntryKind == HelpEntry::Kind::Method && help.Name == word)
					parts.push_back(HelpMarkdown(help.Signature, help.Doc));
			}
		}
		else
		{
			bool found = false;
			for (const Symbol& symbol : symbols)
			{
				const bool phraseWord = symbol.SymbolKind == Symbol::Kind::Phrase && LineUsesPhrase(symbol.Name, word, line);
				if ((symbol.Name == word || phraseWord) && symbol.Owner.empty() && symbol.SymbolKind != Symbol::Kind::Module)
				{
					parts.push_back(HelpMarkdown(symbol.Signature, symbol.Doc));
					found = true;
					break;
				}
			}
			for (const Symbol& symbol : used)
			{
				const bool phraseWord = symbol.SymbolKind == Symbol::Kind::Phrase && LineUsesPhrase(symbol.Name, word, line);
				if (!found && (symbol.Name == word || phraseWord) && symbol.Owner.empty())
				{
					parts.push_back(HelpMarkdown(symbol.Signature, symbol.Doc));
					found = true;
				}
			}
			if (!found)
			{
				if (const HelpEntry* keyword = FindKeywordHelp(word))
					parts.push_back(HelpMarkdown(keyword->Signature, keyword->Doc));
				for (const ApiFunction& function : m_ApiFunctions)
				{
					if (function.Name == word)
						parts.push_back(HelpMarkdown(function.Signature, function.Doc));
				}
				for (const ApiType& type : m_ApiTypes)
				{
					if (type.Name == word)
						parts.push_back(HelpMarkdown("engine type " + type.Name, type.Doc));
				}
				for (const ApiPhrase& phrase : m_ApiPhrases)
				{
					if (LineUsesPhrase(phrase.Pattern, word, line))
						parts.push_back(HelpMarkdown(phrase.Pattern + (phrase.Returns.empty() ? "" : " -> " + ForgeTypeName(phrase.Returns)), phrase.Doc));
				}
				for (const HelpEntry& help : GetLanguageHelp())
				{
					const bool phraseWord = help.EntryKind == HelpEntry::Kind::Phrase && LineUsesPhrase(help.Name, word, line);
					if ((help.EntryKind == HelpEntry::Kind::Function && help.Name == word) || phraseWord)
						parts.push_back(HelpMarkdown(help.Signature, help.Doc));
				}
			}
		}
		if (parts.empty())
			return Json();

		std::string value;
		for (const std::string& part : parts)
			value += (value.empty() ? "" : "\n\n---\n\n") + part;
		Json contents;
		contents.Set("kind", "markdown").Set("value", value);
		Json range;
		range.Set("start", Position(lineIndex, ToUtf16(line, wordStart)));
		range.Set("end", Position(lineIndex, ToUtf16(line, wordEnd)));
		Json result;
		result.Set("contents", std::move(contents)).Set("range", std::move(range));
		return result;
	}

	Json LanguageServer::Definition(const Json& params)
	{
		const std::string& uri = params.Get("textDocument").GetString("uri");
		const Document* document = FindDocument(uri);
		if (!document)
			return Json();
		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const int lineIndex = static_cast<int>(params.Get("position").Get("line").AsNumber());
		if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size()))
			return Json();
		const std::string_view line = lines[static_cast<size_t>(lineIndex)];
		const size_t offset = FromUtf16(line, static_cast<int>(params.Get("position").Get("character").AsNumber()));
		const std::vector<Symbol> symbols = FindScriptSymbols(document->Text);

		// On the quoted path of a 'use' line: the start of the script it names.
		for (const Symbol& symbol : symbols)
		{
			const size_t pathStart = static_cast<size_t>(symbol.Column - 1);
			if (symbol.SymbolKind != Symbol::Kind::Module || symbol.Line != lineIndex + 1 ||
				offset < pathStart || offset >= pathStart + static_cast<size_t>(symbol.Length))
				continue;
			const std::filesystem::path file = ResolveUsePath(FindUseFolder(document->Path), symbol.File).lexically_normal();
			std::string text;
			if (!ReadScriptForUse(file, text))
				return Json();
			Json range;
			range.Set("start", Position(0, 0)).Set("end", Position(0, 0));
			Json result;
			result.Set("uri", PathToUri(file)).Set("range", std::move(range));
			return result;
		}

		size_t wordStart = 0;
		size_t wordEnd = 0;
		const std::string word = WordAt(line, offset, wordStart, wordEnd);
		if (word.empty())
			return Json();
		bool member = false;
		const std::string receiver = ReceiverBefore(line, wordStart, member);

		auto location = [](const std::string& targetUri, const std::string& text, const Symbol& symbol)
		{
			Json result;
			result.Set("uri", targetUri).Set("range", RangeOf(text, symbol.Line, symbol.Column, symbol.Length));
			return result;
		};
		auto matches = [&word](const Symbol& symbol)
		{
			return symbol.SymbolKind != Symbol::Kind::Module &&
				(symbol.Name == word || (symbol.SymbolKind == Symbol::Kind::Phrase && PhraseFirstWord(symbol.Name) == word));
		};

		// In this script: definitions before parameters, so a name means the function, not an argument.
		if (!member || receiver == "my" || receiver == "self")
		{
			for (int pass = 0; pass < 2; pass++)
			{
				for (const Symbol& symbol : symbols)
				{
					if ((symbol.SymbolKind == Symbol::Kind::Parameter) == (pass == 0))
						continue;
					if (matches(symbol) && (member ? !symbol.Owner.empty() : true))
						return location(uri, document->Text, symbol);
				}
			}
		}
		else
		{
			for (const Symbol& symbol : symbols)
			{
				if (!symbol.Owner.empty() && matches(symbol))
					return location(uri, document->Text, symbol);
			}
		}

		// In a script it uses; 'helpers.greet' looks in the script used 'as helpers'.
		for (const Symbol& symbol : UsedSymbols(document->Path, document->Text))
		{
			if (matches(symbol) && (member ? symbol.Owner == receiver : symbol.Owner.empty()))
			{
				const std::filesystem::path file = PathFromUtf8(symbol.File);
				std::string text;
				ReadScriptForUse(file, text);
				return location(PathToUri(file), text, symbol);
			}
		}
		return Json();
	}


	std::vector<OutlineItem> FindOutline(const std::string& source)
	{
		using Kind = OutlineItem::Kind;
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();

		// Items are collected flat with their parent, and nested at the end, so growing the list never
		// leaves a dangling reference.
		struct Flat
		{
			OutlineItem Item;
			int Parent = -1;
		};
		std::vector<Flat> flat;
		// One entry per open block: the item it belongs to, or -1 for a block that is not an item.
		std::vector<int> open;
		std::set<std::pair<int, std::string>> variables;

		auto lineText = [&tokens](size_t first)
		{
			std::string text;
			for (size_t j = first; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				text += (text.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
			return text;
		};
		auto add = [&flat](Kind kind, std::string name, std::string detail, const Token& at, const Token& nameToken, int length, int parent)
		{
			Flat entry;
			entry.Item.ItemKind = kind;
			entry.Item.Name = std::move(name);
			entry.Item.Detail = std::move(detail);
			entry.Item.StartLine = at.Line;
			entry.Item.EndLine = at.Line;
			entry.Item.NameLine = nameToken.Line;
			entry.Item.NameColumn = nameToken.Column;
			entry.Item.NameLength = length;
			entry.Parent = parent;
			flat.push_back(std::move(entry));
			return static_cast<int>(flat.size()) - 1;
		};

		int lastLine = 1;
		for (size_t i = 0; i < tokens.size() && tokens[i].Type != TokenType::Eof; i++)
		{
			const Token& token = tokens[i];
			lastLine = token.Line;
			if (token.Type == TokenType::End)
			{
				if (!open.empty())
				{
					if (open.back() >= 0)
						flat[static_cast<size_t>(open.back())].Item.EndLine = token.Line;
					open.pop_back();
				}
				continue;
			}

			// Only the script's top level and a kind's body are listed, not the code inside functions.
			const int parent = open.empty() ? -1 : open.back();
			const bool listed = open.empty() || (parent >= 0 && flat[static_cast<size_t>(parent)].Item.ItemKind == Kind::KindType);
			const bool inKind = !open.empty();
			const bool opens = TokenOpensBlock(tokens, i);
			const Token* next = i + 1 < tokens.size() ? &tokens[i + 1] : nullptr;
			int item = -1;

			if (listed && opens && token.Type == TokenType::Function && next && next->Type == TokenType::Identifier)
			{
				std::string params;
				for (size_t j = i + 3; j < tokens.size() && tokens[i + 2].Type == TokenType::LeftParen && tokens[j].Type != TokenType::RightParen &&
					tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				{
					if (tokens[j].Type == TokenType::Identifier && tokens[j].Lexeme == "as" && j + 1 < tokens.size())
					{
						params += " as " + std::string(tokens[++j].Lexeme);
						continue;
					}
					if (tokens[j].Type == TokenType::Identifier)
						params += (params.empty() ? "" : ", ") + std::string(tokens[j].Lexeme) + (CollectsTheRest(tokens, j) ? "..." : "");
				}
				item = add(Kind::Function, std::string(next->Lexeme), "function " + std::string(next->Lexeme) + "(" + params + ")", token, *next,
					static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && !inKind && opens && token.Type == TokenType::Phrase && next)
			{
				std::string pattern;
				for (size_t j = i + 1; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
				{
					if (tokens[j].Type == TokenType::LeftParen && j + 2 < tokens.size() && tokens[j + 2].Type == TokenType::RightParen)
					{
						pattern += (pattern.empty() ? "(" : " (") + std::string(tokens[j + 1].Lexeme) + ")";
						j += 2;
					}
					else if (tokens[j].IsWord())
					{
						pattern += (pattern.empty() ? "" : " ") + std::string(tokens[j].Lexeme);
					}
				}
				item = add(Kind::Phrase, pattern, "phrase " + pattern, token, *next, static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && !inKind && opens && token.Type == TokenType::Kind && next && next->Type == TokenType::Identifier)
			{
				std::string detail = "kind " + std::string(next->Lexeme);
				if (i + 3 < tokens.size() && tokens[i + 2].Type == TokenType::Extends)
					detail += " extends " + std::string(tokens[i + 3].Lexeme);
				item = add(Kind::KindType, std::string(next->Lexeme), detail, token, *next, static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && opens && token.Type == TokenType::When)
			{
				// The whole 'when ...' line names the handler; it is selected from 'when' to its last word.
				size_t last = i;
				for (size_t j = i + 1; j < tokens.size() && tokens[j].Type != TokenType::Newline && tokens[j].Type != TokenType::Eof; j++)
					last = j;
				const int length = tokens[last].Column + static_cast<int>(tokens[last].Lexeme.size()) - token.Column;
				item = add(Kind::Handler, lineText(i), "", token, token, length, parent);
			}
			else if (listed && !inKind && opens && TestOpensBlock(tokens, i))
			{
				// 'test "name"' is listed like a handler, selected from 'test' to the end of its name.
				const int length = next->Column + static_cast<int>(next->Lexeme.size()) - token.Column;
				item = add(Kind::Handler, "test " + std::string(next->Lexeme), "", token, token, length, parent);
			}
			else if (listed && inKind && IsSharedLine(tokens, i))
			{
				if (variables.insert({ parent, std::string(next->Lexeme) }).second)
					add(Kind::Variable, std::string(next->Lexeme), RestOfLine(tokens, i), token, *next, static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && token.Type == TokenType::Set && next && next->Type == TokenType::Identifier &&
				i + 2 < tokens.size() && tokens[i + 2].Type == TokenType::To)
			{
				// The first 'set' of a name defines it.
				if (variables.insert({ parent, std::string(next->Lexeme) }).second)
					add(Kind::Variable, std::string(next->Lexeme), "set " + std::string(next->Lexeme) + " to " + lineText(i + 3), token, *next,
						static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && token.Type == TokenType::Set && !UnpackedNames(tokens, i + 1).empty())
			{
				for (size_t nameIndex : UnpackedNames(tokens, i + 1))
				{
					const Token& name = tokens[nameIndex];
					if (variables.insert({ parent, std::string(name.Lexeme) }).second)
						add(Kind::Variable, std::string(name.Lexeme), lineText(i), token, name, static_cast<int>(name.Lexeme.size()), parent);
				}
			}
			else if (listed && !inKind && IsChoicesLine(tokens, i))
			{
				add(Kind::Constant, std::string(next->Lexeme), RestOfLine(tokens, i), token, *next, static_cast<int>(next->Lexeme.size()), parent);
			}
			else if (listed && !inKind && token.Type == TokenType::Constant && next && next->Type == TokenType::Identifier)
			{
				add(Kind::Constant, std::string(next->Lexeme), "constant " + lineText(i + 1), token, *next, static_cast<int>(next->Lexeme.size()), parent);
			}

			if (opens)
				open.push_back(item);
		}
		// A block left open (a mistake) runs to the end of the script.
		for (int entry : open)
		{
			if (entry >= 0)
				flat[static_cast<size_t>(entry)].Item.EndLine = lastLine;
		}

		// Nest children under their parents, last first, so each moves into a parent still in 'flat'.
		for (size_t i = flat.size(); i-- > 0;)
		{
			if (flat[i].Parent >= 0)
			{
				std::vector<OutlineItem>& children = flat[static_cast<size_t>(flat[i].Parent)].Item.Children;
				children.insert(children.begin(), std::move(flat[i].Item));
			}
		}
		std::vector<OutlineItem> outline;
		for (Flat& entry : flat)
		{
			if (entry.Parent < 0)
				outline.push_back(std::move(entry.Item));
		}
		return outline;
	}

	Json LanguageServer::DocumentSymbols(const Json& params)
	{
		const Document* document = FindDocument(params.Get("textDocument").GetString("uri"));
		if (!document)
			return Json();
		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const std::string& text = document->Text;

		std::function<Json(const OutlineItem&, bool)> toJson = [&](const OutlineItem& item, bool inKind)
		{
			int kind = c_SymbolFunction;
			switch (item.ItemKind)
			{
				case OutlineItem::Kind::Function: kind = inKind ? c_SymbolMethod : c_SymbolFunction; break;
				case OutlineItem::Kind::Phrase:   kind = c_SymbolFunction; break;
				case OutlineItem::Kind::KindType: kind = c_SymbolClass; break;
				case OutlineItem::Kind::Handler:  kind = c_SymbolEvent; break;
				case OutlineItem::Kind::Variable: kind = inKind ? c_SymbolField : c_SymbolVariable; break;
				case OutlineItem::Kind::Constant: kind = c_SymbolConstant; break;
			}
			// The whole lines from the definition to its 'end'.
			const size_t endIndex = static_cast<size_t>(std::clamp(item.EndLine - 1, 0, static_cast<int>(lines.size()) - 1));
			Json range;
			range.Set("start", Position(std::max(item.StartLine - 1, 0), 0));
			range.Set("end", Position(static_cast<int>(endIndex), ToUtf16(lines[endIndex], lines[endIndex].size())));
			Json symbol;
			symbol.Set("name", item.Name).Set("kind", kind).Set("range", std::move(range))
				.Set("selectionRange", RangeOf(text, item.NameLine, item.NameColumn, item.NameLength));
			if (!item.Detail.empty())
				symbol.Set("detail", item.Detail);
			if (!item.Children.empty())
			{
				Json children = Json(Json::Array());
				for (const OutlineItem& child : item.Children)
					children.Add(toJson(child, item.ItemKind == OutlineItem::Kind::KindType));
				symbol.Set("children", std::move(children));
			}
			return symbol;
		};

		Json symbols = Json(Json::Array());
		for (const OutlineItem& item : FindOutline(text))
			symbols.Add(toJson(item, false));
		return symbols;
	}

	namespace {

		// Where each parameter sits in a signature label such as "play_sound(path: text, volume: number = 1)",
		// as [start, end) UTF-16 offsets, which LSP clients use to highlight the current one.
		Json ParameterRanges(const std::string& label)
		{
			Json ranges = Json(Json::Array());
			const size_t open = label.find('(');
			if (open == std::string::npos)
				return ranges;
			int depth = 0;
			bool quoted = false;
			size_t start = open + 1;
			for (size_t i = open + 1; i < label.size(); i++)
			{
				const char c = label[i];
				if (c == '"')
					quoted = !quoted;
				if (quoted)
					continue;
				if (c == '(' || c == '[' || c == '{')
					depth++;
				else if ((c == ')' || c == ']' || c == '}') && depth > 0)
					depth--;
				else if ((c == ',' || c == ')') && depth == 0)
				{
					size_t first = start;
					size_t last = i;
					while (first < last && label[first] == ' ')
						first++;
					while (last > first && label[last - 1] == ' ')
						last--;
					if (last > first)
					{
						Json range;
						range.Add(Json(static_cast<double>(ToUtf16(label, first)))).Add(Json(static_cast<double>(ToUtf16(label, last))));
						Json parameter;
						parameter.Set("label", std::move(range));
						ranges.Add(std::move(parameter));
					}
					start = i + 1;
					if (c == ')')
						break;
				}
			}
			return ranges;
		}

	}

	namespace {

		// The parameters' names in a signature label: "attack(target, damage is 10)" gives target, damage;
		// "play(path: text = \"\")" gives path.
		std::vector<std::string> ParameterNames(const std::string& label)
		{
			std::vector<std::string> names;
			const size_t open = label.find('(');
			if (open == std::string::npos)
				return names;
			int depth = 0;
			bool quoted = false;
			bool atStart = true;
			for (size_t i = open + 1; i < label.size(); i++)
			{
				const char c = label[i];
				if (c == '"')
					quoted = !quoted;
				if (quoted)
					continue;
				if (depth == 0 && atStart && (std::isalpha(static_cast<unsigned char>(c)) || c == '_'))
				{
					size_t end = i;
					while (end < label.size() && (std::isalnum(static_cast<unsigned char>(label[end])) || label[end] == '_'))
						end++;
					names.push_back(label.substr(i, end - i));
					atStart = false;
					i = end - 1;
					continue;
				}
				if (c == '(' || c == '[' || c == '{')
					depth++;
				else if ((c == ')' || c == ']' || c == '}') && depth > 0)
					depth--;
				else if (c == ')' && depth == 0)
					break;
				else if (c == ',' && depth == 0)
					atStart = true;
			}
			return names;
		}

	}

	const NameUse* LanguageServer::NameUseAt(const Document& document, const Json& params, std::vector<NameUse>& outUses,
		std::vector<CompileError>& outMistakes) const
	{
		VM vm;
		SetUpChecker(vm, document.Path);
		outUses = vm.FindNameUses(document.Text, PathToUtf8(document.Path.filename()), &outMistakes);
		const std::vector<std::string_view> lines = SplitLines(document.Text);
		const int lineIndex = static_cast<int>(params.Get("position").Get("line").AsNumber());
		if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size()))
			return nullptr;
		const size_t offset = FromUtf16(lines[static_cast<size_t>(lineIndex)], static_cast<int>(params.Get("position").Get("character").AsNumber()));
		// The cursor may be anywhere in the name or just after it, as when it was just typed.
		for (const NameUse& use : outUses)
		{
			const size_t start = static_cast<size_t>(use.Column - 1);
			if (use.Line == lineIndex + 1 && offset >= start && offset <= start + static_cast<size_t>(use.Length))
				return &use;
		}
		return nullptr;
	}

	Json LanguageServer::References(const Json& params)
	{
		const std::string& uri = params.Get("textDocument").GetString("uri");
		const Document* document = FindDocument(uri);
		if (!document)
			return Json();
		std::vector<NameUse> uses;
		std::vector<CompileError> mistakes;
		const NameUse* at = NameUseAt(*document, params, uses, mistakes);
		if (!at)
			return Json();
		// Without a context, the declaration is included, as most editors ask for it.
		const Json& context = params.Get("context");
		const bool withDeclaration = context.Get("includeDeclaration").IsNull() || context.Get("includeDeclaration").AsBool();
		Json locations = Json(Json::Array());
		for (const NameUse& use : uses)
		{
			if (use.Key != at->Key || (use.Declares && !withDeclaration))
				continue;
			Json location;
			location.Set("uri", uri).Set("range", RangeOf(document->Text, use.Line, use.Column, use.Length));
			locations.Add(std::move(location));
		}
		return locations;
	}

	std::string LanguageServer::CheckRenamable(const Document& document, const Json& params, std::vector<NameUse>& outUses, NameUse& outUse) const
	{
		std::vector<CompileError> mistakes;
		const NameUse* at = NameUseAt(document, params, outUses, mistakes);
		if (!mistakes.empty())
			return "Fix the mistakes in this script first: rename needs a script that works, to be sure what each name means.";
		if (!at)
			return "Put the cursor on the name of a variable, parameter, function or kind that this script makes.";
		outUse = *at;
		const std::string oldName(SplitLines(document.Text)[static_cast<size_t>(outUse.Line - 1)].substr(static_cast<size_t>(outUse.Column - 1),
			static_cast<size_t>(outUse.Length)));
		if (outUse.Key.rfind("F:", 0) == 0)
			return "'" + oldName + "' is a field or method. Rename doesn't change those yet, because which kind 'my " + oldName +
				"' or 'thing." + oldName + "' belongs to can't always be told before the script runs.";
		const bool scriptLevel = outUse.Key.rfind("G:", 0) == 0;
		if (scriptLevel && std::none_of(outUses.begin(), outUses.end(), [&](const NameUse& use) { return use.Key == outUse.Key && use.Declares; }))
			return "'" + oldName + "' isn't made in this script (it is built in, or comes from a script this one uses), so it can't be renamed here.";

		// Every word spelled like the name must be one the compiler placed, or renaming could miss one
		// (a phrase's words, for example). A field after '.', 's or 'my' is a different thing.
		Lexer lexer(document.Text);
		const std::vector<Token> tokens = lexer.Tokenize();
		std::set<size_t> placed;
		for (const NameUse& use : outUses)
			placed.insert(use.Token);
		for (size_t i = 0; i < tokens.size(); i++)
		{
			if (tokens[i].Type != TokenType::Identifier || tokens[i].Lexeme != oldName || placed.count(i))
				continue;
			if (i > 0 && (tokens[i - 1].Type == TokenType::Dot || tokens[i - 1].Type == TokenType::Possessive || tokens[i - 1].Type == TokenType::My))
				continue;
			return "'" + oldName + "' is also written on line " + std::to_string(tokens[i].Line) +
				" in a way rename can't follow yet (like a word of a phrase), so renaming it could break the script.";
		}

		// Rename changes only this script, so a script-level name must not be needed by another open one.
		if (scriptLevel)
		{
			for (const auto& [otherUri, other] : m_Documents)
			{
				if (other.Path == document.Path)
					continue;
				for (const Symbol& symbol : FindScriptSymbols(other.Text))
				{
					if (symbol.SymbolKind == Symbol::Kind::Module &&
						ResolveUsePath(FindUseFolder(other.Path), symbol.File).lexically_normal() == document.Path)
						return "'" + PathToUtf8(other.Path.filename()) + "' uses this script, and rename only changes this one. Rename '" +
							oldName + "' in both by hand, or close that script first.";
				}
			}
		}
		return "";
	}

	Json LanguageServer::PrepareRename(const Json& params, std::string& outError)
	{
		const Document* document = FindDocument(params.Get("textDocument").GetString("uri"));
		if (!document)
		{
			outError = "This script isn't open.";
			return Json();
		}
		std::vector<NameUse> uses;
		NameUse use;
		outError = CheckRenamable(*document, params, uses, use);
		if (!outError.empty())
			return Json();
		Json result;
		result.Set("range", RangeOf(document->Text, use.Line, use.Column, use.Length))
			.Set("placeholder", std::string(SplitLines(document->Text)[static_cast<size_t>(use.Line - 1)].substr(static_cast<size_t>(use.Column - 1), static_cast<size_t>(use.Length))));
		return result;
	}

	Json LanguageServer::Rename(const Json& params, std::string& outError)
	{
		const std::string& uri = params.Get("textDocument").GetString("uri");
		const Document* document = FindDocument(uri);
		if (!document)
		{
			outError = "This script isn't open.";
			return Json();
		}
		std::vector<NameUse> uses;
		NameUse target;
		outError = CheckRenamable(*document, params, uses, target);
		if (!outError.empty())
			return Json();
		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const std::string oldName(lines[static_cast<size_t>(target.Line - 1)].substr(static_cast<size_t>(target.Column - 1), static_cast<size_t>(target.Length)));
		const std::string newName = params.GetString("newName");

		// The new name must be one name of a sensible length, and not a keyword.
		if (newName.size() > 255)
		{
			outError = "A name can be at most 255 characters long.";
			return Json();
		}
		Lexer lexer(newName);
		const std::vector<Token> newTokens = lexer.Tokenize();
		if (newTokens.empty() || newTokens[0].Lexeme != newName || (newTokens.size() > 1 && newTokens[1].Type != TokenType::Eof && newTokens[1].Type != TokenType::Newline))
		{
			outError = "'" + newName + "' isn't one name. A name is one word of letters, digits and _, starting with a letter or _.";
			return Json();
		}
		if (newTokens[0].Type != TokenType::Identifier && newTokens[0].IsWord())
		{
			outError = "'" + newName + "' is a ForgeScript keyword, so it can't be a name.";
			return Json();
		}
		if (newTokens[0].Type != TokenType::Identifier)
		{
			outError = "'" + newName + "' isn't a name. A name is one word of letters, digits and _, starting with a letter or _.";
			return Json();
		}

		VM vm;
		SetUpChecker(vm, document->Path);
		const bool engineFunction = std::any_of(m_ApiFunctions.begin(), m_ApiFunctions.end(), [&newName](const ApiFunction& function) { return function.Name == newName; });
		if (newName != oldName && (vm.HasSharedGlobal(newName) || engineFunction))
		{
			outError = "'" + newName + "' is a built-in function, so the script couldn't call it any more under that name. Pick another name.";
			return Json();
		}

		// The renamed script, checked: it must compile, warn no more, and every name in it must mean
		// what it meant before (so the new name doesn't take over, or hide, another one).
		std::vector<const NameUse*> renamed;
		for (const NameUse& use : uses)
		{
			if (use.Key == target.Key)
				renamed.push_back(&use);
		}
		std::string text;
		size_t copied = 0;
		for (const NameUse* use : renamed)
		{
			// The lines are views into the document's text, so a line starts where its view does.
			const size_t lineStart = static_cast<size_t>(lines[static_cast<size_t>(use->Line - 1)].data() - document->Text.data());
			const size_t start = lineStart + static_cast<size_t>(use->Column - 1);
			text.append(document->Text, copied, start - copied);
			text += newName;
			copied = start + static_cast<size_t>(use->Length);
		}
		text.append(document->Text, copied, std::string::npos);

		std::vector<CompileError> mistakes;
		const std::string fileName = PathToUtf8(document->Path.filename());
		const std::vector<NameUse> after = vm.FindNameUses(text, fileName, &mistakes);
		if (!mistakes.empty())
		{
			outError = "Renaming '" + oldName + "' to '" + newName + "' would make this script stop working: " + mistakes.front().Message;
			return Json();
		}
		bool same = after.size() == uses.size();
		std::map<std::string, std::string> keys;
		std::set<std::string> taken;
		for (size_t i = 0; same && i < uses.size(); i++)
		{
			same = uses[i].Token == after[i].Token;
			auto [it, added] = keys.emplace(uses[i].Key, after[i].Key);
			if (added)
				same = same && taken.insert(after[i].Key).second;
			else
				same = same && it->second == after[i].Key;
		}
		if (!same)
		{
			outError = "'" + newName + "' already means something else where '" + oldName + "' is used, so renaming would change what the script does. Pick another name.";
			return Json();
		}
		if (FindWarningsIn(document->Path, text).size() > FindWarningsIn(document->Path, document->Text).size())
		{
			outError = "Renaming '" + oldName + "' to '" + newName + "' would give this script a new warning. Pick another name.";
			return Json();
		}

		Json edits = Json(Json::Array());
		for (const NameUse* use : renamed)
		{
			Json edit;
			edit.Set("range", RangeOf(document->Text, use->Line, use->Column, use->Length)).Set("newText", newName);
			edits.Add(std::move(edit));
		}
		Json changes;
		changes.Set(uri, std::move(edits));
		Json result;
		result.Set("changes", std::move(changes));
		return result;
	}

	Json LanguageServer::SignatureHelp(const Json& params)
	{
		const Document* document = FindDocument(params.Get("textDocument").GetString("uri"));
		if (!document)
			return Json();
		const std::vector<std::string_view> lines = SplitLines(document->Text);
		const int lineIndex = static_cast<int>(params.Get("position").Get("line").AsNumber());
		if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size()))
			return Json();
		const std::string_view line = lines[static_cast<size_t>(lineIndex)];
		const size_t offset = std::min(FromUtf16(line, static_cast<int>(params.Get("position").Get("character").AsNumber())), line.size());

		// The innermost '(' still open before the cursor, and how many of its commas come before it.
		struct Bracket
		{
			char Open;
			size_t Position;
			int Commas;
			size_t ValueStart; // where the value being typed starts: after the '(' or the last ','
		};
		std::vector<Bracket> brackets;
		bool quoted = false;
		for (size_t i = 0; i < offset; i++)
		{
			const char c = line[i];
			if (c == '"' && (i == 0 || line[i - 1] != '\\'))
				quoted = !quoted;
			if (quoted)
				continue;
			if (c == '#')
				break;
			if (c == '(' || c == '[' || c == '{')
				brackets.push_back({ c, i, 0, i + 1 });
			else if ((c == ')' || c == ']' || c == '}') && !brackets.empty())
				brackets.pop_back();
			else if (c == ',' && !brackets.empty())
			{
				brackets.back().Commas++;
				brackets.back().ValueStart = i + 1;
			}
		}
		if (quoted)
			return Json();
		const Bracket* call = nullptr;
		for (auto it = brackets.rbegin(); it != brackets.rend() && !call; ++it)
		{
			if (it->Open == '(')
				call = &*it;
		}
		if (!call)
			return Json();

		// The name right before the '(' (spaces allowed), and what it belongs to after a '.'.
		size_t nameEnd = call->Position;
		while (nameEnd > 0 && line[nameEnd - 1] == ' ')
			nameEnd--;
		size_t nameStart = nameEnd;
		while (nameStart > 0 && IsWordChar(line[nameStart - 1]))
			nameStart--;
		if (nameStart == nameEnd)
			return Json();
		const std::string name(line.substr(nameStart, nameEnd - nameStart));
		bool member = false;
		const std::string receiver = ReceiverBefore(line, nameStart, member);
		const std::vector<Symbol> symbols = FindScriptSymbols(document->Text);

		std::vector<std::pair<std::string, std::string>> found; // Label and documentation.
		auto offer = [&found](std::string label, std::string doc)
		{
			for (const auto& existing : found)
			{
				if (existing.first == label)
					return;
			}
			found.emplace_back(std::move(label), std::move(doc));
		};
		auto scriptLabel = [](const std::string& signature)
		{
			// "function Door.open(force)" shows as "open(force)" while it is being called.
			std::string label = signature.starts_with("function ") ? signature.substr(9) : signature;
			const size_t dot = label.find('.');
			const size_t paren = label.find('(');
			if (dot != std::string::npos && dot < paren)
				label = label.substr(dot + 1);
			return label;
		};

		if (member)
		{
			const std::string known = GuessType(lines, symbols, receiver, lineIndex, 0);
			if (const ApiType* apiType = FindApiType(known))
			{
				for (const ApiMember& entry : apiType->Members)
				{
					if (entry.Name == name && !entry.IsProperty)
						offer(entry.Signature, entry.Doc);
				}
			}
			else if (known.starts_with("kind:"))
			{
				for (const Symbol& symbol : symbols)
				{
					if (symbol.SymbolKind == Symbol::Kind::Function && symbol.Name == name && !symbol.Owner.empty())
						offer(scriptLabel(symbol.Signature), symbol.Doc);
				}
			}
			else
			{
				// Not known: every method of that name, from the engine, the language and the script's kinds.
				if (known.empty() || known == "text" || known == "list" || known == "map" || known == "vector")
				{
					for (const HelpEntry& help : GetLanguageHelp())
					{
						if (help.EntryKind == HelpEntry::Kind::Method && !help.IsProperty && help.Name == name && (known.empty() || help.Owner == known))
							offer(help.Signature, help.Doc);
					}
				}
				if (known.empty())
				{
					for (const ApiType& type : m_ApiTypes)
					{
						for (const ApiMember& entry : type.Members)
						{
							if (entry.Name == name && !entry.IsProperty)
								offer(entry.Signature, entry.Doc);
						}
					}
					for (const Symbol& symbol : symbols)
					{
						if (symbol.SymbolKind == Symbol::Kind::Function && symbol.Name == name && !symbol.Owner.empty())
							offer(scriptLabel(symbol.Signature), symbol.Doc);
					}
				}
			}
		}
		else
		{
			for (const Symbol& symbol : symbols)
			{
				if (symbol.SymbolKind == Symbol::Kind::Function && symbol.Name == name && symbol.Owner.empty())
					offer(scriptLabel(symbol.Signature), symbol.Doc);
			}
			for (const ApiFunction& function : m_ApiFunctions)
			{
				if (function.Name == name)
					offer(function.Signature, function.Doc);
			}
			if (const HelpEntry* help = FindFunctionHelp(name); help && help->EntryKind == HelpEntry::Kind::Function)
				offer(help->Signature, help->Doc);
		}
		if (found.empty())
			return Json();

		constexpr size_t c_MaxSignatures = 8;
		Json signatures = Json(Json::Array());
		for (size_t i = 0; i < found.size() && i < c_MaxSignatures; i++)
		{
			Json signature;
			signature.Set("label", found[i].first).Set("parameters", ParameterRanges(found[i].first));
			if (!found[i].second.empty())
			{
				Json documentation;
				documentation.Set("kind", "markdown").Set("value", found[i].second);
				signature.Set("documentation", std::move(documentation));
			}
			signatures.Add(std::move(signature));
		}
		// A named value ("damage: 25") is for the parameter with that name, wherever it is in the list.
		int active = call->Commas;
		size_t start = call->ValueStart;
		while (start < offset && line[start] == ' ')
			start++;
		size_t end = start;
		while (end < offset && IsWordChar(line[end]))
			end++;
		size_t colon = end;
		while (colon < offset && line[colon] == ' ')
			colon++;
		if (end > start && colon < offset && line[colon] == ':')
		{
			const std::string named(line.substr(start, end - start));
			const std::vector<std::string> parameters = ParameterNames(found[0].first);
			auto it = std::find(parameters.begin(), parameters.end(), named);
			if (it != parameters.end())
				active = static_cast<int>(it - parameters.begin());
		}
		Json result;
		result.Set("signatures", std::move(signatures)).Set("activeSignature", 0).Set("activeParameter", active);
		return result;
	}

}
