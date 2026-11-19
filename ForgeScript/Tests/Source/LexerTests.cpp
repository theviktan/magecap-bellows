#include "TestFramework.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Compiler/Phrase.h"

#include <algorithm>

using namespace ForgeScript;

namespace {

	std::vector<TokenType> Types(const std::string& source)
	{
		Lexer lexer(source);
		std::vector<TokenType> types;
		for (const Token& token : lexer.Tokenize())
			types.push_back(token.Type);
		return types;
	}

}

TEST_CASE("LexerKeywordsAndNames")
{
	std::vector<TokenType> types = Types("set health to 10");
	std::vector<TokenType> expected = { TokenType::Set, TokenType::Identifier, TokenType::To, TokenType::Number, TokenType::Newline, TokenType::Eof };
	CHECK(types == expected);
}

TEST_CASE("LexerNumbers")
{
	Lexer lexer("12 3.5 1_000 0xff 2e3");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK_EQ(tokens[0].Number, 12.0);
	CHECK_EQ(tokens[1].Number, 3.5);
	CHECK_EQ(tokens[2].Number, 1000.0);
	CHECK_EQ(tokens[3].Number, 255.0);
	CHECK_EQ(tokens[4].Number, 2000.0);
}

TEST_CASE("LexerTextAndInterpolation")
{
	Lexer lexer("\"Hi {name}!\"");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK(tokens[0].Type == TokenType::Interpolation);
	CHECK_EQ(tokens[0].Text, std::string("Hi "));
	CHECK(tokens[1].Type == TokenType::Identifier);
	CHECK(tokens[2].Type == TokenType::String);
	CHECK_EQ(tokens[2].Text, std::string("!"));
}

TEST_CASE("LexerPossessive")
{
	std::vector<TokenType> types = Types("player's health");
	CHECK(types[0] == TokenType::Identifier);
	CHECK(types[1] == TokenType::Possessive);
	CHECK(types[2] == TokenType::Identifier);
}

TEST_CASE("LexerNewlinesInsideBracketsAreIgnored")
{
	std::vector<TokenType> types = Types("[1,\n2,\n3]");
	CHECK_EQ(std::count(types.begin(), types.end(), TokenType::Newline), 1);
	CHECK(types[types.size() - 2] == TokenType::Newline);
}

TEST_CASE("LexerNewlinesInsideFunctionsInsideBrackets")
{
	// list.map(function(x) <newline> return x <newline> end)
	std::vector<TokenType> types = Types("f(function(x)\nsay x\nreturn {a:\n1}\nend)");
	CHECK_EQ(std::count(types.begin(), types.end(), TokenType::Newline), 4);
}

TEST_CASE("LexerCommentsAndErrors")
{
	std::vector<TokenType> types = Types("# a comment\n#[ block\ncomment ]# say 1");
	CHECK(types[0] == TokenType::Identifier);

	Lexer bad("'single quotes'");
	std::vector<Token> tokens = bad.Tokenize();
	CHECK(tokens[0].Type == TokenType::Error);
	CHECK_CONTAINS(tokens[0].Text, "double quotes");

	Lexer unterminated("\"no end");
	CHECK(unterminated.Tokenize()[0].Type == TokenType::Error);
}

TEST_CASE("LexerLineNumbers")
{
	Lexer lexer("a\n\nb");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK_EQ(tokens[0].Line, 1);
	CHECK_EQ(tokens[2].Line, 3);
}

TEST_CASE("PhrasePatterns")
{
	PhrasePattern pattern;
	std::string error;
	CHECK(ParsePhrasePattern("move (thing) toward (target) by (amount)", pattern, error));
	CHECK_EQ(pattern.MangledName(), std::string("move _ toward _ by _"));
	CHECK_EQ(pattern.SlotCount(), static_cast<size_t>(3));
	CHECK_EQ(pattern.SlotNames[1], std::string("target"));

	CHECK(!ParsePhrasePattern("(x) first", pattern, error));
	CHECK(!ParsePhrasePattern("move (a) (b)", pattern, error));
	CHECK(!ParsePhrasePattern("move (a", pattern, error));

	PhraseTable table;
	CHECK(ParsePhrasePattern("say (message)", pattern, error));
	CHECK(table.Add(pattern));
	CHECK(!table.Add(pattern));
	CHECK(table.IsHeadWord("say"));
	CHECK(!table.IsHeadWord("move"));
}

TEST_CASE("LexerInlineIfInsideBracketsHasNoEnd")
{
	// The 'if' picks a value, so the newline after the list item ends nothing and is skipped,
	// while a real 'if' block inside a function in brackets keeps its newlines.
	std::vector<TokenType> picked = Types("say [1 if yes otherwise 2,\n3]");
	CHECK(std::count(picked.begin(), picked.end(), TokenType::Newline) == 1);
	std::vector<TokenType> block = Types("say [function(x) if x then\nreturn 1\nend end]");
	CHECK(std::count(block.begin(), block.end(), TokenType::Newline) == 3);
}

TEST_CASE("LexerLongText")
{
	Lexer lexer("set a to \"\"\"\n\tone \"two\"\n\t  three\n\t\"\"\"\nsay a\n");
	std::vector<Token> tokens = lexer.Tokenize();
	REQUIRE(tokens.size() >= 6);
	CHECK(tokens[3].Type == TokenType::String);
	CHECK_EQ(tokens[3].Text, std::string("one \"two\"\n  three"));
	// Lines after the text are counted right.
	CHECK_EQ(std::string(tokens[5].Lexeme), std::string("say"));
	CHECK_EQ(tokens[5].Line, 5);

	// "{ }" inside long text splits it like other text, and the indentation goes from every part.
	Lexer parts("say \"\"\"\n    a {1} b\n    c {2}\n    \"\"\"\n");
	std::vector<Token> partTokens = parts.Tokenize();
	REQUIRE(partTokens.size() >= 6);
	CHECK(partTokens[1].Type == TokenType::Interpolation);
	CHECK_EQ(partTokens[1].Text, std::string("a "));
	CHECK(partTokens[3].Type == TokenType::Interpolation);
	CHECK_EQ(partTokens[3].Text, std::string(" b\nc "));
	CHECK(partTokens[5].Type == TokenType::String);
	CHECK_EQ(partTokens[5].Text, std::string(""));

	// A missing end is one clear mistake, not a cascade.
	Lexer open("say \"\"\"never closed\nsay 1\n");
	std::vector<Token> openTokens = open.Tokenize();
	REQUIRE(openTokens.size() >= 2);
	CHECK(openTokens[1].Type == TokenType::Error);
	CHECK_CONTAINS(openTokens[1].Text, "missing its closing \"\"\" quotes");
}
