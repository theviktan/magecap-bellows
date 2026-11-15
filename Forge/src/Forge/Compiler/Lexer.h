#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Forge {

	enum class TokenType
	{
		// Symbols
		LeftParen, RightParen, LeftBracket, RightBracket, LeftBrace, RightBrace,
		Comma, Dot, Colon, Plus, Minus, Star, Slash, Percent, Caret,
		Equal, EqualEqual, BangEqual, Less, LessEqual, Greater, GreaterEqual,
		Possessive, // 's  as in  player's health

		// Literals
		Identifier, Number, String, Interpolation,

		// Keywords (reserved words)
		And, Or, Not, Is, Contains, Of,
		Set, To, Constant, Add, Take, From, Multiply, Divide, By,
		If, Then, Else, Otherwise, End,
		While, Until, Do, Repeat, For, Each, In, Stop, Skip,
		Function, Phrase, Return, Kind, Extends, New, Self, My, Parent,
		True, False, Yes, No, Nothing,
		Try, Catch, Fail, When, With, Trigger, Wait, Start, Use,

		Newline, Eof, Error
	};

	struct Token
	{
		TokenType Type = TokenType::Eof;
		std::string_view Lexeme;
		std::string Text;   // Processed contents for strings, or the error message for errors.
		double Number = 0.0;
		int Line = 1;
		int Column = 1;

		bool IsWord() const;
	};

	const char* TokenTypeName(TokenType type);

	// Turns Forge source code into a list of tokens.
	// The whole file is tokenized up front so the compiler can look ahead (used for phrases).
	class Lexer
	{
	public:
		explicit Lexer(std::string_view source);

		std::vector<Token> Tokenize();

	private:
		Token NextToken();
		Token MakeToken(TokenType type) const;
		Token MakeError(const std::string& message) const;
		Token LexString();
		Token LexNumber();
		Token LexWord();

		void SkipWhitespaceAndComments();
		bool IsAtEnd() const { return m_Current >= m_Source.size(); }
		char Peek(size_t ahead = 0) const { return m_Current + ahead < m_Source.size() ? m_Source[m_Current + ahead] : '\0'; }
		char Advance();
		bool Match(char expected);

	private:
		std::string_view m_Source;
		size_t m_Start = 0;
		size_t m_Current = 0;
		int m_Line = 1;
		int m_Column = 1;
		int m_StartLine = 1;
		int m_StartColumn = 1;

		bool NewlinesMatter() const { return m_Nesting.empty() || m_Nesting.back() == Nesting::Block; }
		void OpenBracket() { m_Nesting.push_back(Nesting::Bracket); }
		void CloseBracket();
		bool IsBlockOpenerWord(TokenType type) const;

		// Each entry is the bracket depth inside one open "{ }" interpolation.
		std::vector<int> m_InterpolationDepth;

		// Inside ( ), [ ] and { } new lines do not end statements, except inside a
		// "function ... end" written within the brackets, where they do.
		enum class Nesting : uint8_t { Bracket, Block };
		std::vector<Nesting> m_Nesting;
		TokenType m_PreviousType = TokenType::Newline;
		std::string_view m_PreviousLexeme;
	};

}
