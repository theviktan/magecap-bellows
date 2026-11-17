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
		std::vector<Token> tokens;
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
		return tokens;
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
				if (!m_InterpolationDepth.empty())
					m_InterpolationDepth.back()++;
				OpenBracket();
				return MakeToken(TokenType::LeftBrace);
			case '}':
				if (!m_InterpolationDepth.empty())
				{
					if (m_InterpolationDepth.back() == 0)
					{
						// This brace closes "{expression}" inside text, so continue reading the text.
						m_InterpolationDepth.pop_back();
						CloseBracket();
						return LexString();
					}
					m_InterpolationDepth.back()--;
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
			case '"': return LexString();
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
			if (IsBlockOpenerWord(token.Type))
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

	bool Lexer::IsBlockOpenerWord(TokenType type) const
	{
		// Member names (list.repeat) and map keys ({if: 1}) are not blocks.
		if (m_PreviousType == TokenType::Dot || m_PreviousType == TokenType::Possessive)
			return false;
		size_t look = m_Current;
		while (look < m_Source.size() && (m_Source[look] == ' ' || m_Source[look] == '\t'))
			look++;
		if (look < m_Source.size() && m_Source[look] == ':')
			return false;

		switch (type)
		{
			// The 'if' of 'else if' continues the open 'if' and shares its 'end'.
			case TokenType::If:
				return m_PreviousType != TokenType::Else && m_PreviousType != TokenType::Otherwise;
			case TokenType::While: case TokenType::Repeat: case TokenType::For:
			case TokenType::Function: case TokenType::Phrase: case TokenType::When: case TokenType::Try:
				return true;
			case TokenType::Until:
				return m_PreviousType != TokenType::Wait;
			case TokenType::Kind:
				return !(m_PreviousType == TokenType::Identifier && (m_PreviousLexeme == "a" || m_PreviousLexeme == "an"));
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

	Token Lexer::LexString()
	{
		std::string text;
		while (!IsAtEnd())
		{
			char c = Advance();
			if (c == '"')
			{
				Token token = MakeToken(TokenType::String);
				token.Text = std::move(text);
				return token;
			}
			if (c == '{')
			{
				m_InterpolationDepth.push_back(0);
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
