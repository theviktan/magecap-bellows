#include "TestFramework.h"

#include "Forge/Compiler/Lexer.h"
#include "Forge/Compiler/Phrase.h"

#include <algorithm>

using namespace Forge;

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

FORGE_TEST(LexerKeywordsAndNames)
{
	std::vector<TokenType> types = Types("set health to 10");
	std::vector<TokenType> expected = { TokenType::Set, TokenType::Identifier, TokenType::To, TokenType::Number, TokenType::Newline, TokenType::Eof };
	CHECK(types == expected);
}

FORGE_TEST(LexerNumbers)
{
	Lexer lexer("12 3.5 1_000 0xff 2e3");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK_EQ(tokens[0].Number, 12.0);
	CHECK_EQ(tokens[1].Number, 3.5);
	CHECK_EQ(tokens[2].Number, 1000.0);
	CHECK_EQ(tokens[3].Number, 255.0);
	CHECK_EQ(tokens[4].Number, 2000.0);
}

FORGE_TEST(LexerTextAndInterpolation)
{
	Lexer lexer("\"Hi {name}!\"");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK(tokens[0].Type == TokenType::Interpolation);
	CHECK_EQ(tokens[0].Text, std::string("Hi "));
	CHECK(tokens[1].Type == TokenType::Identifier);
	CHECK(tokens[2].Type == TokenType::String);
	CHECK_EQ(tokens[2].Text, std::string("!"));
}

FORGE_TEST(LexerPossessive)
{
	std::vector<TokenType> types = Types("player's health");
	CHECK(types[0] == TokenType::Identifier);
	CHECK(types[1] == TokenType::Possessive);
	CHECK(types[2] == TokenType::Identifier);
}

FORGE_TEST(LexerNewlinesInsideBracketsAreIgnored)
{
	std::vector<TokenType> types = Types("[1,\n2,\n3]");
	CHECK_EQ(std::count(types.begin(), types.end(), TokenType::Newline), 1);
	CHECK(types[types.size() - 2] == TokenType::Newline);
}

FORGE_TEST(LexerNewlinesInsideFunctionsInsideBrackets)
{
	// list.map(function(x) <newline> return x <newline> end)
	std::vector<TokenType> types = Types("f(function(x)\nsay x\nreturn {a:\n1}\nend)");
	CHECK_EQ(std::count(types.begin(), types.end(), TokenType::Newline), 4);
}

FORGE_TEST(LexerCommentsAndErrors)
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

FORGE_TEST(LexerLineNumbers)
{
	Lexer lexer("a\n\nb");
	std::vector<Token> tokens = lexer.Tokenize();
	CHECK_EQ(tokens[0].Line, 1);
	CHECK_EQ(tokens[2].Line, 3);
}

FORGE_TEST(PhrasePatterns)
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
