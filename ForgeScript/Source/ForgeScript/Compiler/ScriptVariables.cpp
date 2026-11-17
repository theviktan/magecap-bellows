#include "ForgeScript/Compiler/ScriptVariables.h"

#include "ForgeScript/Compiler/Lexer.h"

#include <unordered_set>

namespace ForgeScript {

	// Mirrors the compiler's rule for words that open a block closed by 'end'.
	bool TokenOpensBlock(const std::vector<Token>& tokens, size_t i)
	{
		const TokenType previous = i > 0 ? tokens[i - 1].Type : TokenType::Newline;
		if (previous == TokenType::Dot || previous == TokenType::Possessive)
			return false;
		if (i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Colon)
			return false;

		switch (tokens[i].Type)
		{
			case TokenType::If: case TokenType::While: case TokenType::Repeat: case TokenType::For:
			case TokenType::Function: case TokenType::Phrase: case TokenType::When: case TokenType::Try:
				return true;
			case TokenType::Until:
				return previous != TokenType::Wait;
			case TokenType::Kind:
				return !(previous == TokenType::Identifier && (tokens[i - 1].Lexeme == "a" || tokens[i - 1].Lexeme == "an"));
			default:
				return false;
		}
	}

	namespace {

		// Reads an optionally negative number starting at 'i'; advances 'i' past it.
		bool ReadNumber(const std::vector<Token>& tokens, size_t& i, double& outNumber, bool* outWhole = nullptr)
		{
			bool negative = false;
			if (tokens[i].Type == TokenType::Minus)
			{
				negative = true;
				i++;
			}
			if (tokens[i].Type != TokenType::Number)
				return false;
			outNumber = negative ? -tokens[i].Number : tokens[i].Number;
			if (outWhole)
				*outWhole = tokens[i].Lexeme.find_first_of(".eE") == std::string_view::npos || tokens[i].Lexeme.rfind("0x", 0) == 0;
			i++;
			return true;
		}

		// Parses the value after "set name to" when it is a plain value followed by the end of the line.
		bool ReadPlainValue(const std::vector<Token>& tokens, size_t i, ScriptVariable& variable)
		{
			const Token& first = tokens[i];
			switch (first.Type)
			{
				case TokenType::String:
					variable.Kind = ScriptVariable::ValueKind::Text;
					variable.Text = first.Text;
					i++;
					break;
				case TokenType::True: case TokenType::Yes: case TokenType::False: case TokenType::No:
					variable.Kind = ScriptVariable::ValueKind::Boolean;
					variable.Boolean = first.Type == TokenType::True || first.Type == TokenType::Yes;
					i++;
					break;
				case TokenType::Number: case TokenType::Minus:
				{
					bool whole = false;
					if (!ReadNumber(tokens, i, variable.Number, &whole))
						return false;
					variable.Kind = whole ? ScriptVariable::ValueKind::WholeNumber : ScriptVariable::ValueKind::Number;
					break;
				}
				case TokenType::Identifier:
				case TokenType::LeftBracket:
				{
					// vector(x, y[, z])   or   [r, g, b[, a]]
					const bool isVector = first.Type == TokenType::Identifier;
					if (isVector && (first.Lexeme != "vector" || tokens[i + 1].Type != TokenType::LeftParen))
						return false;
					i += isVector ? 2 : 1;
					const TokenType close = isVector ? TokenType::RightParen : TokenType::RightBracket;
					size_t count = 0;
					while (count < 4)
					{
						if (!ReadNumber(tokens, i, variable.Components[count]))
							return false;
						count++;
						if (tokens[i].Type != TokenType::Comma)
							break;
						i++;
					}
					if (tokens[i].Type != close)
						return false;
					i++;
					if (isVector && (count == 2 || count == 3))
					{
						variable.Kind = count == 2 ? ScriptVariable::ValueKind::Vector2 : ScriptVariable::ValueKind::Vector3;
						if (count == 2)
							variable.Components[2] = 0.0;
					}
					else if (!isVector && (count == 3 || count == 4))
					{
						variable.Kind = ScriptVariable::ValueKind::Color;
						if (count == 3)
							variable.Components[3] = 1.0;
					}
					else
					{
						return false;
					}
					break;
				}
				default:
					return false;
			}
			return tokens[i].Type == TokenType::Newline || tokens[i].Type == TokenType::Eof;
		}

	}

	std::vector<ScriptVariable> FindScriptVariables(std::string_view source)
	{
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();

		std::vector<ScriptVariable> variables;
		std::unordered_set<std::string> seen;
		int depth = 0;
		for (size_t i = 0; i < tokens.size() && tokens[i].Type != TokenType::Eof; i++)
		{
			if (TokenOpensBlock(tokens, i))
			{
				depth++;
				continue;
			}
			if (tokens[i].Type == TokenType::End)
			{
				if (depth > 0)
					depth--;
				continue;
			}

			const bool lineStart = i == 0 || tokens[i - 1].Type == TokenType::Newline;
			if (depth != 0 || !lineStart || tokens[i].Type != TokenType::Set)
				continue;
			if (i + 3 >= tokens.size() || tokens[i + 1].Type != TokenType::Identifier || tokens[i + 2].Type != TokenType::To)
				continue;

			ScriptVariable variable;
			variable.Name = std::string(tokens[i + 1].Lexeme);
			variable.Line = tokens[i].Line;
			if (variable.Name.empty() || variable.Name[0] == '_' || seen.contains(variable.Name))
				continue;
			if (ReadPlainValue(tokens, i + 3, variable))
			{
				seen.insert(variable.Name);
				variables.push_back(std::move(variable));
			}
		}
		return variables;
	}

}
