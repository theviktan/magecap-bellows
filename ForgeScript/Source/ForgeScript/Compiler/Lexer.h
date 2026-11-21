#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript {

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

	// Whether an 'if' coming right after tokens[0 .. index) picks one of two values
	// ("big" if x > 3 otherwise "small") rather than starting an 'if' block. It does when it follows a
	// value, except on a block's first line before 'then' or 'do' (after the header of for, while,
	// until, repeat, if, when or catch, or a function's parameters): there an 'if' starts the block's
	// first statement, as it always has. The compiler, the lexer and the tools all use this one rule,
	// so they agree on where blocks end.
	bool IfPicksAValue(const std::vector<Token>& tokens, size_t index);

	// Whether the word 'match' at tokens[index] starts a match block ("match weapon", then lines starting
	// with 'is'). 'match' is not a keyword: it only opens a block at the start of a statement, with a value
	// after it, when the next line starts with 'is'. No line could start with 'is' before, so any script
	// that names something 'match' keeps its meaning. The compiler and the tools share this rule.
	bool MatchOpensBlock(const std::vector<Token>& tokens, size_t index);

	// Whether the word 'every' or 'after' at tokens[index] starts a timer block ("every 3 seconds",
	// "after 2 seconds", then the body and 'end'). Neither word is a keyword: it only opens a block at the
	// start of a statement, with a value after it, when the line ends in a time word (second, seconds,
	// ms, millisecond, milliseconds, minute, minutes, frame, frames). Such a line was always a mistake, so
	// every script keeps its meaning. The compiler and the tools share this rule; the compiler also lets
	// a script's own phrase starting with 'every' or 'after' win.
	bool TimerOpensBlock(const std::vector<Token>& tokens, size_t index);
	// Whether a word is one of the time words that end a timer's first line.
	bool IsTimeUnitWord(std::string_view word);

	// Turns ForgeScript source code into a list of tokens.
	// The whole file is tokenized up front so the compiler can look ahead (used for phrases).
	class Lexer
	{
	public:
		explicit Lexer(std::string_view source);

		std::vector<Token> Tokenize();

		// Every reserved word, sorted (for tools such as the language server).
		static std::vector<std::string> GetKeywords();

	private:
		Token NextToken();
		Token MakeToken(TokenType type) const;
		Token MakeError(const std::string& message) const;
		// Where a """long text""" ends and how much indentation its lines lose, worked out when it opens.
		struct LongText
		{
			size_t ContentEnd = 0; // where its words stop (before a last line that holds only the closing quotes)
			size_t CloseEnd = 0;   // just after the closing """
			std::string Indent;    // the leading spaces and tabs every line shares, removed from each
		};

		Token LexString(const LongText* longText = nullptr);
		Token LexLongText();
		size_t FindLongTextClose(size_t position, std::vector<size_t>* lineStarts) const;
		size_t SkipShortText(size_t position) const;
		size_t SkipCode(size_t position) const;
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
		bool IsBlockOpenerWord(TokenType type, std::string_view lexeme) const;
		// Whether the rest of this source line, after the current word, is a timer's ("every 3 seconds").
		bool RestOfLineIsTimer() const;

		// Each open "{ }" inside text: the bracket depth inside it, and the long text it belongs to, if any.
		struct Interpolation
		{
			int Depth = 0;
			std::optional<LongText> Long;
		};
		std::vector<Interpolation> m_Interpolations;

		// Inside ( ), [ ] and { } new lines do not end statements, except inside a
		// "function ... end" written within the brackets, where they do.
		enum class Nesting : uint8_t { Bracket, Block };
		std::vector<Nesting> m_Nesting;
		// The tokens so far, which IfPicksAValue looks back over.
		std::vector<Token> m_Tokens;
		TokenType m_PreviousType = TokenType::Newline;
		std::string_view m_PreviousLexeme;
	};

}
