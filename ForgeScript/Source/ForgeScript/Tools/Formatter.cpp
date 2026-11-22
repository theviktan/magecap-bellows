#include "ForgeScript/Tools/Formatter.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Compiler/ScriptVariables.h"

#include <algorithm>
#include <vector>

namespace ForgeScript {

	namespace {

		// One line of the source, without its line break.
		struct SourceLine
		{
			size_t Start = 0;
			size_t End = 0;    // where its text stops, before "\r\n" or "\n"
			size_t Next = 0;   // where the next line starts
			bool Keep = false; // inside long text or a #[ ]# comment: left exactly as it is
		};

		std::vector<SourceLine> SplitSourceLines(std::string_view source)
		{
			std::vector<SourceLine> lines;
			size_t start = 0;
			while (start <= source.size())
			{
				const size_t newline = source.find('\n', start);
				SourceLine line;
				line.Start = start;
				line.Next = newline == std::string_view::npos ? source.size() + 1 : newline + 1;
				line.End = newline == std::string_view::npos ? source.size() : newline;
				if (line.End > line.Start && source[line.End - 1] == '\r')
					line.End--;
				lines.push_back(line);
				if (newline == std::string_view::npos)
					break;
				start = newline + 1;
			}
			return lines;
		}

		// The line holding byte 'offset' (lines are in order).
		size_t LineOf(const std::vector<SourceLine>& lines, size_t offset)
		{
			auto it = std::upper_bound(lines.begin(), lines.end(), offset, [](size_t value, const SourceLine& line) { return value < line.Start; });
			return static_cast<size_t>(std::distance(lines.begin(), it)) - 1;
		}

