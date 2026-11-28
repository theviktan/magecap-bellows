#include "ForgeScript/Compiler/ScriptVariables.h"

#include "ForgeScript/Compiler/Lexer.h"

#include <algorithm>
#include <unordered_map>
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
		// A parameter's type ('f as function', 'k as kind') opens nothing.
		if ((tokens[i].Type == TokenType::Function || tokens[i].Type == TokenType::Kind) && previous == TokenType::Identifier &&
			tokens[i - 1].Lexeme == "as")
			return false;

		switch (tokens[i].Type)
		{
			// The 'if' of 'else if' continues the open 'if' and shares its 'end', and an 'if' that picks
			// one of two values has no 'end'.
			case TokenType::If:
				return previous != TokenType::Else && previous != TokenType::Otherwise && !IfPicksAValue(tokens, i);
			case TokenType::While: case TokenType::Repeat:
			case TokenType::Phrase: case TokenType::When: case TokenType::Try:
				return true;
			// 'wait until' and 'wait for' are waits, not loops.
			case TokenType::Until: case TokenType::For:
				return previous != TokenType::Wait;
			// 'x is a function' and 'x is a kind' test a value's type; they open nothing.
			case TokenType::Function: case TokenType::Kind:
				return !(previous == TokenType::Identifier && (tokens[i - 1].Lexeme == "a" || tokens[i - 1].Lexeme == "an"));
			case TokenType::Identifier:
				return MatchOpensBlock(tokens, i) || TimerOpensBlock(tokens, i) || TestOpensBlock(tokens, i) || PropertyOpensBlock(tokens, i) ||
					ContractOpensBlock(tokens, i);
			default:
				return false;
		}
	}

	bool ContractOpensBlock(const std::vector<Token>& tokens, size_t i)
	{
		// 'contract Name' alone on its line (the compiler's IsContractAt; tools don't see phrases).
		if (tokens[i].Type != TokenType::Identifier || tokens[i].Lexeme != "contract" || i + 1 >= tokens.size() ||
			tokens[i + 1].Type != TokenType::Identifier || (i > 0 && tokens[i - 1].Type != TokenType::Newline))
			return false;
		const TokenType after = i + 2 < tokens.size() ? tokens[i + 2].Type : TokenType::Eof;
		return after == TokenType::Newline || after == TokenType::Eof;
	}

	bool PropertyOpensBlock(const std::vector<Token>& tokens, size_t i)
	{
		// 'property name [as type]' alone on its line, directly inside a kind (the compiler's IsPropertyLine).
		if (tokens[i].Type != TokenType::Identifier || tokens[i].Lexeme != "property" || i + 1 >= tokens.size() ||
			tokens[i + 1].Type != TokenType::Identifier || (i > 0 && tokens[i - 1].Type != TokenType::Newline))
			return false;
		const TokenType after = i + 2 < tokens.size() ? tokens[i + 2].Type : TokenType::Eof;
		if (!(after == TokenType::Newline || after == TokenType::Eof || after == TokenType::Do ||
			(after == TokenType::Identifier && tokens[i + 2].Lexeme == "as")))
			return false;
		// The innermost block it's in must be a kind: walk back, skipping blocks that are closed.
		int depth = 0;
		for (size_t j = i; j-- > 0;)
		{
			const TokenType previous = j > 0 ? tokens[j - 1].Type : TokenType::Newline;
			if (tokens[j].Type == TokenType::End && previous != TokenType::Dot && previous != TokenType::Possessive)
				depth++;
			else if (TokenOpensBlock(tokens, j))
			{
				if (depth == 0)
					return tokens[j].Type == TokenType::Kind;
				depth--;
			}
		}
		return false;
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
			if (i + 3 >= tokens.size() || tokens[i + 1].Type != TokenType::Identifier)
				continue;
			// 'set speed to 5', or with a type for checkers, 'set speed as number to 5' ('list of text' too).
			size_t to = i + 2;
			std::string type;
			if (tokens[to].Type == TokenType::Identifier && tokens[to].Lexeme == "as")
			{
				for (to++; to < tokens.size() && (tokens[to].Type == TokenType::Identifier || tokens[to].Type == TokenType::Of ||
					tokens[to].Type == TokenType::Function || tokens[to].Type == TokenType::Kind); to++)
					type += (type.empty() ? "" : " ") + std::string(tokens[to].Lexeme);
			}
			if (to + 1 >= tokens.size() || tokens[to].Type != TokenType::To)
				continue;

			ScriptVariable variable;
			variable.Name = std::string(tokens[i + 1].Lexeme);
			variable.Line = tokens[i].Line;
			variable.Type = type;
			if (variable.Name.empty() || variable.Name[0] == '_' || seen.contains(variable.Name))
				continue;
			if (ReadPlainValue(tokens, to + 1, variable))
			{
				seen.insert(variable.Name);
				variables.push_back(std::move(variable));
			}
		}
		return variables;
	}

	namespace {

		// What one top-level block does: whether it waits itself, and which functions it names in a call.
		struct BlockFacts
		{
			bool Waits = false;
			std::vector<std::string> Calls;
		};

		// Reads the block that starts at tokens[start] (its opener) to its matching 'end'; returns the
		// index after it.
		size_t ReadBlock(const std::vector<Token>& tokens, size_t start, BlockFacts& facts)
		{
			int depth = 0;
			size_t i = start;
			for (; i < tokens.size() && tokens[i].Type != TokenType::Eof; i++)
			{
				if (TokenOpensBlock(tokens, i))
					depth++;
				else if (tokens[i].Type == TokenType::End && --depth == 0)
					return i + 1;
				// A timer started from the handler holds while paused too, like a 'wait'.
				if (tokens[i].Type == TokenType::Wait || TimerOpensBlock(tokens, i))
					facts.Waits = true;
				else if (tokens[i].Type == TokenType::Identifier && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::LeftParen
					&& (i == 0 || (tokens[i - 1].Type != TokenType::Dot && tokens[i - 1].Type != TokenType::Possessive)))
					facts.Calls.emplace_back(tokens[i].Lexeme);
			}
			return i;
		}

		// "clicked", "\"Resume\" is clicked", "changed" for 'when changed with value'.
		std::string HandlerEvent(const std::vector<Token>& tokens, size_t when, std::string& eventName)
		{
			std::string written;
			size_t first = when + 1;
			size_t i = first;
			for (; i < tokens.size() && tokens[i].Type != TokenType::Newline && tokens[i].Type != TokenType::With && tokens[i].Type != TokenType::Eof; i++)
			{
				if (!written.empty())
					written += ' ';
				written += tokens[i].Lexeme;
			}
			// The event's own name: after 'is' in the named form, else the whole text.
			size_t nameStart = first;
			if (first + 1 < i && tokens[first].Type == TokenType::String && tokens[first + 1].Type == TokenType::Is)
				nameStart = first + 2;
			eventName.clear();
			for (size_t j = nameStart; j < i; j++)
			{
				if (!eventName.empty())
					eventName += ' ';
				eventName += tokens[j].Lexeme;
			}
			return written;
		}

	}

	std::vector<WaitingHandler> FindHandlersThatWait(std::string_view source, const std::vector<std::string>& events)
	{
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();

		struct Handler
		{
			WaitingHandler Info;
			BlockFacts Facts;
		};
		std::vector<Handler> handlers;
		std::unordered_map<std::string, BlockFacts> functions;
		int depth = 0;
		for (size_t i = 0; i < tokens.size() && tokens[i].Type != TokenType::Eof;)
		{
			const bool opens = TokenOpensBlock(tokens, i);
			if (depth == 0 && opens && tokens[i].Type == TokenType::When)
			{
				Handler handler;
				std::string eventName;
				handler.Info.Event = HandlerEvent(tokens, i, eventName);
				handler.Info.Line = tokens[i].Line;
				const bool wanted = std::find(events.begin(), events.end(), eventName) != events.end();
				i = ReadBlock(tokens, i, handler.Facts);
				if (wanted)
					handlers.push_back(std::move(handler));
				continue;
			}
			if (depth == 0 && opens && tokens[i].Type == TokenType::Function && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Identifier)
			{
				BlockFacts facts;
				const std::string name(tokens[i + 1].Lexeme);
				i = ReadBlock(tokens, i, facts);
				functions.emplace(name, std::move(facts));
				continue;
			}
			if (opens)
				depth++;
			else if (tokens[i].Type == TokenType::End && depth > 0)
				depth--;
			i++;
		}

		// A function waits when it waits itself or calls one that does.
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (auto& [name, facts] : functions)
			{
				if (facts.Waits)
					continue;
				for (const std::string& call : facts.Calls)
				{
					auto it = functions.find(call);
					if (it != functions.end() && it->second.Waits)
					{
						facts.Waits = changed = true;
						break;
					}
				}
			}
		}

		std::vector<WaitingHandler> waiting;
		for (const Handler& handler : handlers)
		{
			bool waits = handler.Facts.Waits;
			for (size_t c = 0; !waits && c < handler.Facts.Calls.size(); c++)
			{
				auto it = functions.find(handler.Facts.Calls[c]);
				waits = it != functions.end() && it->second.Waits;
			}
			if (waits)
				waiting.push_back(handler.Info);
		}
		return waiting;
	}

}
