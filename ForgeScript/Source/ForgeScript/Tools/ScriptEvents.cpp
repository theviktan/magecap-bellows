#include "ForgeScript/Tools/ScriptEvents.h"

#include "ForgeScript/Compiler/Lexer.h"

namespace ForgeScript {

	namespace {

		// An event's name starting at tokens[i], read as the compiler's EventName reads it: the text in
		// quotes, or the words up to 'with', 'by', 'then' or 'do'. Sets 'last' to its last token.
		std::string EventNameAt(const std::vector<Token>& tokens, size_t i, size_t& last)
		{
			if (i < tokens.size() && tokens[i].Type == TokenType::String)
			{
				last = i;
				return tokens[i].Text;
			}
			std::string name;
			for (; i < tokens.size() && tokens[i].IsWord() && tokens[i].Type != TokenType::With && tokens[i].Type != TokenType::By &&
				tokens[i].Type != TokenType::Then && tokens[i].Type != TokenType::Do; i++)
			{
				if (!name.empty())
					name += ' ';
				name += std::string(tokens[i].Lexeme);
				last = i;
			}
			return name;
		}

		// 'when "Start" is clicked' and 'wait for "Start" is clicked': the entity's name and 'is' come
		// before the event's own name.
		size_t AfterEntityName(const std::vector<Token>& tokens, size_t i)
		{
			if (i + 1 < tokens.size() && tokens[i].Type == TokenType::String && tokens[i + 1].Type == TokenType::Is)
				return i + 2;
			return i;
		}

	}

	ScriptEvents FindScriptEvents(const std::string& source)
	{
		ScriptEvents events;
		Lexer lexer(source);
		const std::vector<Token> tokens = lexer.Tokenize();
		for (size_t i = 0; i < tokens.size(); i++)
		{
			const Token& token = tokens[i];
			size_t last = 0;
			if (token.Type == TokenType::When && i + 1 < tokens.size() && tokens[i + 1].Type != TokenType::My)
			{
				const std::string name = EventNameAt(tokens, AfterEntityName(tokens, i + 1), last);
				if (!name.empty())
					events.Listened.insert(name);
			}
			else if (token.Type == TokenType::Wait && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::For)
			{
				const std::string name = EventNameAt(tokens, AfterEntityName(tokens, i + 2), last);
				if (!name.empty())
					events.Listened.insert(name);
			}
			else if (token.Type == TokenType::Trigger && i + 1 < tokens.size())
			{
				const std::string name = EventNameAt(tokens, i + 1, last);
				const Token& first = tokens[i + 1];
				const Token& end = tokens[last];
				if (!name.empty() && first.Line == end.Line)
				{
					EventTrigger trigger;
					trigger.Name = name;
					trigger.Line = first.Line;
					trigger.Column = first.Column;
					trigger.EndColumn = end.Column + static_cast<int>(end.Lexeme.size());
					trigger.Quoted = first.Type == TokenType::String;
					events.Triggers.push_back(std::move(trigger));
				}
			}
		}
		return events;
	}

	std::set<std::string> FindPythonTexts(const std::string& source)
	{
		// Plain '...' and "..." pieces of text, one line each; a backslash keeps the next character in
		// the text. Comments are skipped so a quote in one doesn't start a piece of text.
		std::set<std::string> texts;
		for (size_t i = 0; i < source.size(); i++)
		{
			const char c = source[i];
			if (c == '#')
			{
				while (i < source.size() && source[i] != '\n')
					i++;
				continue;
			}
			if (c != '"' && c != '\'')
				continue;
			std::string text;
			size_t j = i + 1;
			for (; j < source.size() && source[j] != c && source[j] != '\n'; j++)
			{
				if (source[j] == '\\' && j + 1 < source.size())
					j++;
				text += source[j];
			}
			if (j < source.size() && source[j] == c)
				texts.insert(std::move(text));
			i = j;
		}
		return texts;
	}

}