		bool SameTokens(const std::vector<Token>& a, const std::vector<Token>& b)
		{
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); i++)
			{
				if (a[i].Type != b[i].Type || a[i].Text != b[i].Text || (a[i].Type != TokenType::Newline && a[i].Lexeme != b[i].Lexeme))
					return false;
			}
			return true;
		}

		// TokenOpensBlock, except that 'is a function' (a type test) opens nothing, as 'is a kind' doesn't.
		bool OpensBlock(const std::vector<Token>& tokens, size_t i)
		{
			if (tokens[i].Type == TokenType::Function && i > 0 && tokens[i - 1].Type == TokenType::Identifier &&
				(tokens[i - 1].Lexeme == "a" || tokens[i - 1].Lexeme == "an"))
				return false;
			return TokenOpensBlock(tokens, i);
		}

		// Something open when a line starts: a block (with the kinds whose middle lines sit one level
		// out) or a bracket, and the line that opened it.
		struct Open
		{
			enum class Type
			{
				Block,
				If,
				Match,
				Try,
				Bracket
			};
			Type Kind = Type::Block;
			size_t Line = 0;
		};
	}

	FormatResult FormatScript(std::string_view source)
	{
		FormatResult result;
		result.Text = std::string(source);
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();
		for (const Token& token : tokens)
		{
			if (token.Type == TokenType::Error)
			{
				result.Error = "The script has text ForgeScript can't read here, so it was left as it is: " + token.Text;
				result.ErrorLine = token.Line;
				return result;
			}
		}

		std::vector<SourceLine> lines = SplitSourceLines(source);
		auto offsetOf = [&source](const Token& token) { return static_cast<size_t>(token.Lexeme.data() - source.data()); };
		// Tokens the lexer makes up (the line break it adds at the end) point at no text, so they go by
		// their line number.
		auto lineOfToken = [&](const Token& token)
		{
			if (token.Lexeme.empty())
				return std::min(static_cast<size_t>(std::max(token.Line, 1) - 1), lines.size() - 1);
			return LineOf(lines, offsetOf(token));
		};

		// Lines inside a token (long text) or a #[ ]# comment keep every byte; so does the end of the
		// line where such a span starts, since its spaces may be part of the text.
		auto keepSpan = [&lines](size_t from, size_t to)
		{
			const size_t first = LineOf(lines, from);
			const size_t last = LineOf(lines, to);
			for (size_t i = first + 1; i <= last && i < lines.size(); i++)
				lines[i].Keep = true;
			return first;
		};
		std::vector<bool> keepEnd(lines.size(), false);
		auto keep = [&](size_t from, size_t to)
		{
			const size_t first = keepSpan(from, to);
			if (LineOf(lines, to) != first)
				keepEnd[first] = true;
		};
		size_t gapStart = 0;
		for (const Token& token : tokens)
		{
			if (token.Lexeme.empty() && token.Type != TokenType::Eof)
				continue;
			const size_t start = token.Type == TokenType::Eof ? source.size() : offsetOf(token);
			// Between tokens there are only spaces, line breaks and comments, read the way the lexer
			// reads them.
			for (size_t at = gapStart; at < start; at++)
			{
				if (source[at] != '#')
					continue;
				if (at + 1 < source.size() && source[at + 1] == '[')
				{
					const size_t close = source.find("]#", at + 2);
					const size_t end = close == std::string_view::npos ? source.size() - 1 : close + 1;
					keep(at, end);
					at = end;
				}
				else
				{
					const size_t end = source.find('\n', at);
					at = end == std::string_view::npos ? source.size() : end;
				}
			}
			if (token.Type == TokenType::Eof)
				break;
			if (!token.Lexeme.empty() && token.Type != TokenType::Newline)
				keep(start, start + token.Lexeme.size() - 1);
			gapStart = std::max(gapStart, start + token.Lexeme.size());
		}

		// The indent of each line: one tab for each earlier line that opened a block or a bracket still
		// open when this line starts (a line that opens several, like "(function()", counts once).
		// Lines that start by closing ('end', ')') or by moving to the next part of their block
		// ('else', a match's 'is', 'catch') sit at the level of the line that opened it.
		std::vector<int> indents(lines.size(), -1);
		std::vector<Open> open;
		auto levels = [&open](size_t count)
		{
			int distinct = 0;
			for (size_t i = 0; i < count; i++)
			{
				if (i == 0 || open[i].Line != open[i - 1].Line)
					distinct++;
			}
			return distinct;
		};
		size_t t = 0;
		for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++)
		{
			// The tokens that start on this line.
			const size_t first = t;
			while (t < tokens.size() && tokens[t].Type != TokenType::Eof && lineOfToken(tokens[t]) == lineIndex)
				t++;
			size_t lead = first;
			while (lead < t && tokens[lead].Type == TokenType::Newline)
				lead++;
			if (lead == t)
				continue; // blank, or only a comment: indented like the next code line, below

			// The closing tokens this line starts with, and whether it moves on within the block on top.
			size_t closing = 0;
			for (size_t i = lead; i < t && closing < open.size(); i++)
			{
				const Token& token = tokens[i];
				const TokenType previous = i > 0 ? tokens[i - 1].Type : TokenType::Newline;
				const Open& top = open[open.size() - 1 - closing];
				const bool closesBlock = token.Type == TokenType::End && previous != TokenType::Dot && previous != TokenType::Possessive &&
					top.Kind != Open::Type::Bracket;
				const bool closesBracket = (token.Type == TokenType::RightParen || token.Type == TokenType::RightBracket ||
					token.Type == TokenType::RightBrace) && top.Kind == Open::Type::Bracket;
				if (!closesBlock && !closesBracket)
					break;
				closing++;
			}
			bool movesOn = false;
			if (closing == 0 && !open.empty())
			{
				const Token& head = tokens[lead];
				const Open::Type top = open.back().Kind;
				movesOn = ((head.Type == TokenType::Else || head.Type == TokenType::Otherwise) && (top == Open::Type::If || top == Open::Type::Match)) ||
					(head.Type == TokenType::Is && top == Open::Type::Match) ||
					(top == Open::Type::Try && (head.Type == TokenType::Catch || (head.Type == TokenType::Identifier && head.Lexeme == "finally")));
			}
			int indent = levels(open.size() - closing - (movesOn ? 1 : 0));
			// A statement carried on from the line before (after '\\', or an operator at its end) goes one
			// further, except inside brackets, which are counted already.
			if (lead > 0 && tokens[lead - 1].Type != TokenType::Newline && (open.empty() || open.back().Kind != Open::Type::Bracket) &&
				closing == 0)
				indent++;
			// A line inside long text keeps its spaces, but what starts on it still opens and closes.
			if (!lines[lineIndex].Keep)
				indents[lineIndex] = indent;

			for (size_t i = lead; i < t; i++)
			{
				const Token& token = tokens[i];
				const TokenType previous = i > 0 ? tokens[i - 1].Type : TokenType::Newline;
				switch (token.Type)
				{
					case TokenType::LeftParen: case TokenType::LeftBracket: case TokenType::LeftBrace:
						open.push_back({ Open::Type::Bracket, lineIndex });
						break;
					case TokenType::RightParen: case TokenType::RightBracket: case TokenType::RightBrace:
						if (open.empty() || open.back().Kind != Open::Type::Bracket)
						{
							result.Error = "This closing bracket has no opening one to close, so the script was left as it is.";
							result.ErrorLine = token.Line;
							return result;
						}
						open.pop_back();
						break;
					case TokenType::End:
						if (previous != TokenType::Dot && previous != TokenType::Possessive)
						{
							if (open.empty() || open.back().Kind == Open::Type::Bracket)
							{
								result.Error = "This 'end' has no block to close, so the script was left as it is.";
								result.ErrorLine = token.Line;
								return result;
							}
							open.pop_back();
						}
						break;
					default:
						if (OpensBlock(tokens, i))
						{
							Open::Type kind = Open::Type::Block;
							if (token.Type == TokenType::If)
								kind = Open::Type::If;
							else if (token.Type == TokenType::Try)
								kind = Open::Type::Try;
							else if (token.Type == TokenType::Identifier && token.Lexeme == "match")
								kind = Open::Type::Match;
							open.push_back({ kind, lineIndex });
						}
						break;
				}
			}
		}
		if (!open.empty())
		{
			result.Error = open.back().Kind == Open::Type::Bracket ? "A bracket is never closed, so the script was left as it is." :
				"A block is missing its 'end', so the script was left as it is.";
			result.ErrorLine = static_cast<int>(open.back().Line) + 1;
			return result;
		}

		// A comment-only line takes the indent of the next code line (or of the line before at the end).
		int following = 0;
		for (size_t i = lines.size(); i > 0; i--)
		{
			if (indents[i - 1] >= 0)
				following = indents[i - 1];
			else if (!lines[i - 1].Keep)
				indents[i - 1] = following;
		}

		// Rebuild the text, keeping the file's line breaks.
		const bool crlf = source.find("\r\n") != std::string_view::npos;
		const char* lineBreak = crlf ? "\r\n" : "\n";
		std::string text;
		int blankRun = 0;
		for (size_t i = 0; i < lines.size(); i++)
		{
			const SourceLine& line = lines[i];
			std::string_view body = source.substr(line.Start, line.End - line.Start);
			if (line.Keep)
			{
				text += body;
				text += lineBreak;
				blankRun = 0;
				continue;
			}
			const size_t firstChar = body.find_first_not_of(" \t");
			if (firstChar == std::string_view::npos)
			{
				if (++blankRun <= 2)
					text += lineBreak;
				continue;
			}
			blankRun = 0;
			body.remove_prefix(firstChar);
			if (!keepEnd[i])
				body = body.substr(0, body.find_last_not_of(" \t") + 1);
			text.append(static_cast<size_t>(indents[i]), '\t');
			text += body;
			text += lineBreak;
		}
		// One line break at the end, none before it.
		while (text.size() >= 2 * std::char_traits<char>::length(lineBreak) &&
			text.compare(text.size() - 2 * std::char_traits<char>::length(lineBreak), 2 * std::char_traits<char>::length(lineBreak),
				std::string(lineBreak) + lineBreak) == 0 &&
			!lines.back().Keep)
			text.resize(text.size() - std::char_traits<char>::length(lineBreak));
		if (text == lineBreak)
			text.clear();

		Lexer check(text);
		if (!SameTokens(tokens, check.Tokenize()))
		{
			result.Error = "Formatting would have changed how the script reads, so it was left as it is. Please report this script.";
			return result;
		}
		result.Changed = text != source;
		result.Text = std::move(text);
		return result;
	}

}
