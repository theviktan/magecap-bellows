#include "ForgeScript/Compiler/Lexer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace ForgeScript {

	namespace {

		const std::unordered_map<std::string_view, TokenType>& Keywords()
		{
			static const std::unordered_map<std::string_view, TokenType> s_Keywords = {
				{ "and", TokenType::And },           { "or", TokenType::Or },
				{ "not", TokenType::Not },           { "is", TokenType::Is },
				{ "contains", TokenType::Contains }, { "of", TokenType::Of },
				{ "set", TokenType::Set },           { "to", TokenType::To },
				{ "constant", TokenType::Constant }, { "add", TokenType::Add },
				{ "take", TokenType::Take },         { "from", TokenType::From },
				{ "multiply", TokenType::Multiply }, { "divide", TokenType::Divide },
				{ "by", TokenType::By },             { "if", TokenType::If },
				{ "then", TokenType::Then },         { "else", TokenType::Else },
				{ "otherwise", TokenType::Otherwise }, { "end", TokenType::End },
				{ "while", TokenType::While },       { "until", TokenType::Until },
				{ "do", TokenType::Do },             { "repeat", TokenType::Repeat },
				{ "for", TokenType::For },           { "each", TokenType::Each },
				{ "in", TokenType::In },             { "stop", TokenType::Stop },
				{ "skip", TokenType::Skip },         { "function", TokenType::Function },
				{ "phrase", TokenType::Phrase },     { "return", TokenType::Return },
				{ "kind", TokenType::Kind },         { "extends", TokenType::Extends },
				{ "new", TokenType::New },           { "self", TokenType::Self },
				{ "my", TokenType::My },             { "parent", TokenType::Parent },
				{ "true", TokenType::True },         { "false", TokenType::False },
				{ "yes", TokenType::Yes },           { "no", TokenType::No },
				{ "nothing", TokenType::Nothing },   { "try", TokenType::Try },
				{ "catch", TokenType::Catch },       { "fail", TokenType::Fail },
				{ "when", TokenType::When },         { "with", TokenType::With },
				{ "trigger", TokenType::Trigger },   { "wait", TokenType::Wait },
				{ "start", TokenType::Start },       { "use", TokenType::Use },
			};
			return s_Keywords;
		}

		bool IsWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
		bool IsWordPart(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

		// A line that ends with one of these continues on the next line.
		bool ContinuesLine(TokenType type)
		{
			switch (type)
			{
				case TokenType::Comma: case TokenType::Plus: case TokenType::Minus:
				case TokenType::Star: case TokenType::Slash: case TokenType::Percent:
				case TokenType::Caret: case TokenType::EqualEqual: case TokenType::BangEqual:
				case TokenType::Less: case TokenType::LessEqual: case TokenType::Greater:
				case TokenType::GreaterEqual: case TokenType::Dot: case TokenType::And:
				case TokenType::Or: case TokenType::Not:
					return true;
				default:
					return false;
			}
		}

	}

	bool Token::IsWord() const
	{
		return Type == TokenType::Identifier || (Type >= TokenType::And && Type <= TokenType::Use);
	}

	const char* TokenTypeName(TokenType type)
	{
		switch (type)
		{
			case TokenType::LeftParen:     return "'('";
			case TokenType::RightParen:    return "')'";
			case TokenType::LeftBracket:   return "'['";
			case TokenType::RightBracket:  return "']'";
			case TokenType::LeftBrace:     return "'{'";
			case TokenType::RightBrace:    return "'}'";
			case TokenType::Comma:         return "','";
			case TokenType::Colon:         return "':'";
			case TokenType::Identifier:    return "a name";
			case TokenType::Number:        return "a number";
			case TokenType::String:        return "text";
			case TokenType::Newline:       return "the end of the line";
			case TokenType::Eof:           return "the end of the script";
			case TokenType::End:           return "'end'";
			case TokenType::To:            return "'to'";
			case TokenType::From:          return "'from'";
			case TokenType::By:            return "'by'";
			case TokenType::In:            return "'in'";
			default:                       return "a symbol";
		}
	}

	Lexer::Lexer(std::string_view source)
		: m_Source(source)
	{
	}

	std::vector<Token> Lexer::Tokenize()
	{
		std::vector<Token>& tokens = m_Tokens;
		tokens.clear();
		while (true)
		{
			Token token = NextToken();

			if (token.Type == TokenType::Newline)
			{
				// Collapse blank lines and skip newlines that would split one statement in two.
				if (tokens.empty() || tokens.back().Type == TokenType::Newline || ContinuesLine(tokens.back().Type))
					continue;
			}

			bool done = token.Type == TokenType::Eof;
			if (done && !tokens.empty() && tokens.back().Type != TokenType::Newline)
			{
				Token newline = token;
				newline.Type = TokenType::Newline;
				tokens.push_back(newline);
			}
			m_PreviousType = token.Type;
			m_PreviousLexeme = token.Lexeme;
			tokens.push_back(std::move(token));
			if (done)
				break;
		}
		return std::move(m_Tokens);
	}

	namespace {

		bool EndsValue(const Token& token)
		{
			switch (token.Type)
			{
				case TokenType::Identifier: case TokenType::Number: case TokenType::String:
				case TokenType::RightParen: case TokenType::RightBracket: case TokenType::RightBrace:
				case TokenType::True: case TokenType::False: case TokenType::Yes: case TokenType::No:
				case TokenType::Nothing: case TokenType::Self:
					return true;
				default:
					return false;
			}
		}

		// Text with "{ }" in it is an Interpolation token (the text up to "{"), the expression's tokens,
		// and then a String or another Interpolation for the text after "}". Those later parts start
		// with "}", which is how they are told apart from ordinary text.
		bool StartsAfterInterpolation(const Token& token)
		{
			return !token.Lexeme.empty() && token.Lexeme.front() == '}';
		}

	}

	bool MatchOpensBlock(const std::vector<Token>& tokens, size_t index)
	{
		if (index >= tokens.size() || tokens[index].Type != TokenType::Identifier || tokens[index].Lexeme != "match")
			return false;
		if (index > 0)
		{
			switch (tokens[index - 1].Type)
			{
				case TokenType::Newline: case TokenType::Then: case TokenType::Do:
				case TokenType::Else: case TokenType::Otherwise:
					break;
				default:
					return false;
			}
		}
		// The lexer keeps no newlines inside brackets, so the header ends at the next newline.
		size_t i = index + 1;
		if (i >= tokens.size() || tokens[i].Type == TokenType::Newline || tokens[i].Type == TokenType::Eof)
			return false;
		while (i < tokens.size() && tokens[i].Type != TokenType::Newline && tokens[i].Type != TokenType::Eof)
			i++;
		return i + 1 < tokens.size() && tokens[i].Type == TokenType::Newline && tokens[i + 1].Type == TokenType::Is;
	}

	bool IsTimeUnitWord(std::string_view word)
	{
		return word == "second" || word == "seconds" || word == "ms" || word == "millisecond" || word == "milliseconds" ||
			word == "minute" || word == "minutes" || word == "frame" || word == "frames";
	}

	bool TestOpensBlock(const std::vector<Token>& tokens, size_t index)
	{
		if (index + 2 >= tokens.size() || tokens[index].Type != TokenType::Identifier || tokens[index].Lexeme != "test" ||
			tokens[index + 1].Type != TokenType::String)
			return false;
		if (index > 0 && tokens[index - 1].Type != TokenType::Newline)
			return false;
		const TokenType after = tokens[index + 2].Type;
		return after == TokenType::Newline || after == TokenType::Do || after == TokenType::Eof;
	}

	bool TimerOpensBlock(const std::vector<Token>& tokens, size_t index)
	{
		if (index >= tokens.size() || tokens[index].Type != TokenType::Identifier ||
			(tokens[index].Lexeme != "every" && tokens[index].Lexeme != "after"))
			return false;
		if (index > 0)
		{
			switch (tokens[index - 1].Type)
			{
				case TokenType::Newline: case TokenType::Then: case TokenType::Do:
				case TokenType::Else: case TokenType::Otherwise:
					break;
				default:
					return false;
			}
		}
		// A value must follow: not a member, an assignment's 'to', a comparison or the end of the line.
		size_t i = index + 1;
		if (i >= tokens.size())
			return false;
		switch (tokens[i].Type)
		{
			case TokenType::Newline: case TokenType::Eof: case TokenType::Dot: case TokenType::Possessive:
			case TokenType::Colon: case TokenType::To: case TokenType::Is:
				return false;
			default:
				break;
		}
		// The lexer keeps no newlines inside brackets, so the header ends at the next newline.
		while (i < tokens.size() && tokens[i].Type != TokenType::Newline && tokens[i].Type != TokenType::Eof)
			i++;
		const Token& last = tokens[i - 1];
		return i - 1 > index + 1 && last.Type == TokenType::Identifier && IsTimeUnitWord(last.Lexeme);
	}

	bool IfPicksAValue(const std::vector<Token>& tokens, size_t index)
	{
		if (index == 0 || index > tokens.size() || !EndsValue(tokens[index - 1]))
			return false;

		// Walk back over the current statement, skipping anything inside brackets, to find what it is.
		int depth = 0;
		for (size_t k = index; k > 0; k--)
		{
			const Token& token = tokens[k - 1];
			switch (token.Type)
			{
				case TokenType::RightParen: case TokenType::RightBracket: case TokenType::RightBrace:
					depth++;
					continue;
				case TokenType::String:
					if (StartsAfterInterpolation(token))
						depth++;
					continue;
				case TokenType::Interpolation:
					// "}text{" closes one "{ }" and opens the next, so it leaves the depth unchanged,
					// unless the 'if' is inside the "{ }" that it opens.
					if (StartsAfterInterpolation(token))
					{
						if (depth == 0)
							return true;
						continue;
					}
					[[fallthrough]];
				case TokenType::LeftParen: case TokenType::LeftBracket: case TokenType::LeftBrace:
					if (depth == 0)
						return true; // Inside brackets or "{ }": always a value.
					depth--;
					// 'function name(...)' or 'function(...)': the 'if' follows a function's parameters.
					if (depth == 0 && token.Type == TokenType::LeftParen && k >= 2)
					{
						const Token& before = tokens[k - 2];
						if (before.Type == TokenType::Function ||
							(before.Type == TokenType::Identifier && k >= 3 && tokens[k - 3].Type == TokenType::Function))
							return false;
					}
					continue;
				default:
					break;
			}
			if (depth > 0)
				continue;
			switch (token.Type)
			{
				// The start of a statement: what came after it is not a block's header.
				case TokenType::Newline: case TokenType::Then: case TokenType::Do:
				case TokenType::Else: case TokenType::Otherwise: case TokenType::Return:
					return true;
				case TokenType::For: case TokenType::While: case TokenType::Repeat: case TokenType::If:
				case TokenType::When: case TokenType::Catch: case TokenType::Phrase:
					return false;
				case TokenType::Until:
					if (k >= 2 && tokens[k - 2].Type == TokenType::Wait)
						continue;
					return false;
				default:
					continue;
			}
		}
		return true;
	}

	std::vector<std::string> Lexer::GetKeywords()
	{
		std::vector<std::string> words;
		for (const auto& [word, type] : Keywords())
			words.emplace_back(word);
		std::sort(words.begin(), words.end());
		return words;
	}

	char Lexer::Advance()
	{
		char c = m_Source[m_Current++];
		if (c == '\n')
		{
			m_Line++;
			m_Column = 1;
		}
		else
		{
			m_Column++;
		}
		return c;
	}

	bool Lexer::Match(char expected)
	{
		if (IsAtEnd() || m_Source[m_Current] != expected)
			return false;
		Advance();
		return true;
	}

	Token Lexer::MakeToken(TokenType type) const
	{
		Token token;
		token.Type = type;
		token.Lexeme = m_Source.substr(m_Start, m_Current - m_Start);
		token.Line = m_StartLine;
		token.Column = m_StartColumn;
		return token;
	}

	Token Lexer::MakeError(const std::string& message) const
	{
		Token token = MakeToken(TokenType::Error);
		token.Text = message;
		return token;
	}

	void Lexer::SkipWhitespaceAndComments()
	{
		while (!IsAtEnd())
		{
			char c = Peek();
			if (c == ' ' || c == '\t' || c == '\r')
			{
				Advance();
			}
			else if (c == '\n' && !NewlinesMatter())
			{
				// Inside ( ), [ ] or { } a new line never ends the statement.
				Advance();
			}
			else if (c == '\\' && (Peek(1) == '\n' || (Peek(1) == '\r' && Peek(2) == '\n')))
			{
				// A backslash at the end of a line joins it with the next line.
				Advance();
				if (Peek() == '\r')
					Advance();
				Advance();
			}
			else if (c == '#')
			{
				if (Peek(1) == '[')
				{
					// Block comment:  #[ ... ]#
					Advance();
					Advance();
					while (!IsAtEnd() && !(Peek() == ']' && Peek(1) == '#'))
						Advance();
					if (!IsAtEnd())
					{
						Advance();
						Advance();
					}
				}
				else
				{
					while (!IsAtEnd() && Peek() != '\n')
						Advance();
				}
			}
			else
			{
				return;
			}
		}
	}

	Token Lexer::NextToken()
	{
		SkipWhitespaceAndComments();

		m_Start = m_Current;
		m_StartLine = m_Line;
		m_StartColumn = m_Column;

		if (IsAtEnd())
			return MakeToken(TokenType::Eof);

		char c = Advance();

		if (IsWordStart(c))
			return LexWord();
		if (std::isdigit(static_cast<unsigned char>(c)))
			return LexNumber();

		switch (c)
		{
			case '\n': return MakeToken(TokenType::Newline);
			case '(': OpenBracket(); return MakeToken(TokenType::LeftParen);
			case ')': CloseBracket(); return MakeToken(TokenType::RightParen);
			case '[': OpenBracket(); return MakeToken(TokenType::LeftBracket);
			case ']': CloseBracket(); return MakeToken(TokenType::RightBracket);
			case '{':
				if (!m_Interpolations.empty())
					m_Interpolations.back().Depth++;
				OpenBracket();
				return MakeToken(TokenType::LeftBrace);
			case '}':
				if (!m_Interpolations.empty())
				{
					if (m_Interpolations.back().Depth == 0)
					{
						// This brace closes "{expression}" inside text, so continue reading the text.
						const std::optional<LongText> longText = std::move(m_Interpolations.back().Long);
						m_Interpolations.pop_back();
						CloseBracket();
						return LexString(longText ? &*longText : nullptr);
					}
					m_Interpolations.back().Depth--;
				}
				CloseBracket();
				return MakeToken(TokenType::RightBrace);
			case ',': return MakeToken(TokenType::Comma);
			case '.': return MakeToken(TokenType::Dot);
			case ':': return MakeToken(TokenType::Colon);
			case '+': return MakeToken(TokenType::Plus);
			case '-': return MakeToken(TokenType::Minus);
			case '*': return MakeToken(TokenType::Star);
			case '/': return MakeToken(TokenType::Slash);
			case '%': return MakeToken(TokenType::Percent);
			case '^': return MakeToken(TokenType::Caret);
			case '=': return MakeToken(Match('=') ? TokenType::EqualEqual : TokenType::Equal);
			case '!':
				if (Match('='))
					return MakeToken(TokenType::BangEqual);
				return MakeError("'!' is not used in ForgeScript. Use 'not' instead, or '!=' for 'is not'.");
			case '<': return MakeToken(Match('=') ? TokenType::LessEqual : TokenType::Less);
			case '>': return MakeToken(Match('=') ? TokenType::GreaterEqual : TokenType::Greater);
			case '"':
				if (Peek() == '"' && Peek(1) == '"')
				{
					Advance();
					Advance();
					return LexLongText();
				}
				return LexString();
			case '\'':
				if ((Peek() == 's' || Peek() == 'S') && !IsWordPart(Peek(1)))
				{
					Advance();
					return MakeToken(TokenType::Possessive);
				}
				return MakeError("Text in ForgeScript uses double quotes, like \"hello\".");
			default:
				break;
		}

		if (c == '&')
			return MakeError("Unexpected character '&'. ForgeScript writes 'and', like: if x > 0 and x < 5 then");
		if (c == '|')
			return MakeError("Unexpected character '|'. ForgeScript writes 'or', like: if x is 0 or y is 0 then");
		return MakeError(std::string("Unexpected character '") + c + "'.");
	}

	Token Lexer::LexWord()
	{
		while (IsWordPart(Peek()))
			Advance();

		Token token = MakeToken(TokenType::Identifier);
		auto it = Keywords().find(token.Lexeme);
		if (it != Keywords().end())
			token.Type = it->second;

		// Track blocks that are written inside brackets, such as  list.map(function(x) ... end)
		if (!m_Nesting.empty())
		{
			if (IsBlockOpenerWord(token.Type, token.Lexeme))
				m_Nesting.push_back(Nesting::Block);
			else if (token.Type == TokenType::End && m_PreviousType != TokenType::Dot &&
				m_PreviousType != TokenType::Possessive && m_Nesting.back() == Nesting::Block)
				m_Nesting.pop_back();
		}
		return token;
	}

	void Lexer::CloseBracket()
	{
		// Drop any blocks left open inside the bracket (a mistake the compiler will report).
		while (!m_Nesting.empty() && m_Nesting.back() == Nesting::Block)
			m_Nesting.pop_back();
		if (!m_Nesting.empty())
			m_Nesting.pop_back();
	}

	bool Lexer::RestOfLineIsTimer() const
	{
		// The same test as TimerOpensBlock, on the text: a value, then a time word at the end of the line.
		size_t end = m_Current;
		while (end < m_Source.size() && m_Source[end] != '\n' && m_Source[end] != '\r' && m_Source[end] != '#')
			end++;
		std::string_view rest(m_Source.data() + m_Current, end - m_Current);
		while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
			rest.remove_suffix(1);
		size_t wordStart = rest.size();
		while (wordStart > 0 && IsWordPart(rest[wordStart - 1]))
			wordStart--;
		std::string_view before = rest.substr(0, wordStart);
		const bool somethingBefore = before.find_first_not_of(" \t") != std::string_view::npos;
		return somethingBefore && IsTimeUnitWord(rest.substr(wordStart));
	}

	bool Lexer::IsBlockOpenerWord(TokenType type, std::string_view lexeme) const
	{
		// Member names (list.repeat) and map keys ({if: 1}) are not blocks.
		if (m_PreviousType == TokenType::Dot || m_PreviousType == TokenType::Possessive)
			return false;
		size_t look = m_Current;
		while (look < m_Source.size() && (m_Source[look] == ' ' || m_Source[look] == '\t'))
			look++;
		if (look < m_Source.size() && m_Source[look] == ':')
			return false;

		// A parameter's type ('f as function', 'k as kind') names a type; it opens nothing.
		if ((type == TokenType::Function || type == TokenType::Kind) && m_PreviousType == TokenType::Identifier && m_PreviousLexeme == "as")
			return false;

		switch (type)
		{
			// The 'if' of 'else if' continues the open 'if' and shares its 'end', and an 'if' that picks
			// one of two values has no 'end'.
			case TokenType::If:
				return m_PreviousType != TokenType::Else && m_PreviousType != TokenType::Otherwise &&
					!IfPicksAValue(m_Tokens, m_Tokens.size());
			case TokenType::While: case TokenType::Repeat:
			case TokenType::Phrase: case TokenType::When: case TokenType::Try:
				return true;
			// 'wait until' and 'wait for' are waits, not loops.
			case TokenType::Until: case TokenType::For:
				return m_PreviousType != TokenType::Wait;
			// 'x is a function' and 'x is a kind' test a value's type; they open nothing.
			case TokenType::Function: case TokenType::Kind:
				return !(m_PreviousType == TokenType::Identifier && (m_PreviousLexeme == "a" || m_PreviousLexeme == "an"));
			// A timer written inside an anonymous function within brackets: its 'end' is its own.
			case TokenType::Identifier:
				switch (m_PreviousType)
				{
					case TokenType::Newline: case TokenType::Then: case TokenType::Do:
					case TokenType::Else: case TokenType::Otherwise:
						return (lexeme == "every" || lexeme == "after") && RestOfLineIsTimer();
					default:
						return false;
				}
			default:
				return false;
		}
	}

	Token Lexer::LexNumber()
	{
		std::string digits;
		digits += m_Source[m_Start];

		if (m_Source[m_Start] == '0' && (Peek() == 'x' || Peek() == 'X'))
		{
			Advance();
			std::string hex;
			while (std::isxdigit(static_cast<unsigned char>(Peek())) || Peek() == '_')
			{
				char h = Advance();
				if (h != '_')
					hex += h;
			}
			if (hex.empty())
				return MakeError("A hex number needs digits after '0x'.");
			Token token = MakeToken(TokenType::Number);
			token.Number = static_cast<double>(std::strtoull(hex.c_str(), nullptr, 16));
			return token;
		}

		auto readDigits = [&]()
		{
			while (std::isdigit(static_cast<unsigned char>(Peek())) || (Peek() == '_' && std::isdigit(static_cast<unsigned char>(Peek(1)))))
			{
				char d = Advance();
				if (d != '_')
					digits += d;
			}
		};

		readDigits();
		if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(Peek(1))))
		{
			digits += Advance();
			readDigits();
		}
		if ((Peek() == 'e' || Peek() == 'E') && (std::isdigit(static_cast<unsigned char>(Peek(1))) ||
			((Peek(1) == '-' || Peek(1) == '+') && std::isdigit(static_cast<unsigned char>(Peek(2))))))
		{
			digits += Advance();
			if (Peek() == '-' || Peek() == '+')
				digits += Advance();
			readDigits();
		}

		Token token = MakeToken(TokenType::Number);
		token.Number = std::strtod(digits.c_str(), nullptr);
		return token;
	}

	// """long text""" can hold " and "" without escapes and runs until the next """ (the last three of a
	// longer run of quotes, so text may end with a quote). When the opening """ ends its line, that line
	// break isn't part of the text, and neither is the line holding only the closing """. The spaces and
	// tabs that every line starts with (the indentation from the code around it) are removed, and line
	// ends are always a plain new line ("\r\n" becomes "\n"). Escapes and "{ }" work as in other text.
	Token Lexer::LexLongText()
	{
		const size_t open = m_Current;
		std::vector<size_t> lineStarts;
		const size_t close = FindLongTextClose(open, &lineStarts);
		if (close == std::string_view::npos)
		{
			while (!IsAtEnd())
				Advance();
			return MakeError("This long text is missing its closing \"\"\" quotes.");
		}

		auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
		size_t contentStart = open;
		while (contentStart < close && isSpace(m_Source[contentStart]))
			contentStart++;
		const bool startsOnNewLine = contentStart < close && m_Source[contentStart] == '\n';
		contentStart = startsOnNewLine ? contentStart + 1 : open;

		size_t contentEnd = close;
		while (contentEnd > contentStart && isSpace(m_Source[contentEnd - 1]))
			contentEnd--;
		if (contentEnd > contentStart && m_Source[contentEnd - 1] == '\n')
		{
			contentEnd--;
			if (contentEnd > contentStart && m_Source[contentEnd - 1] == '\r')
				contentEnd--; // Windows line ends
		}
		else if (contentEnd == contentStart && startsOnNewLine)
			contentEnd = contentStart;
		else
			contentEnd = close;

		// The indentation every line with words on it shares. The first line counts only when it starts
		// on its own line, after the opening quotes.
		if (startsOnNewLine)
			lineStarts.insert(lineStarts.begin(), contentStart);
		LongText longText;
		longText.ContentEnd = contentEnd;
		longText.CloseEnd = close + 3;
		bool first = true;
		for (size_t start : lineStarts)
		{
			if (start >= contentEnd)
				break;
			size_t end = start;
			while (end < contentEnd && (m_Source[end] == ' ' || m_Source[end] == '\t'))
				end++;
			if (end >= contentEnd || m_Source[end] == '\n' || m_Source[end] == '\r')
				continue; // A blank line says nothing about the indentation.
			const std::string_view indent = m_Source.substr(start, end - start);
			if (first)
				longText.Indent = std::string(indent);
			else
			{
				size_t shared = 0;
				while (shared < longText.Indent.size() && shared < indent.size() && longText.Indent[shared] == indent[shared])
					shared++;
				longText.Indent.resize(shared);
			}
			first = false;
		}

		while (m_Current < contentStart)
			Advance();
		if (startsOnNewLine)
		{
			for (size_t i = 0; i < longText.Indent.size() && m_Current < contentEnd && Peek() == longText.Indent[i]; i++)
				Advance();
		}
		return LexString(&longText);
	}

	// The position of the closing """ for long text whose words start at 'position', or npos. Notes
	// where each line starts (outside any "{ }"), for the indentation.
	size_t Lexer::FindLongTextClose(size_t position, std::vector<size_t>* lineStarts) const
	{
		while (position < m_Source.size())
		{
			const char c = m_Source[position];
			if (c == '\\')
			{
				position += 2;
				continue;
			}
			if (c == '{')
			{
				position = SkipCode(position + 1);
				if (position == std::string_view::npos)
					return position;
				continue;
			}
			if (c == '"')
			{
				size_t run = 0;
				while (position + run < m_Source.size() && m_Source[position + run] == '"')
					run++;
				if (run >= 3)
					return position + run - 3;
				position += run;
				continue;
			}
			if (c == '\n' && lineStarts)
				lineStarts->push_back(position + 1);
			position++;
		}
		return std::string_view::npos;
	}

	// Just after the closing " of text whose words start at 'position', or npos.
	size_t Lexer::SkipShortText(size_t position) const
	{
		while (position < m_Source.size())
		{
			const char c = m_Source[position];
			if (c == '\\')
				position += 2;
			else if (c == '{')
			{
				position = SkipCode(position + 1);
				if (position == std::string_view::npos)
					return position;
			}
			else if (c == '"')
				return position + 1;
			else
				position++;
		}
		return std::string_view::npos;
	}

	// Just after the '}' that closes the "{ }" whose code starts at 'position', or npos.
	size_t Lexer::SkipCode(size_t position) const
	{
		int depth = 0;
		while (position < m_Source.size())
		{
			const char c = m_Source[position];
			if (c == '{')
				depth++;
			else if (c == '}')
			{
				if (depth == 0)
					return position + 1;
				depth--;
			}
			else if (c == '"')
			{
				if (position + 2 < m_Source.size() && m_Source[position + 1] == '"' && m_Source[position + 2] == '"')
				{
					const size_t close = FindLongTextClose(position + 3, nullptr);
					if (close == std::string_view::npos)
						return close;
					position = close + 3;
				}
				else
				{
					position = SkipShortText(position + 1);
					if (position == std::string_view::npos)
						return position;
				}
				continue;
			}
			else if (c == '#')
			{
				// Comments, read the way SkipWhitespaceAndComments reads them.
				if (position + 1 < m_Source.size() && m_Source[position + 1] == '[')
				{
					const size_t end = m_Source.find("]#", position + 2);
					position = end == std::string_view::npos ? m_Source.size() : end + 2;
				}
				else
				{
					while (position < m_Source.size() && m_Source[position] != '\n')
						position++;
				}
				continue;
			}
			position++;
		}
		return std::string_view::npos;
	}

	Token Lexer::LexString(const LongText* longText)
	{
		std::string text;
		while (!IsAtEnd())
		{
			if (longText && m_Current >= longText->ContentEnd)
			{
				while (m_Current < longText->CloseEnd)
					Advance();
				Token token = MakeToken(TokenType::String);
				token.Text = std::move(text);
				return token;
			}
			char c = Advance();
			// Long text ends every line with a plain new line, also in files saved with Windows line ends.
			if (longText && c == '\r' && Peek() == '\n' && m_Current < longText->ContentEnd)
				continue;
			if (longText && c == '\n')
			{
				text += c;
				for (size_t i = 0; i < longText->Indent.size() && m_Current < longText->ContentEnd && Peek() == longText->Indent[i]; i++)
					Advance();
				continue;
			}
			if (c == '"' && !longText)
			{
				Token token = MakeToken(TokenType::String);
				token.Text = std::move(text);
				return token;
			}
			if (c == '{')
			{
				m_Interpolations.push_back({ 0, longText ? std::optional<LongText>(*longText) : std::nullopt });
				OpenBracket();
				Token token = MakeToken(TokenType::Interpolation);
				token.Text = std::move(text);
				return token;
			}
			if (c == '\\')
			{
				if (IsAtEnd())
					break;
				char e = Advance();
				switch (e)
				{
					case 'n':  text += '\n'; break;
					case 't':  text += '\t'; break;
					case 'r':  text += '\r'; break;
					case '0':  text += '\0'; break;
					case '"':  text += '"'; break;
					case '\\': text += '\\'; break;
					case '{':  text += '{'; break;
					case '}':  text += '}'; break;
					default:
						return MakeError(std::string("Unknown escape '\\") + e + "' in text.");
				}
				continue;
			}
			text += c;
		}
		return MakeError("This text is missing its closing \" quote.");
	}

}
