#include "ForgeScript/Compiler/Compiler.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Compiler/Phrase.h"
#include "ForgeScript/Core/Object.h"
#include "ForgeScript/Core/Suggest.h"
#include "ForgeScript/VM/VM.h"

#if defined(_MSC_VER)
	#include <intrin.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ForgeScript {

	std::string CompileError::ToString() const
	{
		std::string text = File.empty() ? std::string("script") : File;
		text += ":" + std::to_string(Line);
		if (Column > 0)
			text += ":" + std::to_string(Column);
		text += ": " + Message;
		return text;
	}

	namespace {

		// Thrown to stop compiling at the first mistake. ForgeScript reports one clear error at a time.
		struct CompileAbort {};

		enum class ScopeKind
		{
			Script,
			Function,
			Method,
			FieldInit,
			Event,
			Phrase
		};

		struct Local
		{
			std::string Name;
			int Depth = 0;
			bool IsCaptured = false;
			size_t NameIndex = static_cast<size_t>(-1); // Its entry in Function::LocalNames, if named.
			int Id = -1; // Tells variables apart for CompileOptions::NameUses, even with the same name.
		};

		struct UpvalueInfo
		{
			uint8_t Index = 0;
			bool IsLocal = false;
			int LocalId = -1; // The Local::Id of the variable it reaches.
		};

		struct LoopInfo
		{
			size_t ContinueTarget = 0;
			bool ContinueKnown = false;
			std::vector<size_t> ContinueJumps;
			std::vector<size_t> BreakJumps;
			int ScopeDepth = 0;
			int TryDepth = 0;
		};

		// Where a parameter's default value is written: tokens [Start, End), or both 0 when it has none.
		struct DefaultValue
		{
			size_t Start = 0;
			size_t End = 0;
		};

		// A 'try' with a 'finally' whose try or catch section is being compiled. Leaving it early (return,
		// stop, skip) runs a copy of its finally code first.
		struct FinallyContext
		{
			size_t BodyStart = 0;  // the first token after 'finally'
			int TryDepth = 0;      // handlers open outside the 'try'
			size_t LocalCount = 0; // locals declared outside the 'try'
			size_t LoopCount = 0;  // loops around the 'try'
		};

		struct FunctionState
		{
			FunctionState* Enclosing = nullptr;
			Function* Fn = nullptr;
			ScopeKind Kind = ScopeKind::Script;
			std::vector<Local> Locals;
			std::vector<UpvalueInfo> Upvalues;
			std::vector<LoopInfo> Loops;
			std::vector<FinallyContext> Finally;
			int ScopeDepth = 0;
			int TryDepth = 0;
		};

		enum class TargetType
		{
			Local,
			Upvalue,
			Global,
			Property,
			Index,
			Temporary
		};

		struct Target
		{
			TargetType Type = TargetType::Global;
			int Slot = 0;
			uint16_t Name = 0;
		};

		// A target such as "entity.position.x". Every property or list item that was read on the way
		// is remembered so a changed vector can be written back. Engine properties usually hand out a
		// copy of a vector, so without the write back "set entity.position.x to 5" would change nothing.
		struct TargetChain
		{
			Target Final;
			std::vector<Target> WriteBacks;
		};

		// Words that may not appear inside phrase patterns because they open or close blocks.
		bool IsBlockKeyword(TokenType type)
		{
			switch (type)
			{
				case TokenType::If: case TokenType::Then: case TokenType::Else: case TokenType::Otherwise:
				case TokenType::End: case TokenType::While: case TokenType::Until: case TokenType::Do:
				case TokenType::Repeat: case TokenType::For: case TokenType::Function: case TokenType::Phrase:
				case TokenType::Kind: case TokenType::When: case TokenType::Try: case TokenType::Catch:
					return true;
				default:
					return false;
			}
		}

		const std::set<std::string> s_NoStopWords;

	}

	std::string CheckUsePath(const std::string& path)
	{
		const std::string inside = "'use' only reaches scripts inside this script's place: the project's Assets folder, or "
			"the folder this script is in and the folders below it. Write the path from there, like: use \"Scripts/helpers\"";
		if (path.empty())
			return "Write the name of the script to use in the quotes, like: use \"helpers\"";
		if (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'))
			return inside;
		std::string last;
		size_t start = 0;
		while (start <= path.size())
		{
			const size_t end = path.find_first_of("/\\", start);
			const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
			if (part == "..")
				return inside;
			last = part;
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		const size_t dot = last.rfind('.');
		if (dot != std::string::npos && dot > 0)
		{
			std::string extension = last.substr(dot + 1);
			for (char& c : extension)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (extension != "forge")
				return "'use' only loads ForgeScript scripts: the name must end in .forge or have no extension, like: use \"helpers\"";
		}
		return "";
	}

	class Compiler
	{
	public:
		Compiler(VM& vm, Module* module, std::vector<Token> tokens, const std::string& fileName, const CompileOptions& options)
			: m_VM(vm), m_Module(module), m_Tokens(std::move(tokens)), m_FileName(fileName), m_Options(options),
			  m_Phrases(vm.GetPhraseTable())
		{
			m_StopWords.push_back(&s_NoStopWords);
		}

		// Like Compile, but keeps going after a mistake to find the next ones (for editors and checkers).
		// The compiled code is thrown away, since a script with mistakes never runs.
		std::vector<CompileError> FindErrors()
		{
			std::vector<CompileError> errors;
			m_AllErrors = &errors;
			CompileError first;
			if (!Compile(first) && errors.empty())
				errors.push_back(first);
			m_AllErrors = nullptr;
			std::stable_sort(errors.begin(), errors.end(), [](const CompileError& a, const CompileError& b)
			{
				return a.Line != b.Line ? a.Line < b.Line : a.Column < b.Column;
			});
			return errors;
		}

		Function* Compile(CompileError& outError)
		{
			m_StackStart = StackAddress();
			try
			{
				// Text the lexer could not read (an unfinished string, a stray character). The rest of the
				// script cannot be trusted after one of these, so errors stop here even when collecting.
				for (const Token& token : m_Tokens)
				{
					if (token.Type != TokenType::Error)
						continue;
					if (!m_AllErrors)
						ErrorAt(token, token.Text);
					RecordError(token, token.Text);
				}
				if (m_AllErrors && !m_AllErrors->empty())
					throw CompileAbort();

				std::unordered_set<std::string> visited;
				PrescanPhrases(m_Tokens, visited);
				// "match 5" may be a phrase's line when the engine, this script or one it uses has a phrase
				// starting with 'match'; otherwise a match with no 'is' still pairs with its 'end'.
				m_ChoicelessMatchIsBlock = m_Phrases.GetRoot()->Words.count("match") == 0;
				// Likewise a phrase starting with 'every' or 'after' keeps its lines (timers came later).
				m_EveryIsPhrase = m_Phrases.GetRoot()->Words.count("every") > 0;
				m_AfterIsPhrase = m_Phrases.GetRoot()->Words.count("after") > 0;
				m_ChoicesIsPhrase = m_Phrases.GetRoot()->Words.count("choices") > 0;
				m_TestIsPhrase = m_Phrases.GetRoot()->Words.count("test") > 0;
				m_ExpectIsPhrase = m_Phrases.GetRoot()->Words.count("expect") > 0;
				PrescanChoices();
				for (const char* verb : { "sort", "shuffle", "reverse" })
				{
					if (m_Phrases.GetRoot()->Words.count(verb) > 0)
						m_ListVerbPhrases.insert(verb);
				}
				PrescanModuleNames();

				Function* fn = m_VM.NewFunction();
				fn->Name = m_VM.Intern(m_FileName.empty() ? "script" : m_FileName);
				fn->Owner = m_Module;

				FunctionState script;
				script.Fn = fn;
				script.Kind = ScopeKind::Script;
				script.Locals.push_back({ "", 0, false });
				m_State = &script;

				// Top level functions and phrases are compiled first, so they can be used anywhere
				// in the script, even above the lines that define them.
				std::vector<std::pair<size_t, size_t>> hoisted = FindHoistedDefinitions();
				for (const auto& [start, end] : hoisted)
				{
					m_Pos = start;
					RecoverableUnit(script, [unitEnd = end] { return unitEnd; }, [this] { Statement(); });
				}

				m_Pos = 0;
				size_t nextHoisted = 0;
				SkipNewlines();
				while (!Check(TokenType::Eof))
				{
					if (nextHoisted < hoisted.size() && m_Pos == hoisted[nextHoisted].first)
					{
						m_Pos = hoisted[nextHoisted].second;
						m_ClosingPos = 0;
						nextHoisted++;
						EndOfStatement();
						SkipNewlines();
						continue;
					}
					const size_t start = m_Pos;
					RecoverableUnit(script, [this, start] { return EndOfUnit(start); }, [this] { Statement(); });
					SkipNewlines();
				}

				WarnTypesOfCalls();
				if (m_AllErrors && !m_AllErrors->empty())
					throw CompileAbort();
				EmitOp(OpCode::Nothing);
				EmitOp(OpCode::Return);
				m_State = nullptr;
				return fn;
			}
			catch (const CompileAbort&)
			{
				outError = m_Error;
				m_State = nullptr;
				return nullptr;
			}
		}

	private:
		// ------------------------------------------------------------------ Tokens

		const Token& Current() const { return m_Tokens[m_Pos]; }
		const Token& Previous() const { return m_Tokens[m_Pos > 0 ? m_Pos - 1 : 0]; }
		const Token& Peek(size_t ahead) const
		{
			size_t index = std::min(m_Pos + ahead, m_Tokens.size() - 1);
			return m_Tokens[index];
		}

		bool Check(TokenType type) const { return Current().Type == type; }

		void Advance()
		{
			if (m_Pos < m_Tokens.size() - 1)
				m_Pos++;
		}

		bool Match(TokenType type)
		{
			if (!Check(type))
				return false;
			Advance();
			return true;
		}

		bool CheckWord(const char* word) const
		{
			return Current().Type == TokenType::Identifier && Current().Lexeme == word;
		}

		bool MatchWord(const char* word)
		{
			if (!CheckWord(word))
				return false;
			Advance();
			return true;
		}

		void Consume(TokenType type, const std::string& message)
		{
			if (Check(type))
			{
				Advance();
				return;
			}
			ErrorAtCurrent(message);
		}

		std::string ConsumeName(const std::string& message)
		{
			if (!Check(TokenType::Identifier))
			{
				if (Current().IsWord())
					ErrorAtCurrent("'" + std::string(Current().Lexeme) + "' is a ForgeScript keyword and cannot be used as a name here.");
				ErrorAtCurrent(message);
			}
			std::string name(Current().Lexeme);
			if (m_Options.NameUses)
				m_ConsumedNames.push_back(m_Pos);
			Advance();
			return name;
		}

		// Property and method names may be any word, including keywords (list.add, map.end ...).
		std::string ConsumeMemberName()
		{
			if (!Current().IsWord())
				ErrorAtCurrent("Expected a property name after '.'.");
			std::string name(Current().Lexeme);
			// A field or method: which kind's can't be told here, so every one of that name shares a key.
			if (m_Options.NameUses)
				NoteName(m_Pos, name, "F:" + name, false);
			Advance();
			return name;
		}

		void SkipNewlines()
		{
			while (Match(TokenType::Newline)) {}
		}

		bool IsBlockEnd() const
		{
			switch (Current().Type)
			{
				case TokenType::End: case TokenType::Else: case TokenType::Otherwise:
				case TokenType::Catch: case TokenType::Eof:
					return true;
				// Inside a match, a line starting with 'is' starts the next choice.
				case TokenType::Is:
					return m_MatchDepth > 0;
				default:
					return m_FinallyMarkers.count(m_Pos) > 0;
			}
		}

		void EndOfStatement()
		{
			if (Match(TokenType::Newline) || IsBlockEnd())
				return;
			if (Check(TokenType::Equal))
				ErrorAtCurrent("'=' is not used in ForgeScript. Use 'set x to 5' to change a value, or 'is' to compare.");
			ErrorAtCurrent("Expected the end of the line, but found '" + std::string(Current().Lexeme) + "'." + GuessedStatementHint());
		}

		// The words from tokens[from] to the end of the line, for a hint that repeats what was written.
		std::string LineText(size_t from) const
		{
			std::string text;
			for (size_t i = from; i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Newline && m_Tokens[i].Type != TokenType::Eof; i++)
			{
				if (!text.empty())
					text += ' ';
				text += m_Tokens[i].Lexeme;
			}
			return text;
		}

		// The teaching layer: a statement written the way another language (or plain English) says it,
		// answered with the ForgeScript that does it. Only used on a line that is already a mistake.
		std::string GuessedStatementHint() const
		{
			const size_t start = m_StatementStart;
			if (start + 1 >= m_Tokens.size() || m_Tokens[start].Type != TokenType::Identifier)
				return {};
			const std::string_view word = m_Tokens[start].Lexeme;
			const Token& next = m_Tokens[start + 1];
			const std::string name = next.Type == TokenType::Identifier ? std::string(next.Lexeme) : "x";
			if ((word == "increase" || word == "decrease") && next.Type == TokenType::Identifier &&
				start + 3 < m_Tokens.size() && m_Tokens[start + 2].Type == TokenType::By)
			{
				const std::string amount = LineText(start + 3);
				return word == "increase" ? " To make a number bigger, write: add " + amount + " to " + name :
					" To make a number smaller, write: take " + amount + " from " + name;
			}
			if (next.Type != TokenType::Identifier)
				return {};
			if (word == "def" || word == "func" || word == "fn" || word == "fun" || word == "sub" || word == "procedure")
				return " To make a function, write: function " + name + "() ... end";
			if (word == "var" || word == "let" || word == "local" || word == "dim" || word == "auto")
				return " To make a variable, write: set " + name + " to ...";
			if (word == "const")
				return " To make a constant, write: constant " + name + " is ...";
			if (word == "elif" || word == "elsif" || word == "elseif")
				return " ForgeScript writes it as two words, like: else if " + name + " is 2 then";
			return {};
		}

		// The same for a value: symbols from other languages that ForgeScript writes in words.
		std::string GuessedValueHint() const
		{
			const Token& token = Current();
			const Token& before = Previous();
			const Token& after = Peek(1);
			// 'x += 1', 'x -= 1', 'x *= 2', 'x /= 2': the value before the operator is the one being changed.
			if (token.Type == TokenType::Equal && m_Pos >= 2)
			{
				const std::string target(m_Tokens[m_Pos - 2].Lexeme);
				const std::string amount = LineText(m_Pos + 1);
				switch (before.Type)
				{
					case TokenType::Plus:  return " To add to a value, write: add " + amount + " to " + target;
					case TokenType::Minus: return " To take from a value, write: take " + amount + " from " + target;
					case TokenType::Star:  return " To multiply a value, write: multiply " + target + " by " + amount;
					case TokenType::Slash: return " To divide a value, write: divide " + target + " by " + amount;
					default: break;
				}
			}
			if ((token.Type == TokenType::Plus && before.Type == TokenType::Plus) || (token.Type == TokenType::Minus && before.Type == TokenType::Minus))
			{
				const std::string target = m_Pos >= 2 ? std::string(m_Tokens[m_Pos - 2].Lexeme) : "x";
				return token.Type == TokenType::Plus ? " ForgeScript has no ++. Write: add 1 to " + target :
					" ForgeScript has no --. Write: take 1 from " + target;
			}
			if (token.Type == TokenType::Slash && before.Type == TokenType::Slash)
				return " ForgeScript has no //. For a whole-number result, write: floor(a / b)";
			if (token.Type == TokenType::Colon && (after.Type == TokenType::Newline || after.Type == TokenType::Eof))
				return " ForgeScript doesn't end a line with ':'. Write 'then' after an if, or 'do' after a loop, and close the block with 'end'.";
			return {};
		}

		// The quick fix for GuessedValueHint's 'x += 5', 'x++' and 'x--', when the whole statement is just that:
		// the statement becomes 'add 5 to x' (and so on).
		void FixGuessedValue(size_t targetIndex, const std::string& replacement, const std::string& title)
		{
			if (targetIndex != m_StatementStart || targetIndex >= m_Tokens.size() || m_Tokens[targetIndex].Type != TokenType::Identifier)
				return;
			size_t last = targetIndex;
			while (last + 1 < m_Tokens.size() && m_Tokens[last + 1].Type != TokenType::Newline && m_Tokens[last + 1].Type != TokenType::Eof)
				last++;
			FixTokens(m_Tokens[targetIndex], m_Tokens[last], title, replacement);
		}

		void FixGuessedValueAtCurrent()
		{
			const Token& token = Current();
			const Token& before = Previous();
			if (m_Pos < 2)
				return;
			const size_t target = m_Pos - 2;
			const std::string name(m_Tokens[target].Lexeme);
			if (token.Type == TokenType::Equal)
			{
				const std::string amount = LineText(m_Pos + 1);
				if (amount.empty())
					return;
				switch (before.Type)
				{
					case TokenType::Plus:  FixGuessedValue(target, "add " + amount + " to " + name, "Write: add " + amount + " to " + name); break;
					case TokenType::Minus: FixGuessedValue(target, "take " + amount + " from " + name, "Write: take " + amount + " from " + name); break;
					case TokenType::Star:  FixGuessedValue(target, "multiply " + name + " by " + amount, "Write: multiply " + name + " by " + amount); break;
					case TokenType::Slash: FixGuessedValue(target, "divide " + name + " by " + amount, "Write: divide " + name + " by " + amount); break;
					default: break;
				}
			}
			else if (token.Type == TokenType::Plus && before.Type == TokenType::Plus)
				FixGuessedValue(target, "add 1 to " + name, "Write: add 1 to " + name);
		}

		// ------------------------------------------------------------------ Errors

		[[noreturn]] void ErrorAt(const Token& token, const std::string& message)
		{
			m_Error = CompileError();
			m_Error.Message = message;
			m_Error.File = m_FileName;
			m_Error.Line = token.Line;
			m_Error.Column = token.Column;
			if (m_Fix.HasFix())
			{
				m_Error.FixTitle = std::move(m_Fix.FixTitle);
				m_Error.FixLine = m_Fix.FixLine;
				m_Error.FixColumn = m_Fix.FixColumn;
				m_Error.FixEndLine = m_Fix.FixEndLine;
				m_Error.FixEndColumn = m_Fix.FixEndColumn;
				m_Error.FixText = std::move(m_Fix.FixText);
			}
			m_Fix = CompileError();
			throw CompileAbort();
		}

		// --------------------------------------------------------------- Quick fixes
		// A mistake that says exactly what to write instead carries that change for editors (CompileError's
		// Fix fields). Set it right before the ErrorAt it belongs to.

		static int EndColumnOf(const Token& token) { return token.Column + static_cast<int>(token.Lexeme.size()); }

		// Replace the tokens from 'first' to 'last' (both included, on one line) with 'text'.
		void FixTokens(const Token& first, const Token& last, std::string title, std::string text)
		{
			if (first.Line != last.Line || first.Lexeme.empty() || last.Lexeme.empty())
				return;
			m_Fix.FixTitle = std::move(title);
			m_Fix.FixLine = m_Fix.FixEndLine = first.Line;
			m_Fix.FixColumn = first.Column;
			m_Fix.FixEndColumn = EndColumnOf(last);
			m_Fix.FixText = std::move(text);
		}

		// The spaces or tabs a line starts with, read from the source the tokens point into.
		static std::string IndentOf(const Token& onLine)
		{
			if (onLine.Lexeme.empty() || onLine.Column < 1)
				return {};
			const char* lineStart = onLine.Lexeme.data() - (onLine.Column - 1);
			std::string indent;
			for (const char* c = lineStart; c < onLine.Lexeme.data() && (*c == ' ' || *c == '\t'); c++)
				indent += *c;
			return indent;
		}

		// For "This 'if' is missing its 'end'": insert 'end', at the opener's indent, after the last line
		// indented deeper than the opener (or the last line of the script).
		size_t IndexOfToken(const Token& token) const
		{
			size_t index = 0;
			while (index < m_Tokens.size() && !(m_Tokens[index].Line == token.Line && m_Tokens[index].Column == token.Column))
				index++;
			return index;
		}

		// The first word on the line of m_Tokens[index].
		const Token& FirstOnLine(size_t index) const
		{
			while (index > 0 && m_Tokens[index - 1].Line == m_Tokens[index].Line && m_Tokens[index - 1].Type != TokenType::Newline)
				index--;
			return m_Tokens[index];
		}

		void FixMissingEnd(const Token& reported)
		{
			// When a block inside this one took an 'end' indented less than its own line, that 'end' was
			// meant for an outer block, and the inner one is the block missing its 'end'.
			const Token opener = m_MisplacedEndOpener.Line > reported.Line ? m_MisplacedEndOpener : reported;
			m_MisplacedEndOpener = Token();
			const size_t openerIndex = IndexOfToken(opener);
			if (openerIndex >= m_Tokens.size())
				return;
			const Token& openerLine = FirstOnLine(openerIndex);
			const Token* last = &opener;
			bool lineStart = false;
			for (size_t i = openerIndex + 1; i < m_Tokens.size(); i++)
			{
				const Token& token = m_Tokens[i];
				if (token.Type == TokenType::Eof)
					break;
				if (token.Type == TokenType::Newline)
				{
					lineStart = true;
					continue;
				}
				if (lineStart && token.Column <= openerLine.Column)
					break;
				lineStart = false;
				last = &token;
			}
			if (last->Lexeme.empty())
				return;
			// The end of that line in the source, which ends where the Eof token points.
			const char* sourceEnd = m_Tokens.back().Type == TokenType::Eof ? m_Tokens.back().Lexeme.data() : nullptr;
			const char* lineEnd = last->Lexeme.data() + last->Lexeme.size();
			if (!sourceEnd || lineEnd > sourceEnd)
				return;
			int column = EndColumnOf(*last);
			while (lineEnd < sourceEnd && *lineEnd != '\n' && *lineEnd != '\r')
			{
				lineEnd++;
				column++;
			}
			m_Fix.FixTitle = "Add the missing 'end'";
			m_Fix.FixLine = m_Fix.FixEndLine = last->Line;
			m_Fix.FixColumn = m_Fix.FixEndColumn = column;
			m_Fix.FixText = "\n" + IndentOf(openerLine) + "end";
		}

		[[noreturn]] void ErrorAtCurrent(const std::string& message) { ErrorAt(Current(), message); }
		[[noreturn]] void Error(const std::string& message) { ErrorAt(Previous(), message); }

		// ------------------------------------------------------------------ Prescan

		bool IsBlockOpener(const std::vector<Token>& tokens, size_t i) const
		{
			const Token& token = tokens[i];
			if (i > 0 && (tokens[i - 1].Type == TokenType::Dot || tokens[i - 1].Type == TokenType::Possessive))
				return false;
			if (i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::Colon)
				return false;
			// A parameter's type ('f as function', 'k as kind') opens nothing.
			if ((token.Type == TokenType::Function || token.Type == TokenType::Kind) && i > 0 &&
				tokens[i - 1].Type == TokenType::Identifier && tokens[i - 1].Lexeme == "as")
				return false;

			switch (token.Type)
			{
				// The 'if' of 'else if' continues the open 'if' and shares its 'end', and an 'if' that picks
				// one of two values has no 'end'.
				case TokenType::If:
					return !(i > 0 && (tokens[i - 1].Type == TokenType::Else || tokens[i - 1].Type == TokenType::Otherwise)) &&
						!IfPicksAValue(tokens, i);
				case TokenType::While: case TokenType::Repeat:
				case TokenType::Phrase: case TokenType::When: case TokenType::Try:
					return true;
				// 'wait until' and 'wait for' are waits, not loops.
				case TokenType::Until: case TokenType::For:
					return !(i > 0 && tokens[i - 1].Type == TokenType::Wait);
				// 'x is a function' and 'x is a kind' test a value's type; they open nothing.
				case TokenType::Function: case TokenType::Kind:
					return !(i > 0 && tokens[i - 1].Type == TokenType::Identifier &&
						(tokens[i - 1].Lexeme == "a" || tokens[i - 1].Lexeme == "an"));
				case TokenType::Identifier:
					// A match with no 'is' choice is a mistake, but its 'end' still belongs to it, so the
					// rest of the script pairs up as written.
					return MatchOpensBlock(tokens, i) || (&tokens == &m_Tokens && m_ChoicelessMatchIsBlock && MatchWithoutChoicesAt(i)) ||
						IsTimerAt(tokens, i) || IsTestAt(tokens, i);
				default:
					return false;
			}
		}

		// Compiles one statement or definition of the function 'state'. When collecting errors, a mistake
		// inside it is recorded, the compiler's state is put back as it was before the unit, and compiling
		// continues at 'resume' (the end of the unit, including any block it opens). Skipping whole
		// statements keeps one mistake from causing more.
		// 'resume' gives where to go on after a mistake. It is only asked then: working it out scans the
		// rest of the unit, which for every statement of deeply nested blocks would add up quickly.
		template<typename Resume, typename Unit>
		void RecoverableUnit(FunctionState& state, Resume resume, Unit unit)
		{
			// Without a list of mistakes to fill, the first one ends the compile: no catch, so the error
			// goes straight up instead of through one handler per nested block (on Windows each handler
			// runs on top of the stack the error was thrown from).
			if (!m_AllErrors)
			{
				unit();
				return;
			}
			const size_t locals = state.Locals.size();
			const size_t loops = state.Loops.size();
			const size_t finallies = state.Finally.size();
			const int scopeDepth = state.ScopeDepth;
			const int tryDepth = state.TryDepth;
			const size_t stopWords = m_StopWords.size();
			try
			{
				unit();
			}
			catch (const CompileAbort&)
			{
				if (!m_AllErrors)
					throw;
				m_AllErrors->push_back(m_Error);
				if (m_AllErrors->size() >= c_MaxCollectedErrors)
					throw;
				m_State = &state;
				state.Locals.resize(locals);
				state.Loops.resize(loops);
				state.Finally.resize(finallies);
				state.ScopeDepth = scopeDepth;
				state.TryDepth = tryDepth;
				m_StopWords.resize(stopWords);
				m_NoInvoke = false;
				// A mistake that knows its block reaches further (a match with no 'is') says where to go on.
				const size_t next = resume();
				m_Pos = m_ResumeAfter != c_NoResume ? std::max(next, m_ResumeAfter) : next;
				m_ResumeAfter = c_NoResume;
			}
		}

		void RecordError(const Token& token, const std::string& message)
		{
			CompileError error;
			error.Message = message;
			error.File = m_FileName;
			error.Line = token.Line;
			error.Column = token.Column;
			m_AllErrors->push_back(error);
		}

		// Index of the newline (or end of file) that ends the statement starting at 'start',
		// counting whole blocks, so "when created ... end" or "set f to function() ... end" is one unit.
		size_t EndOfUnit(size_t start) const
		{
			int depth = 0;
			size_t i = start;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof)
			{
				if (IsBlockOpener(m_Tokens, i))
					depth++;
				// An 'end' with nothing open closes the enclosing block, so stop before it (unless the
				// statement is that 'end' itself, a stray one at the top level).
				else if (m_Tokens[i].Type == TokenType::End && --depth < 0)
					return i == start ? i + 1 : i;
				else if (m_Tokens[i].Type == TokenType::Newline && depth == 0 && i > start)
					return i;
				i++;
			}
			return i;
		}

		// Returns the index just after the 'end' that closes the block starting at index i.
		size_t SkipBlock(size_t i) const
		{
			int depth = 1;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof)
			{
				if (IsBlockOpener(m_Tokens, i))
				{
					depth++;
				}
				else if (m_Tokens[i].Type == TokenType::End)
				{
					depth--;
					if (depth == 0)
						return i + 1;
				}
				i++;
			}
			return i;
		}

		// Collects the names a scope assigns with "set NAME to ...", skipping nested functions.
		// The names of "set x, y to ...", read from the first name: two or more names with commas between
		// them, or empty.
		std::vector<std::string> UnpackNames(size_t first) const
		{
			std::vector<std::string> names;
			size_t i = first;
			while (i < m_Tokens.size() && m_Tokens[i].Type == TokenType::Identifier)
			{
				names.emplace_back(m_Tokens[i].Lexeme);
				if (i + 1 < m_Tokens.size() && m_Tokens[i + 1].Type == TokenType::Comma)
				{
					i += 2;
					continue;
				}
				break;
			}
			// Two or more names: the statement is meant to take a value apart (a missing 'to' is reported there).
			if (names.size() < 2)
				names.clear();
			return names;
		}

		void ScanScope(size_t start, bool isScript, std::vector<std::string>& outNames) const
		{
			int depth = 0;
			size_t i = start;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof)
			{
				const Token& token = m_Tokens[i];
				if (IsBlockOpener(m_Tokens, i))
				{
					// A timer's body is a function of its own, like a handler's.
					bool functionLike = token.Type == TokenType::Function || token.Type == TokenType::Phrase ||
						token.Type == TokenType::When || token.Type == TokenType::Kind ||
						(token.Type == TokenType::Identifier && (IsTimerAt(m_Tokens, i) || IsTestAt(m_Tokens, i)));
					if (functionLike)
					{
						if (isScript && (token.Type == TokenType::Function || token.Type == TokenType::Kind) &&
							i + 1 < m_Tokens.size() && m_Tokens[i + 1].Type == TokenType::Identifier)
						{
							outNames.emplace_back(m_Tokens[i + 1].Lexeme);
						}
						i = SkipBlock(i + 1);
						continue;
					}
					depth++;
					i++;
					continue;
				}

				if (token.Type == TokenType::End)
				{
					if (depth == 0 && !isScript)
						return;
					if (depth > 0)
						depth--;
					i++;
					continue;
				}

				bool afterMember = i > 0 && (m_Tokens[i - 1].Type == TokenType::Dot || m_Tokens[i - 1].Type == TokenType::Possessive);
				if (!afterMember && (token.Type == TokenType::Set || token.Type == TokenType::Constant) &&
					i + 2 < m_Tokens.size() && m_Tokens[i + 1].Type == TokenType::Identifier &&
					(m_Tokens[i + 2].Type == TokenType::To || m_Tokens[i + 2].Type == TokenType::Is))
				{
					outNames.emplace_back(m_Tokens[i + 1].Lexeme);
				}
				else if (!afterMember && token.Type == TokenType::Set)
				{
					// set x, y to ...: each name is set, so each is a variable here.
					std::vector<std::string> names = UnpackNames(i + 1);
					outNames.insert(outNames.end(), names.begin(), names.end());
				}
				else if (!afterMember && IsChoicesAt(m_Tokens, i))
				{
					outNames.emplace_back(m_Tokens[i + 1].Lexeme);
				}
				else if (!afterMember && token.Type == TokenType::Wait)
				{
					// wait for hit with damage: the event's values are set into its names.
					std::vector<std::string> names = WaitForNames(i);
					outNames.insert(outNames.end(), names.begin(), names.end());
				}
				i++;
			}
		}

		// Finds "function name(...) ... end" and "phrase ... end" blocks that are not inside anything else.
		std::vector<std::pair<size_t, size_t>> FindHoistedDefinitions() const
		{
			std::vector<std::pair<size_t, size_t>> ranges;
			int depth = 0;
			size_t i = 0;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof)
			{
				bool lineStart = i == 0 || m_Tokens[i - 1].Type == TokenType::Newline;
				if (IsBlockOpener(m_Tokens, i))
				{
					bool definition = (m_Tokens[i].Type == TokenType::Function && i + 1 < m_Tokens.size() &&
						m_Tokens[i + 1].Type == TokenType::Identifier) || m_Tokens[i].Type == TokenType::Phrase;
					if (depth == 0 && lineStart && definition)
					{
						size_t end = SkipBlock(i + 1);
						ranges.emplace_back(i, end);
						i = end;
						continue;
					}
					depth++;
				}
				else if (m_Tokens[i].Type == TokenType::End && depth > 0)
				{
					depth--;
				}
				i++;
			}
			return ranges;
		}

		void PrescanModuleNames()
		{
			std::vector<std::string> names;
			ScanScope(0, true, names);
			for (std::string& name : names)
				m_ModuleNames.insert(std::move(name));
		}

		bool IsModuleName(const std::string& name) const
		{
			if (m_ModuleNames.count(name))
				return true;
			String* key = m_VM.Intern(name);
			return m_Module->Globals.find(key) != m_Module->Globals.end();
		}

		// Builds a phrase pattern from the tokens after the 'phrase' keyword.
		bool PatternFromTokens(const std::vector<Token>& tokens, size_t& i, PhrasePattern& outPattern, std::string& outError) const
		{
			outPattern = PhrasePattern();
			while (i < tokens.size() && tokens[i].Type != TokenType::Newline && tokens[i].Type != TokenType::Eof)
			{
				const Token& token = tokens[i];
				if (token.Type == TokenType::LeftParen)
				{
					if (i + 2 >= tokens.size() || tokens[i + 1].Type != TokenType::Identifier || tokens[i + 2].Type != TokenType::RightParen)
					{
						outError = "A phrase slot looks like (name).";
						return false;
					}
					if (!outPattern.Parts.empty() && outPattern.Parts.back().empty())
					{
						outError = "Two slots in a row are not allowed; put a word between them.";
						return false;
					}
					outPattern.Parts.emplace_back();
					outPattern.SlotNames.emplace_back(tokens[i + 1].Lexeme);
					i += 3;
					continue;
				}
				if (!token.IsWord() || IsBlockKeyword(token.Type))
				{
					outError = "'" + std::string(token.Lexeme) + "' cannot be used inside a phrase.";
					return false;
				}
				if (outPattern.Parts.empty() && token.Type != TokenType::Identifier)
				{
					outError = "A phrase cannot start with the keyword '" + std::string(token.Lexeme) + "'.";
					return false;
				}
				outPattern.Parts.emplace_back(token.Lexeme);
				i++;
			}

			if (outPattern.Parts.empty())
			{
				outError = "A phrase needs at least one word, like: phrase heal (target) by (amount)";
				return false;
			}
			return true;
		}

		// Finds every phrase this script (and the scripts it uses) defines, so they can be
		// called before the line that defines them.
		void PrescanPhrases(const std::vector<Token>& tokens, std::unordered_set<std::string>& visited)
		{
			for (size_t i = 0; i < tokens.size(); i++)
			{
				if (tokens[i].Type == TokenType::Phrase && (i == 0 || tokens[i - 1].Type == TokenType::Newline))
				{
					size_t cursor = i + 1;
					PhrasePattern pattern;
					std::string error;
					if (PatternFromTokens(tokens, cursor, pattern, error))
					{
						m_Phrases.Add(pattern);
						m_ScriptPhraseWords.insert(pattern.Parts.front());
					}
				}
				else if (tokens[i].Type == TokenType::Use && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::String)
				{
					const std::string& path = tokens[i + 1].Text;
					std::string resolvedName;
					std::string source;
					if (!visited.insert(path).second)
						continue;
					if (m_VM.ResolveModuleSource(path, resolvedName, source))
					{
						Lexer lexer(source);
						std::vector<Token> moduleTokens = lexer.Tokenize();
						PrescanPhrases(moduleTokens, visited);
					}
				}
			}
		}

		// ------------------------------------------------------------------ Emitting

		Chunk& CurrentChunk() { return m_State->Fn->Chunk; }

		void EmitByte(uint8_t byte) { CurrentChunk().Write(byte, CodeLine()); }
		void EmitOp(OpCode op) { CurrentChunk().Write(op, CodeLine()); }

		// The line code being emitted belongs to. Code emitted right after an 'end' closes its block (a
		// loop's jump back, a definition's closure) and belongs to the line that opened the block, so
		// the debugger never stops on an 'end' line.
		int CodeLine() const
		{
			if (m_ClosingPos != 0 && m_Pos == m_ClosingPos)
				return m_ClosingLine;
			return Previous().Line;
		}

		// The line of the last token before the 'end' just consumed.
		int LineBeforeEnd() const
		{
			size_t i = m_Pos - 1;
			while (i > 0 && (m_Tokens[i].Type == TokenType::End || m_Tokens[i].Type == TokenType::Newline))
				i--;
			return m_Tokens[i].Line;
		}
		void EmitU16(uint16_t value)
		{
			EmitByte(static_cast<uint8_t>((value >> 8) & 0xff));
			EmitByte(static_cast<uint8_t>(value & 0xff));
		}

		uint16_t MakeConstant(Value value)
		{
			size_t index = CurrentChunk().AddConstant(value);
			if (index > 0xffff)
				Error("This script is too large: it has more than 65535 values in one function.");
			return static_cast<uint16_t>(index);
		}

		uint16_t NameConstant(const std::string& name)
		{
			return MakeConstant(Value::FromObject(m_VM.Intern(name)));
		}

		void EmitConstant(Value value)
		{
			EmitOp(OpCode::Constant);
			EmitU16(MakeConstant(value));
		}

		void EmitWithName(OpCode op, const std::string& name)
		{
			EmitOp(op);
			EmitU16(NameConstant(name));
		}

		size_t EmitJump(OpCode op)
		{
			EmitOp(op);
			EmitByte(0xff);
			EmitByte(0xff);
			return CurrentChunk().Size() - 2;
		}

		void PatchJump(size_t offset)
		{
			size_t jump = CurrentChunk().Size() - offset - 2;
			if (jump > 0xffff)
				Error("This block of code is too long to jump over. Split it into smaller functions.");
			std::vector<uint8_t>& code = CurrentChunk().GetCode();
			code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
			code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
		}

		void EmitLoop(size_t loopStart)
		{
			EmitOp(OpCode::Loop);
			size_t offset = CurrentChunk().Size() - loopStart + 2;
			if (offset > 0xffff)
				Error("This loop body is too long. Split it into smaller functions.");
			EmitU16(static_cast<uint16_t>(offset));
		}

		// ------------------------------------------------------------------ Scopes and variables

		void BeginScope() { m_State->ScopeDepth++; }

		void EndScope()
		{
			m_State->ScopeDepth--;
			std::vector<Local>& locals = m_State->Locals;
			while (!locals.empty() && locals.back().Depth > m_State->ScopeDepth)
			{
				if (locals.back().NameIndex < m_State->Fn->LocalNames.size())
					m_State->Fn->LocalNames[locals.back().NameIndex].EndIp = CurrentChunk().Size();
				EmitOp(locals.back().IsCaptured ? OpCode::CloseUpvalue : OpCode::Pop);
				locals.pop_back();
			}
		}

		static constexpr size_t c_NoToken = static_cast<size_t>(-1);
		static constexpr size_t c_LastConsumed = static_cast<size_t>(-2);

		// Declares a local variable for the value that is currently on top of the stack. 'at' is the token
		// that names it, for CompileOptions::NameUses: by default the last name read with ConsumeName
		// that is spelled the same and not yet noted; c_NoToken when no token names it here.
		int AddLocal(const std::string& name, size_t at = c_LastConsumed)
		{
			if (m_State->Locals.size() >= 256)
				Error("Too many variables in one function (the limit is 255). Split it into smaller functions.");
			m_State->Locals.push_back({ name, m_State->ScopeDepth, false });
			m_State->Locals.back().Id = m_NextLocalId++;
			NameLocal(m_State->Locals.size() - 1);
			if (!name.empty())
				NoteLocal(at == c_LastConsumed ? LastConsumed(name) : at, name, m_State->Locals.back().Id, true);
			return static_cast<int>(m_State->Locals.size()) - 1;
		}

		// The last token of the statement being compiled that ConsumeName read, is spelled 'name' and is
		// not yet noted, or c_NoToken.
		size_t LastConsumed(const std::string& name) const
		{
			for (auto it = m_ConsumedNames.rbegin(); it != m_ConsumedNames.rend() && *it >= m_StatementStart; ++it)
			{
				if (m_Tokens[*it].Lexeme == name && !m_NotedTokens.count(*it))
					return *it;
			}
			return c_NoToken;
		}

		// Records that the token at 'at' names the variable 'key' (see CompileOptions::NameUses). Only a
		// token spelled 'name' counts, so a wrong guess of where a name is notes nothing.
		void NoteName(size_t at, const std::string& name, const std::string& key, bool declares)
		{
			if (!m_Options.NameUses || at >= m_Tokens.size() || m_Tokens[at].Type != TokenType::Identifier ||
				m_Tokens[at].Lexeme != name || !m_NotedTokens.insert(at).second)
				return;
			const Token& token = m_Tokens[at];
			m_Options.NameUses->push_back({ token.Line, token.Column, static_cast<int>(token.Lexeme.size()), at, key, declares });
		}

		// The key is made only when someone listens, since this runs for every name a script uses.
		void NoteLocal(size_t at, const std::string& name, int id, bool declares)
		{
			if (m_Options.NameUses)
				NoteName(at, name, "L" + std::to_string(id), declares);
		}

		void NoteScriptName(size_t at, const std::string& name, bool declares)
		{
			if (m_Options.NameUses)
				NoteName(at, name, "G:" + name, declares);
		}

		// Records where a named local lives, for the debugger. It is visible from here on.
		void NameLocal(size_t slot)
		{
			Local& local = m_State->Locals[slot];
			if (local.Name.empty())
				return;
			local.NameIndex = m_State->Fn->LocalNames.size();
			m_State->Fn->LocalNames.push_back({ local.Name, static_cast<uint8_t>(slot), CurrentChunk().Size() });
		}

		static int ResolveLocal(FunctionState* state, const std::string& name)
		{
			for (int i = static_cast<int>(state->Locals.size()) - 1; i >= 0; i--)
			{
				if (!name.empty() && state->Locals[static_cast<size_t>(i)].Name == name)
					return i;
			}
			return -1;
		}

		int AddUpvalue(FunctionState* state, uint8_t index, bool isLocal, const std::string& name, int localId)
		{
			for (size_t i = 0; i < state->Upvalues.size(); i++)
			{
				if (state->Upvalues[i].Index == index && state->Upvalues[i].IsLocal == isLocal)
					return static_cast<int>(i);
			}
			if (state->Upvalues.size() >= 256)
				Error("Too many outside variables used by one function (the limit is 255).");
			state->Upvalues.push_back({ index, isLocal, localId });
			state->Fn->UpvalueNames.push_back(name);
			state->Fn->UpvalueCount = static_cast<int>(state->Upvalues.size());
			return static_cast<int>(state->Upvalues.size()) - 1;
		}

		int ResolveUpvalue(FunctionState* state, const std::string& name)
		{
			if (!state->Enclosing)
				return -1;

			int local = ResolveLocal(state->Enclosing, name);
			if (local != -1)
			{
				state->Enclosing->Locals[static_cast<size_t>(local)].IsCaptured = true;
				return AddUpvalue(state, static_cast<uint8_t>(local), true, name, state->Enclosing->Locals[static_cast<size_t>(local)].Id);
			}

			int upvalue = ResolveUpvalue(state->Enclosing, name);
			if (upvalue != -1)
				return AddUpvalue(state, static_cast<uint8_t>(upvalue), false, name,
					state->Enclosing->Upvalues[static_cast<size_t>(upvalue)].LocalId);
			return -1;
		}

		// True when an enclosing function has a local with this name (checked without capturing it).
		bool IsVisibleInEnclosing(const std::string& name) const
		{
			for (FunctionState* state = m_State->Enclosing; state; state = state->Enclosing)
			{
				if (state->Kind == ScopeKind::Script)
					continue;
				if (ResolveLocal(state, name) != -1)
					return true;
			}
			return false;
		}

		// 'at' is the token of the name, for CompileOptions::NameUses (c_NoToken when there is none).
		Target ResolveName(const std::string& name, size_t at = c_NoToken)
		{
			Target target;
			int local = ResolveLocal(m_State, name);
			if (local != -1)
			{
				target.Type = TargetType::Local;
				target.Slot = local;
				NoteLocal(at, name, m_State->Locals[static_cast<size_t>(local)].Id, false);
				return target;
			}
			int upvalue = ResolveUpvalue(m_State, name);
			if (upvalue != -1)
			{
				target.Type = TargetType::Upvalue;
				target.Slot = upvalue;
				NoteLocal(at, name, m_State->Upvalues[static_cast<size_t>(upvalue)].LocalId, false);
				return target;
			}
			target.Type = TargetType::Global;
			target.Name = NameConstant(name);
			NoteScriptName(at, name, false);
			return target;
		}

		void EmitGet(const Target& target)
		{
			switch (target.Type)
			{
				case TargetType::Local:     EmitOp(OpCode::GetLocal); EmitByte(static_cast<uint8_t>(target.Slot)); break;
				case TargetType::Upvalue:   EmitOp(OpCode::GetUpvalue); EmitByte(static_cast<uint8_t>(target.Slot)); break;
				case TargetType::Global:    EmitOp(OpCode::GetGlobal); EmitU16(target.Name); break;
				case TargetType::Property:  EmitOp(OpCode::GetProperty); EmitU16(target.Name); break;
				case TargetType::Index:     EmitOp(OpCode::GetIndex); break;
				case TargetType::Temporary: break;
			}
		}

		void EmitSet(const Target& target)
		{
			switch (target.Type)
			{
				case TargetType::Local:     EmitOp(OpCode::SetLocal); EmitByte(static_cast<uint8_t>(target.Slot)); break;
				case TargetType::Upvalue:   EmitOp(OpCode::SetUpvalue); EmitByte(static_cast<uint8_t>(target.Slot)); break;
				case TargetType::Global:    EmitOp(OpCode::SetGlobal); EmitU16(target.Name); break;
				case TargetType::Property:  EmitOp(OpCode::SetProperty); EmitU16(target.Name); break;
				case TargetType::Index:     EmitOp(OpCode::SetIndex); break;
				case TargetType::Temporary: Error("This cannot be changed. You can only change variables, properties and list items."); break;
			}
		}

		void EmitSelf()
		{
			EmitGet(ResolveName("self"));
		}

		// Parses something that can be changed: a variable, a property or a list item.
		// Emits the code for everything except the final read or write.
		TargetChain ParseTarget()
		{
			TargetChain chain;
			Target& target = chain.Final;
			if (Check(TokenType::Identifier))
			{
				const size_t at = m_Pos;
				std::string name = ConsumeName("Expected a variable name.");
				target = ResolveName(name, at);
			}
			else if (Match(TokenType::My))
			{
				EmitSelf();
				target.Type = TargetType::Property;
				target.Name = NameConstant(ConsumeMemberName());
			}
			else if (Match(TokenType::Self))
			{
				EmitSelf();
				target.Type = TargetType::Temporary;
			}
			else
			{
				if (Current().IsWord())
					ErrorAtCurrent("'" + std::string(Current().Lexeme) + "' is a ForgeScript keyword and cannot be used as a variable name.");
				ErrorAtCurrent("Expected a variable name here.");
			}

			while (Check(TokenType::Dot) || Check(TokenType::Possessive) || Check(TokenType::LeftBracket) || Check(TokenType::LeftParen))
			{
				bool isCall = Check(TokenType::LeftParen);
				if (isCall && !chain.WriteBacks.empty())
					ErrorAtCurrent("Store this result in a variable first, then change it.");

				// Keep the parent object on the stack so the value can be written back afterwards.
				if (!isCall && target.Type == TargetType::Property)
				{
					EmitOp(OpCode::Dup);
					EmitGet(target);
					chain.WriteBacks.push_back(target);
				}
				else if (!isCall && target.Type == TargetType::Index)
				{
					EmitOp(OpCode::Dup2);
					EmitGet(target);
					chain.WriteBacks.push_back(target);
				}
				else
				{
					EmitGet(target);
				}

				if (Match(TokenType::Dot) || Match(TokenType::Possessive))
				{
					target.Type = TargetType::Property;
					target.Name = NameConstant(ConsumeMemberName());
				}
				else if (Match(TokenType::LeftBracket))
				{
					PushStopWords(nullptr);
					Expression();
					PopStopWords();
					Consume(TokenType::RightBracket, "Expected ']' after the position.");
					target.Type = TargetType::Index;
				}
				else
				{
					Advance();
					uint8_t count = ArgumentList();
					EmitOp(OpCode::Call);
					EmitByte(count);
					target.Type = TargetType::Temporary;
				}
			}

			if (target.Type == TargetType::Temporary)
				Error("This cannot be changed. You can only change variables, properties and list items.");
			return chain;
		}

		// Before the final write: keep a copy of the object that is being changed.
		void BeginWriteBack(const TargetChain& chain)
		{
			if (chain.WriteBacks.empty())
				return;
			EmitOp(chain.Final.Type == TargetType::Index ? OpCode::Dup2 : OpCode::Dup);
		}

		// After the final write: store every changed vector back where it came from.
		void FinishWriteBack(const TargetChain& chain)
		{
			if (chain.WriteBacks.empty())
				return;
			if (chain.Final.Type == TargetType::Index)
				EmitOp(OpCode::Pop);
			for (auto it = chain.WriteBacks.rbegin(); it != chain.WriteBacks.rend(); ++it)
			{
				if (it->Type == TargetType::Property)
				{
					EmitOp(OpCode::WriteBackProperty);
					EmitU16(it->Name);
				}
				else
				{
					EmitOp(OpCode::WriteBackIndex);
				}
			}
			EmitOp(OpCode::Pop);
		}

		template<typename EmitValue>
		void EmitAssign(const TargetChain& chain, EmitValue emitValue)
		{
			BeginWriteBack(chain);
			emitValue();
			EmitSet(chain.Final);
			EmitOp(OpCode::Pop);
			FinishWriteBack(chain);
		}

		// Reads the target, applies an operator with a value, and writes the result back.
		template<typename EmitValue>
		void EmitCompound(const TargetChain& chain, EmitValue emitValue, OpCode op)
		{
			const Target& target = chain.Final;
			BeginWriteBack(chain);
			if (target.Type == TargetType::Property)
				EmitOp(OpCode::Dup);
			else if (target.Type == TargetType::Index)
				EmitOp(OpCode::Dup2);

			EmitGet(target);
			emitValue();
			EmitOp(op);
			EmitSet(target);
			EmitOp(OpCode::Pop);
			FinishWriteBack(chain);
		}

		// ------------------------------------------------------------------ Stop words

		void PushStopWords(const std::set<std::string>* words) { m_StopWords.push_back(words ? words : &s_NoStopWords); }
		void PopStopWords() { m_StopWords.pop_back(); }

		bool IsStopWord(const Token& token) const
		{
			const std::set<std::string>* words = m_StopWords.back();
			return !words->empty() && token.IsWord() && words->count(std::string(token.Lexeme)) > 0;
		}

		bool CheckOperator(TokenType type) const { return Check(type) && !IsStopWord(Current()); }

		// ------------------------------------------------------------------ Statements

		void Statement()
		{
			m_StatementStart = m_Pos;
			switch (Current().Type)
			{
				case TokenType::Set:       SetStatement(); break;
				case TokenType::Constant:  ConstantStatement(); break;
				case TokenType::Add:       AddOrTakeStatement(true); break;
				case TokenType::Take:      AddOrTakeStatement(false); break;
				case TokenType::Multiply:  MultiplyOrDivideStatement(true); break;
				case TokenType::Divide:    MultiplyOrDivideStatement(false); break;
				case TokenType::If:        IfStatement(); break;
				case TokenType::While:     WhileStatement(false); break;
				case TokenType::Until:     WhileStatement(true); break;
				case TokenType::Repeat:    RepeatStatement(); break;
				case TokenType::For:       ForStatement(); break;
				case TokenType::Stop:      LoopJumpStatement(true); break;
				case TokenType::Skip:      LoopJumpStatement(false); break;
				case TokenType::Function:
					if (Peek(1).IsWord() && Peek(1).Type != TokenType::Identifier && Peek(2).Type == TokenType::LeftParen)
						ErrorAt(Peek(1), "'" + std::string(Peek(1).Lexeme) + "' is a ForgeScript keyword and cannot be used as a function name.");
					if (Peek(1).Type == TokenType::Identifier)
						FunctionStatement();
					else
						ExpressionStatement();
					break;
				case TokenType::Phrase:    PhraseStatement(); break;
				case TokenType::Return:    ReturnStatement(); break;
				case TokenType::Kind:      KindStatement(); break;
				case TokenType::Try:       TryStatement(); break;
				case TokenType::Fail:      FailStatement(); break;
				case TokenType::When:      WhenStatement(); break;
				case TokenType::Trigger:   TriggerStatement(); break;
				case TokenType::Wait:      WaitStatement(); break;
				case TokenType::Start:     StartStatement(); break;
				case TokenType::Use:       UseStatement(); break;
				case TokenType::End:
					ErrorAtCurrent("This 'end' does not close anything. Check for an extra 'end'.");
				case TokenType::Else:
				case TokenType::Otherwise:
					ErrorAtCurrent("This '" + std::string(Current().Lexeme) + "' needs an 'if' before it.");
				case TokenType::Catch:
					ErrorAtCurrent("This 'catch' needs a 'try' before it.");
				case TokenType::Is:
					ErrorAtCurrent("A line can't start with 'is' here. Each 'is' choice goes inside a 'match', like: match weapon");
				default:
					if (MatchOpensBlock(m_Tokens, m_Pos))
						MatchStatement();
					else if (IsTimerAt(m_Tokens, m_Pos))
						TimerStatement();
					else if (IsListVerbAt(m_Pos))
						ListVerbStatement();
					else if (IsChoicesAt(m_Tokens, m_Pos))
						ChoicesStatement();
					else if (IsTestAt(m_Tokens, m_Pos))
						TestStatement();
					else if (IsExpectAt(m_Pos))
						ExpectStatement();
					else if (TryPhrase(true))
						EmitOp(OpCode::Pop);
					else if (MatchWithoutChoices())
					{
						// Skip the rest of it, so its 'otherwise' and 'end' aren't reported as stray too.
						m_ResumeAfter = SkipBlock(m_Pos + 1);
						ErrorAtCurrent("A 'match' needs at least one 'is' choice before 'otherwise' or 'end', like: "
							"match weapon / is \"sword\" then ... / otherwise ... / end");
					}
					else
						ExpressionStatement();
					break;
			}
			EndOfStatement();
		}

		// Parses statements until 'end', 'else', 'otherwise', 'catch' or the end of the file.
		void Block()
		{
			Nested nested(*this);
			SkipNewlines();
			while (!IsBlockEnd())
			{
				const size_t start = m_Pos;
				RecoverableUnit(*m_State, [this, start] { return EndOfUnit(start); }, [this] { Statement(); });
				SkipNewlines();
			}
		}

		void ScopedBlock()
		{
			BeginScope();
			Block();
			EndScope();
		}

		void ConsumeEnd(const Token& opener, const char* what)
		{
			if (Check(TokenType::End))
			{
				// Cheap test first (the opener's line starts at or before the opener), the token search only then.
				if (Current().Line > opener.Line && Current().Column < opener.Column)
				{
					const size_t openerIndex = IndexOfToken(opener);
					if (openerIndex < m_Tokens.size() && Current().Column < FirstOnLine(openerIndex).Column)
						m_MisplacedEndOpener = opener;
				}
				Advance();
				m_ClosingPos = m_Pos;
				m_ClosingLine = opener.Line;
				return;
			}
			FixMissingEnd(opener);
			ErrorAt(opener, std::string("This ") + what + " is missing its 'end'.");
		}

		void ExpressionStatement()
		{
			bool show = m_Options.Repl && m_State->Kind == ScopeKind::Script && m_State->ScopeDepth == 0;
			if (show)
				EmitWithName(OpCode::GetGlobal, "show");
			Expression();
			if (show)
			{
				EmitOp(OpCode::Call);
				EmitByte(1);
			}
			EmitOp(OpCode::Pop);
		}

		// set x, y to position: takes a list (or a vector) apart, one item per name. The value is worked
		// out first, so "set a, b to [b, a]" swaps them. Before, a comma after the name was always a mistake.
		void UnpackStatement(const std::vector<std::string>& names)
		{
			const size_t firstName = m_Pos;
			for (size_t i = 0; i < names.size(); i++)
			{
				for (size_t j = 0; j < i; j++)
				{
					if (names[i] == names[j])
						ErrorAtCurrent("'" + names[i] + "' is named twice; give each value its own name.");
				}
				Advance(); // the name
				if (i + 1 < names.size())
					Advance(); // ,
			}
			Consume(TokenType::To, "Expected 'to' after the names, like: set x, y to position");
			Expression();
			if (names.size() > 255)
				Error("At most 255 values can be taken apart at once.");
			EmitOp(OpCode::Unpack);
			EmitByte(static_cast<uint8_t>(names.size()));
			// The items are on the stack in order, so the last name takes the top one.
			for (size_t i = names.size(); i > 0; i--)
			{
				const std::string& name = names[i - 1];
				const size_t at = firstName + 2 * (i - 1);
				const bool scriptVariable = m_State->Kind == ScopeKind::Script && ResolveLocal(m_State, name) == -1;
				Target target;
				if (scriptVariable)
				{
					target.Type = TargetType::Global;
					target.Name = NameConstant(name);
					NoteScriptName(at, name, true);
				}
				else
				{
					target = ResolveName(name, at);
				}
				EmitSet(target);
				EmitOp(OpCode::Pop);
			}
		}

		void SetStatement()
		{
			Advance(); // set
			if (std::vector<std::string> names = UnpackNames(m_Pos); !names.empty())
			{
				UnpackStatement(names);
				return;
			}
			if (m_State->Kind == ScopeKind::Script && Check(TokenType::Identifier) && Peek(1).Type == TokenType::To)
			{
				std::string name(Current().Lexeme);
				if (ResolveLocal(m_State, name) == -1)
				{
					// A script level variable: "set score to 0".
					WarnIfHidesBuiltin(name);
					NoteScriptName(m_Pos, name, true);
					Advance();
					Advance();
					Expression();
					EmitWithName(OpCode::SetGlobal, name);
					EmitOp(OpCode::Pop);
					return;
				}
			}

			TargetChain target = ParseTarget();
			if (Check(TokenType::Equal))
			{
				FixTokens(Current(), Current(), "Change '=' to 'to'", "to");
				ErrorAtCurrent("Use 'to' instead of '=', like: set health to 100");
			}
			Consume(TokenType::To, "Expected 'to' after the name, like: set health to 100");
			EmitAssign(target, [&]() { Expression(); });
		}

		void ConstantStatement()
		{
			Token keyword = Current();
			Advance();
			if (m_State->Kind != ScopeKind::Script || m_State->ScopeDepth != 0)
				ErrorAt(keyword, "Constants can only be made at the top level of a script, not inside blocks or functions.");
			std::string name = ConsumeName("Expected a name for the constant.");
			if (!Match(TokenType::Is) && !Match(TokenType::To))
				ErrorAtCurrent("Expected 'is' after the constant name, like: constant MAX_HEALTH is 100");
			Expression();
			EmitWithName(OpCode::DefineConstant, name);
		}

		// add 5 to score  /  take 10 from health
		void AddOrTakeStatement(bool isAdd)
		{
			Advance();
			BeginScope();
			Expression();
			int valueSlot = AddLocal("");
			Consume(isAdd ? TokenType::To : TokenType::From,
				isAdd ? "Expected 'to' here, like: add 5 to score" : "Expected 'from' here, like: take 5 from health");
			TargetChain target = ParseTarget();
			EmitCompound(target, [&]() { EmitOp(OpCode::GetLocal); EmitByte(static_cast<uint8_t>(valueSlot)); },
				isAdd ? OpCode::AddTo : OpCode::TakeFrom);
			EndScope();
		}

		// multiply speed by 2  /  divide damage by 4
		void MultiplyOrDivideStatement(bool isMultiply)
		{
			Advance();
			TargetChain target = ParseTarget();
			Consume(TokenType::By, isMultiply ? "Expected 'by' here, like: multiply speed by 2" : "Expected 'by' here, like: divide damage by 2");
			EmitCompound(target, [&]() { Expression(); }, isMultiply ? OpCode::Multiply : OpCode::Divide);
		}

		void IfStatement()
		{
			Token opener = Current();
			Advance();
			std::vector<size_t> endJumps;
			while (true)
			{
				Expression();
				Match(TokenType::Then);
				size_t skip = EmitJump(OpCode::JumpIfFalse);
				EmitOp(OpCode::Pop);
				ScopedBlock();
				endJumps.push_back(EmitJump(OpCode::Jump));
				PatchJump(skip);
				EmitOp(OpCode::Pop);

				if (Match(TokenType::Else) || Match(TokenType::Otherwise))
				{
					if (Match(TokenType::If))
						continue;
					ScopedBlock();
				}
				break;
			}
			ConsumeEnd(opener, "'if'");
			for (size_t jump : endJumps)
				PatchJump(jump);
		}

		// "match weapon" followed by a line starting with 'otherwise' or 'end': a match with no 'is' choice.
		// Only asked once no phrase took the line, so a script's own "phrase match (x)" keeps working; and
		// 'match' followed by a plain value can't be anything else ('match(x)', 'match[1]' and
		// 'match - 1' are left alone).
		bool MatchWithoutChoices() const { return MatchWithoutChoicesAt(m_Pos); }


		// A timer ('every 3 seconds', 'after 2 seconds') starts here, unless the script's phrases use its word.
		bool IsTimerAt(const std::vector<Token>& tokens, size_t index) const
		{
			if (!TimerOpensBlock(tokens, index))
				return false;
			if (&tokens != &m_Tokens)
				return true;
			return tokens[index].Lexeme == "every" ? !m_EveryIsPhrase : !m_AfterIsPhrase;
		}

		// 'sort items', 'shuffle my cards', 'reverse items': a list verb with a variable or field after it.
		// Neither word is reserved, and such a line was always a mistake, so names, calls ('sort(x)') and a
		// script's own phrases starting with the word keep their meaning.
		// 'choices State are idle, running' starts a statement: the word, a name, then 'are'. None of
		// them are reserved, and the line was an error before; a script phrase starting with it wins.
		bool IsChoicesAt(const std::vector<Token>& tokens, size_t index) const
		{
			if (m_ChoicesIsPhrase || index + 2 >= tokens.size())
				return false;
			const Token& word = tokens[index];
			const Token& are = tokens[index + 2];
			const bool startsLine = index == 0 || tokens[index - 1].Type == TokenType::Newline;
			return startsLine && word.Type == TokenType::Identifier && word.Lexeme == "choices" &&
				tokens[index + 1].Type == TokenType::Identifier && are.Type == TokenType::Identifier && are.Lexeme == "are";
		}

		// Notes every choice set before compiling, so 'State.idel' is caught above the line that makes it.
		void PrescanChoices()
		{
			for (size_t i = 0; i < m_Tokens.size(); i++)
			{
				if (!IsChoicesAt(m_Tokens, i))
					continue;
				const std::string set(m_Tokens[i + 1].Lexeme);
				std::vector<std::string>& names = m_Choices[set];
				names.clear();
				for (size_t j = i + 3; j < m_Tokens.size() && m_Tokens[j].Type != TokenType::Newline && m_Tokens[j].Type != TokenType::Eof; j++)
				{
					if (m_Tokens[j].Type == TokenType::Identifier)
						names.emplace_back(m_Tokens[j].Lexeme);
				}
				m_ChoiceSets[set] = std::unordered_set<std::string>(names.begin(), names.end());
			}
		}

		// choices State are idle, running and jumping: a constant map from each name to itself as text.
		void ChoicesStatement()
		{
			const Token keyword = Current();
			Advance(); // choices
			if (m_State->Kind != ScopeKind::Script || m_State->ScopeDepth != 0)
				ErrorAt(keyword, "Write choices at the top level of the script, not inside a function, kind or block.");
			const std::string name(Current().Lexeme);
			Advance(); // the name
			Advance(); // are
			std::unordered_set<std::string> names;
			EmitOp(OpCode::NewMap);
			while (true)
			{
				if (!Check(TokenType::Identifier))
					ErrorAtCurrent("Each choice is a name, like: choices " + name + " are idle, running");
				const std::string choice(Current().Lexeme);
				if (!names.insert(choice).second)
					ErrorAtCurrent("'" + choice + "' is in " + name + "'s choices twice.");
				Advance();
				Value text = Value::FromObject(m_VM.Intern(choice));
				EmitConstant(text);
				EmitConstant(text);
				EmitOp(OpCode::MapSet);
				if (!Match(TokenType::Comma) && !Match(TokenType::And))
					break;
			}
			EmitWithName(OpCode::DefineConstant, name);
		}

		// 'State.idel' where State is this script's choice set: a compile error that lists the choices.
		void CheckChoiceName(const std::string& set)
		{
			auto found = m_Choices.find(set);
			if (found == m_Choices.end() || !(Check(TokenType::Dot) || Check(TokenType::Possessive)) || Peek(1).Type != TokenType::Identifier ||
				Peek(2).Type == TokenType::LeftParen)
				return;
			const std::string member(Peek(1).Lexeme);
			const std::vector<std::string>& names = found->second;
			if (m_ChoiceSets[set].count(member) || member == "length" || member == "size" || member == "count")
				return;
			std::string list;
			std::vector<std::string_view> candidates;
			for (const std::string& choice : names)
			{
				list += (list.empty() ? "" : ", ") + choice;
				candidates.push_back(choice);
			}
			const std::string closest = ClosestName(member, candidates);
			if (!closest.empty())
				FixTokens(Peek(1), Peek(1), "Change to '" + closest + "'", closest);
			ErrorAt(Peek(1), set + " has no choice '" + member + "'." + DidYouMean(member, candidates) + " Its choices are: " + list + ".");
		}

		bool IsTestAt(const std::vector<Token>& tokens, size_t index) const
		{
			return !m_TestIsPhrase && TestOpensBlock(tokens, index);
		}

		// 'expect' and then a value that can't follow a name ('expect x is 5', 'expect not done'): a line
		// that was a mistake. 'expect(x)', 'expect - 1' and 'expect[1]' keep their meaning.
		bool IsExpectAt(size_t index) const
		{
			if (m_ExpectIsPhrase || index + 1 >= m_Tokens.size())
				return false;
			const Token& word = m_Tokens[index];
			if (word.Type != TokenType::Identifier || word.Lexeme != "expect")
				return false;
			switch (m_Tokens[index + 1].Type)
			{
				case TokenType::Number: case TokenType::String: case TokenType::Interpolation: case TokenType::Identifier:
				case TokenType::True: case TokenType::False: case TokenType::Yes: case TokenType::No: case TokenType::Nothing:
				case TokenType::Not: case TokenType::My: case TokenType::Self: case TokenType::New:
					return true;
				default:
					return false;
			}
		}

		// The script's text from tokens[first] to tokens[last], as written.
		std::string SourceText(size_t first, size_t last) const
		{
			const std::string_view a = m_Tokens[first].Lexeme;
			const std::string_view b = m_Tokens[last].Lexeme;
			if (a.data() && b.data() && b.data() + b.size() > a.data())
				return std::string(a.data(), static_cast<size_t>(b.data() + b.size() - a.data()));
			return std::string(a);
		}

		// test "double works" ... end: a function kept by the module, run only by VM::RunTest
		// ('forgescript --test'), never when the script runs.
		void TestStatement()
		{
			const Token opener = Current();
			if (m_State->Kind != ScopeKind::Script || m_State->ScopeDepth != 0)
				ErrorAt(opener, "Write tests at the top level of the script, not inside a function, kind or block.");
			Advance(); // test
			const std::string name = Current().Text;
			Advance();
			Match(TokenType::Do);
			FunctionBody("test \"" + name + "\"", ScopeKind::Function, {}, opener, "'test'");
			EmitWithName(OpCode::AddTest, name);
		}

		// expect double(2) is 4: an error naming what was expected and what was found, unless it's true.
		// For one comparison, the value on its left is kept to show in the message.
		void ExpectStatement()
		{
			Advance(); // expect
			const size_t first = m_Pos;
			size_t last = first;
			bool single = false;
			size_t leftEnd = first;
			{
				// One comparison at the top: no 'and', 'or' or 'not' outside brackets, and an operator.
				int depth = 0;
				bool logic = m_Tokens[first].Type == TokenType::Not;
				for (size_t i = first; i < m_Tokens.size(); i++)
				{
					const TokenType type = m_Tokens[i].Type;
					if (type == TokenType::Newline || type == TokenType::Eof)
						break;
					last = i;
					const bool afterBrace = !m_Tokens[i].Lexeme.empty() && m_Tokens[i].Lexeme.front() == '}';
					if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace ||
						(type == TokenType::Interpolation && !afterBrace))
						depth++;
					else if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace ||
						(type == TokenType::String && afterBrace))
						depth--;
					else if (depth == 0 && (type == TokenType::And || type == TokenType::Or))
						logic = true;
					else if (depth == 0 && !single && i > first &&
						(type == TokenType::Is || type == TokenType::Contains || type == TokenType::EqualEqual || type == TokenType::BangEqual ||
						 type == TokenType::Less || type == TokenType::LessEqual || type == TokenType::Greater || type == TokenType::GreaterEqual))
					{
						single = true;
						leftEnd = i - 1;
					}
				}
				single = single && !logic;
			}

			const uint16_t text = NameConstant(SourceText(first, last));
			if (single)
			{
				BeginScope();
				Term();
				const int left = AddLocal("");
				EmitOp(OpCode::GetLocal);
				EmitByte(static_cast<uint8_t>(left));
				ComparisonRest();
				EmitOp(OpCode::Expect);
				EmitU16(text);
				EmitU16(NameConstant(SourceText(first, leftEnd)));
				EmitByte(1);
				EndScope();
			}
			else
			{
				Expression();
				EmitOp(OpCode::Expect);
				EmitU16(text);
				EmitU16(text);
				EmitByte(0);
			}
		}

		bool IsListVerbAt(size_t index) const
		{
			const Token& token = m_Tokens[index];
			if (token.Type != TokenType::Identifier || (token.Lexeme != "sort" && token.Lexeme != "shuffle" && token.Lexeme != "reverse"))
				return false;
			if (m_ListVerbPhrases.count(std::string(token.Lexeme)) > 0 || index + 1 >= m_Tokens.size())
				return false;
			const TokenType next = m_Tokens[index + 1].Type;
			return next == TokenType::Identifier || next == TokenType::My || next == TokenType::Self;
		}

		// The names a 'wait for' line gives the event's values ('wait for hit with damage, source'), when the
		// statement at tokens[index] is one.
		std::vector<std::string> WaitForNames(size_t index) const
		{
			std::vector<std::string> names;
			if (index + 1 >= m_Tokens.size() || m_Tokens[index].Type != TokenType::Wait || m_Tokens[index + 1].Type != TokenType::For)
				return names;
			size_t i = index + 2;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Newline && m_Tokens[i].Type != TokenType::Eof &&
				m_Tokens[i].Type != TokenType::With && m_Tokens[i].Type != TokenType::By)
				i++;
			if (i >= m_Tokens.size() || (m_Tokens[i].Type != TokenType::With && m_Tokens[i].Type != TokenType::By))
				return names;
			// The names, separated by ',' or 'and'.
			i++;
			while (i < m_Tokens.size() && m_Tokens[i].Type == TokenType::Identifier)
			{
				names.emplace_back(m_Tokens[i].Lexeme);
				if (i + 1 >= m_Tokens.size() || (m_Tokens[i + 1].Type != TokenType::Comma && m_Tokens[i + 1].Type != TokenType::And))
					break;
				i += 2;
			}
			return names;
		}

		bool MatchWithoutChoicesAt(size_t index) const
		{
			if (index + 1 >= m_Tokens.size() || m_Tokens[index].Type != TokenType::Identifier || m_Tokens[index].Lexeme != "match")
				return false;
			if (index > 0)
			{
				switch (m_Tokens[index - 1].Type)
				{
					case TokenType::Newline: case TokenType::Then: case TokenType::Do:
					case TokenType::Else: case TokenType::Otherwise:
						break;
					default:
						return false;
				}
			}
			switch (m_Tokens[index + 1].Type)
			{
				case TokenType::Identifier: case TokenType::Number: case TokenType::String:
				case TokenType::True: case TokenType::False: case TokenType::Yes: case TokenType::No:
				case TokenType::Nothing:
					break;
				default:
					return false;
			}
			size_t i = index + 1;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Newline && m_Tokens[i].Type != TokenType::Eof)
				i++;
			if (i + 1 >= m_Tokens.size() || m_Tokens[i].Type != TokenType::Newline)
				return false;
			const TokenType next = m_Tokens[i + 1].Type;
			return next == TokenType::Otherwise || next == TokenType::Else || next == TokenType::End;
		}

		// match weapon
		//     is "sword" then ...
		//     is "bow", "crossbow" then ...    (any one of the values)
		//     is above 100 then ...            (any comparison 'is' allows)
		//     otherwise ...
		// end
		// The value is worked out once; the first choice that fits runs, and nothing runs when none fits
		// and there is no 'otherwise'.
		// For the missing-choice warning: whether the case at the current token is 'Set.choice' and
		// nothing more, of the same set as the cases before it.
		void NoteMatchChoice(std::string& choiceSet, bool& allChoices, std::vector<std::string>& named)
		{
			if (!allChoices)
				return;
			const TokenType after = Peek(3).Type;
			const bool isChoice = Check(TokenType::Identifier) && m_Choices.count(std::string(Current().Lexeme)) &&
				ResolveLocal(m_State, std::string(Current().Lexeme)) == -1 && Peek(1).Type == TokenType::Dot &&
				Peek(2).Type == TokenType::Identifier &&
				(after == TokenType::Comma || after == TokenType::Then || after == TokenType::Newline);
			const std::string set = isChoice ? std::string(Current().Lexeme) : std::string();
			if (!isChoice || (!choiceSet.empty() && set != choiceSet))
			{
				allChoices = false;
				return;
			}
			choiceSet = set;
			named.emplace_back(Peek(2).Lexeme);
		}

		void MatchStatement()
		{
			Token opener = Current();
			Advance(); // match
			BeginScope();
			Expression();
			const int subject = AddLocal("");
			SkipNewlines();

			struct MatchDepth
			{
				int& Depth;
				explicit MatchDepth(int& depth) : Depth(depth) { Depth++; }
				~MatchDepth() { Depth--; }
			} inMatch(m_MatchDepth);

			std::vector<size_t> endJumps;
			// Whether every case is a choice of one set ('is State.idle'), and which choices they name.
			std::string choiceSet;
			bool allChoices = true;
			std::vector<std::string> named;
			while (Check(TokenType::Is))
			{
				Advance(); // is
				std::vector<size_t> fits;
				while (true)
				{
					NoteMatchChoice(choiceSet, allChoices, named);
					EmitOp(OpCode::GetLocal);
					EmitByte(static_cast<uint8_t>(subject));
					IsTail();
					if (!Match(TokenType::Comma))
						break;
					fits.push_back(EmitJump(OpCode::JumpIfTrue));
					EmitOp(OpCode::Pop);
				}
				for (size_t jump : fits)
					PatchJump(jump);
				Match(TokenType::Then);

				size_t skip = EmitJump(OpCode::JumpIfFalse);
				EmitOp(OpCode::Pop);
				ScopedBlock();
				endJumps.push_back(EmitJump(OpCode::Jump));
				PatchJump(skip);
				EmitOp(OpCode::Pop);
			}

			bool hasOtherwise = false;
			if (Match(TokenType::Otherwise) || Match(TokenType::Else))
			{
				hasOtherwise = true;
				ScopedBlock();
				if (Check(TokenType::Is))
					ErrorAtCurrent("Put 'otherwise' last in a 'match', after every 'is'.");
			}
			if (!hasOtherwise && allChoices && !choiceSet.empty())
			{
				const std::unordered_set<std::string> covered(named.begin(), named.end());
				for (const std::string& choice : m_Choices[choiceSet])
				{
					if (!covered.count(choice))
						Warn(opener, "This match has no case for " + choiceSet + "." + choice + ", and no otherwise.");
				}
			}
			ConsumeEnd(opener, "'match'");
			for (size_t jump : endJumps)
				PatchJump(jump);
			EndScope();
		}

		// --- Warnings: only collected for checkers (CompileOptions::Warnings); they never change the code.

		void Warn(const Token& at, const std::string& message)
		{
			if (!m_Options.Warnings)
				return;
			// A checker may compile a part twice while it recovers from a mistake: keep each warning once.
			for (const CompileError& existing : *m_Options.Warnings)
			{
				if (existing.Line == at.Line && existing.Column == at.Column && existing.Message == message)
					return;
			}
			CompileError warning;
			warning.Message = message;
			warning.File = m_FileName;
			warning.Line = at.Line;
			warning.Column = at.Column;
			m_Options.Warnings->push_back(std::move(warning));
		}

		// The type of a value that is written out plainly from tokens[first] to tokens[last] (a number,
		// text without '{ }', true/false/yes/no, nothing, a list or a map), or "" for anything else.
		std::string PlainValueType(size_t first, size_t last) const
		{
			const TokenType type = m_Tokens[first].Type;
			if (first == last)
			{
				switch (type)
				{
					case TokenType::Number: return "number";
					case TokenType::String: return "text";
					case TokenType::True: case TokenType::False: case TokenType::Yes: case TokenType::No: return "boolean";
					case TokenType::Nothing: return "nothing";
					default: return {};
				}
			}
			if (type == TokenType::Minus && last == first + 1 && m_Tokens[last].Type == TokenType::Number)
				return "number";
			if ((type == TokenType::LeftBracket && m_Tokens[last].Type == TokenType::RightBracket) ||
				(type == TokenType::LeftBrace && m_Tokens[last].Type == TokenType::RightBrace))
			{
				// Only when the closing bracket closes the opening one: '[1] + [2]' isn't one list.
				int depth = 0;
				for (size_t i = first; i <= last; i++)
				{
					const TokenType t = m_Tokens[i].Type;
					if (t == TokenType::LeftBracket || t == TokenType::LeftBrace || t == TokenType::LeftParen)
						depth++;
					else if (t == TokenType::RightBracket || t == TokenType::RightBrace || t == TokenType::RightParen)
						depth--;
					if (depth == 0 && i < last)
						return {};
				}
				return type == TokenType::LeftBracket ? "list" : "map";
			}
			return {};
		}

		// Notes the plain values of the call whose '(' is tokens[open], for WarnTypesOfCalls.
		void NoteCallValues(const std::string& function, size_t open)
		{
			NotedCall call;
			call.Function = function;
			size_t position = 0;
			size_t start = open + 1;
			int depth = 0;
			for (size_t i = open + 1; i < m_Tokens.size(); i++)
			{
				const TokenType type = m_Tokens[i].Type;
				const bool afterBrace = !m_Tokens[i].Lexeme.empty() && m_Tokens[i].Lexeme.front() == '}';
				if (type == TokenType::Newline || type == TokenType::Eof)
					return;
				if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace ||
					(type == TokenType::Interpolation && !afterBrace))
				{
					depth++;
					continue;
				}
				const bool closes = type == TokenType::RightParen && depth == 0;
				if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace ||
					(type == TokenType::String && afterBrace))
				{
					if (!closes)
					{
						depth--;
						continue;
					}
				}
				if (depth > 0 || (type != TokenType::Comma && !closes))
					continue;
				if (i > start)
				{
					CallValue value;
					size_t first = start;
					if (m_Tokens[first].Type == TokenType::Identifier && first + 1 < i && m_Tokens[first + 1].Type == TokenType::Colon)
					{
						value.Name = std::string(m_Tokens[first].Lexeme);
						first += 2;
					}
					value.Position = position++;
					if (first < i)
					{
						value.Type = PlainValueType(first, i - 1);
						value.Token = first;
						if (!value.Type.empty())
							call.Values.push_back(value);
					}
				}
				start = i + 1;
				if (closes)
					break;
			}
			if (!call.Values.empty())
				m_NotedCalls.push_back(std::move(call));
		}

		// 'heal("ten")' where heal is 'function heal(amount as number)': a warning on the value. Only
		// plain values are compared, so a warning is never a guess.
		void WarnTypesOfCalls()
		{
			for (const NotedCall& call : m_NotedCalls)
			{
				auto found = m_FunctionTypes.find(call.Function);
				if (found == m_FunctionTypes.end())
					continue;
				const FunctionTypes& function = found->second;
				for (const CallValue& value : call.Values)
				{
					size_t index = value.Position;
					if (!value.Name.empty())
					{
						auto named = std::find(function.Params.begin(), function.Params.end(), value.Name);
						if (named == function.Params.end())
							continue;
						index = static_cast<size_t>(named - function.Params.begin());
					}
					if (index >= function.Types.size() || function.Types[index].empty() || function.Types[index] == value.Type)
						continue;
					const std::string& wanted = function.Types[index];
					const std::string& param = function.Params[index];
					auto article = [](const std::string& word)
					{
						if (word == "text" || word == "nothing")
							return word;
						return std::string(std::string("aeiouAEIOU").find(word[0]) != std::string::npos ? "an " : "a ") + word;
					};
					Warn(m_Tokens[value.Token], "'" + call.Function + "' takes " + article(wanted) + " for '" + param + "' (" + param +
						" as " + wanted + "), but this is " + article(value.Type) + ".");
				}
			}
		}

		// Whether the tokens from 'begin' to 'end' (a loop's body) may wait or leave the loop: a 'wait',
		// 'stop', 'return' or 'fail', or a call that might wait (the script's own functions, phrases and
		// methods). Built-in and engine functions never wait. Used only for a warning, so when in doubt
		// it says yes.
		bool MayPauseOrLeave(size_t begin, size_t end) const
		{
			for (size_t i = begin; i < end && i < m_Tokens.size(); i++)
			{
				const Token& token = m_Tokens[i];
				switch (token.Type)
				{
					case TokenType::Wait: case TokenType::Stop: case TokenType::Return: case TokenType::Fail:
						return true;
					case TokenType::Identifier:
					{
						std::string name(token.Lexeme);
						if (m_ScriptPhraseWords.count(name) > 0)
							return true;
						if (i + 1 < m_Tokens.size() && m_Tokens[i + 1].Type == TokenType::LeftParen)
						{
							bool method = i > 0 && (m_Tokens[i - 1].Type == TokenType::Dot || m_Tokens[i - 1].Type == TokenType::Possessive);
							if (method || !IsBuiltinFunction(name))
								return true;
						}
						break;
					}
					default:
						break;
				}
			}
			return false;
		}

		bool IsBuiltinFunction(const std::string& name) const
		{
			if (m_VM.HasSharedGlobal(name))
				return true;
			const std::vector<std::string>* more = m_Options.BuiltinFunctions;
			return more && std::find(more->begin(), more->end(), name) != more->end();
		}

		// The token where a value named like a built-in gets its name: the nearest use of the name before
		// 'index' that isn't a call, or else the first after.
		const Token* NamingToken(const std::string& name, size_t index) const
		{
			const Token* before = nullptr;
			const Token* after = nullptr;
			for (size_t i = 0; i + 1 < m_Tokens.size(); i++)
			{
				const Token& token = m_Tokens[i];
				if (token.Type != TokenType::Identifier || token.Lexeme != name || m_Tokens[i + 1].Type == TokenType::LeftParen)
					continue;
				if (i <= index)
					before = &token;
				else if (!after)
					after = &token;
			}
			return before ? before : after;
		}

		void WarnHidesBuiltin(const Token& at, const std::string& name, int callLine)
		{
			Warn(at, "Naming a value '" + name + "' hides the built-in function '" + name + "', so " + name +
				"(...) on line " + std::to_string(callLine) + " won't call it. Pick another name, like '" + name + "_value'.");
		}

		// A script variable named like a built-in ("set max to 10") hides it everywhere in the script, so it
		// is warned when the script calls that built-in anywhere. A game's "set round to 1" with no
		// round(...) stays quiet.
		void WarnIfHidesBuiltin(const std::string& name)
		{
			if (!m_Options.Warnings || name.empty() || !IsBuiltinFunction(name))
				return;
			for (size_t i = 0; i + 1 < m_Tokens.size(); i++)
			{
				if (m_Tokens[i].Type == TokenType::Identifier && m_Tokens[i].Lexeme == name && m_Tokens[i + 1].Type == TokenType::LeftParen)
				{
					if (const Token* named = NamingToken(name, m_Pos))
						WarnHidesBuiltin(*named, name, m_Tokens[i].Line);
					return;
				}
			}
		}

		// A call like max(a, b) that finds a function's own value or parameter named max, not the built-in.
		void WarnIfCallFindsOwnValue(const std::string& name, const Target& target, size_t nameIndex)
		{
			if (!m_Options.Warnings || (target.Type != TargetType::Local && target.Type != TargetType::Upvalue) ||
				!Check(TokenType::LeftParen) || !IsBuiltinFunction(name))
				return;
			if (const Token* named = NamingToken(name, nameIndex))
				WarnHidesBuiltin(*named, name, m_Tokens[nameIndex].Line);
		}

		// "show (pause_menu)": the space doesn't make it the phrase, so it calls the show function. Not
		// warned when the script makes a function of that name itself, since then the call is meant.
		void WarnIfBracketsHidePhrase(const std::string& word)
		{
			const Token& open = Peek(1);
			if (!m_Options.Warnings || open.Type != TokenType::LeftParen || open.Line != Current().Line ||
				open.Column <= Current().Column + static_cast<int>(Current().Lexeme.size()))
				return;
			for (size_t i = 0; i + 1 < m_Tokens.size(); i++)
			{
				if (m_Tokens[i].Type == TokenType::Function && m_Tokens[i + 1].Lexeme == word)
					return;
			}
			std::string example = word + " the_widget";
			if (Peek(2).Type == TokenType::Identifier && Peek(3).Type == TokenType::RightParen)
				example = word + " " + std::string(Peek(2).Lexeme);
			Warn(Current(), "With brackets, '" + word + " (...)' is a call to a function named '" + word + "', not the '" + word +
				"' phrase. To use the phrase, leave the brackets out, like: " + example);
		}

		void PushLoop(size_t continueTarget, bool continueKnown)
		{
			LoopInfo loop;
			loop.ContinueTarget = continueTarget;
			loop.ContinueKnown = continueKnown;
			loop.ScopeDepth = m_State->ScopeDepth;
			loop.TryDepth = m_State->TryDepth;
			m_State->Loops.push_back(std::move(loop));
		}

		void PatchContinues(LoopInfo& loop)
		{
			for (size_t jump : loop.ContinueJumps)
				PatchJump(jump);
			loop.ContinueJumps.clear();
		}

		void PopLoop()
		{
			LoopInfo& loop = m_State->Loops.back();
			for (size_t jump : loop.BreakJumps)
				PatchJump(jump);
			m_State->Loops.pop_back();
		}

		void WhileStatement(bool isUntil)
		{
			Token opener = Current();
			Advance();
			size_t loopStart = CurrentChunk().Size();
			Expression();
			Match(TokenType::Do);
			size_t exit = EmitJump(isUntil ? OpCode::JumpIfTrue : OpCode::JumpIfFalse);
			EmitOp(OpCode::Pop);
			PushLoop(loopStart, true);
			ScopedBlock();
			EmitLoop(loopStart);
			PatchJump(exit);
			EmitOp(OpCode::Pop);
			PopLoop();
			ConsumeEnd(opener, isUntil ? "'until' loop" : "'while' loop");
		}

		void RepeatStatement()
		{
			Token opener = Current();
			Advance();

			if (MatchWord("forever"))
			{
				Match(TokenType::Do);
				size_t loopStart = CurrentChunk().Size();
				size_t bodyStart = m_Pos;
				PushLoop(loopStart, true);
				ScopedBlock();
				EmitLoop(loopStart);
				PopLoop();
				if (m_Options.Warnings && !MayPauseOrLeave(bodyStart, m_Pos))
					Warn(opener, "This 'repeat forever' loop never waits, so nothing else in the game can run while it "
						"does. Add a 'wait' inside it, like: wait 1 frame");
				ConsumeEnd(opener, "'repeat' loop");
				return;
			}

			if (Check(TokenType::While) || Check(TokenType::Until))
			{
				const Token& repeatWord = Previous();
				if (repeatWord.Line == Current().Line)
				{
					m_Fix.FixTitle = "Remove 'repeat'";
					m_Fix.FixLine = m_Fix.FixEndLine = repeatWord.Line;
					m_Fix.FixColumn = repeatWord.Column;
					m_Fix.FixEndColumn = Current().Column;
				}
				ErrorAtCurrent("Write 'while ... do' or 'until ... do' without 'repeat' in front.");
			}

			BeginScope();
			Expression();
			int countSlot = AddLocal("");
			if (!MatchWord("times") && !MatchWord("time"))
				ErrorAtCurrent("Expected 'times' here, like: repeat 3 times");
			Match(TokenType::Do);
			EmitConstant(Value::FromNumber(0));
			AddLocal("");

			size_t loopStart = CurrentChunk().Size();
			EmitOp(OpCode::RepeatCheck);
			EmitByte(static_cast<uint8_t>(countSlot));
			size_t exit = CurrentChunk().Size();
			EmitByte(0xff);
			EmitByte(0xff);

			PushLoop(loopStart, true);
			ScopedBlock();
			EmitLoop(loopStart);
			PatchJump(exit);
			PopLoop();
			ConsumeEnd(opener, "'repeat' loop");
			EndScope();
		}

		void ForStatement()
		{
			Token opener = Current();
			Advance();
			Match(TokenType::Each);

			std::string first = ConsumeName("Expected a name for each item, like: for each enemy in enemies");
			if (Match(TokenType::From))
			{
				ForRangeLoop(opener, first);
				return;
			}

			std::string second;
			if (Match(TokenType::Comma))
				second = ConsumeName("Expected a second name after ','.");
			Consume(TokenType::In, "Expected 'in' here, like: for each enemy in enemies");

			BeginScope();
			Expression();
			int iterSlot = AddLocal("");
			EmitConstant(Value::FromNumber(0));
			AddLocal("");
			Match(TokenType::Do);

			size_t loopStart = CurrentChunk().Size();
			EmitOp(OpCode::IterNext);
			EmitByte(static_cast<uint8_t>(iterSlot));
			size_t exit = CurrentChunk().Size();
			EmitByte(0xff);
			EmitByte(0xff);

			PushLoop(loopStart, true);
			BeginScope();
			AddLocal(first);
			AddLocal(second);
			Block();
			EndScope();
			EmitLoop(loopStart);
			PatchJump(exit);
			PopLoop();
			ConsumeEnd(opener, "'for each' loop");
			EndScope();
		}

		// for i from 1 to 10 [by 2] do
		void ForRangeLoop(const Token& opener, const std::string& name)
		{
			BeginScope();
			Expression();
			int counterSlot = AddLocal("");
			Consume(TokenType::To, "Expected 'to' here, like: for i from 1 to 10");
			Expression();
			AddLocal("");
			if (Match(TokenType::By))
				Expression();
			else
				EmitOp(OpCode::Nothing);
			AddLocal("");
			Match(TokenType::Do);

			EmitOp(OpCode::ForPrepare);
			EmitByte(static_cast<uint8_t>(counterSlot));

			size_t loopStart = CurrentChunk().Size();
			EmitOp(OpCode::ForCheck);
			EmitByte(static_cast<uint8_t>(counterSlot));
			size_t exit = CurrentChunk().Size();
			EmitByte(0xff);
			EmitByte(0xff);

			PushLoop(0, false);
			BeginScope();
			EmitOp(OpCode::GetLocal);
			EmitByte(static_cast<uint8_t>(counterSlot));
			AddLocal(name);
			Block();
			EndScope();

			PatchContinues(m_State->Loops.back());
			EmitOp(OpCode::ForStep);
			EmitByte(static_cast<uint8_t>(counterSlot));
			EmitLoop(loopStart);
			PatchJump(exit);
			PopLoop();
			ConsumeEnd(opener, "'for' loop");
			EndScope();
		}

		void LoopJumpStatement(bool isBreak)
		{
			Token keyword = Current();
			Advance();
			if (m_State->Loops.empty())
				ErrorAt(keyword, isBreak ? "'stop' can only be used inside a loop." : "'skip' can only be used inside a loop.");

			// Each finally between here and the loop runs first, innermost first.
			int depth = m_State->TryDepth;
			const std::vector<FinallyContext> contexts = m_State->Finally;
			const size_t loopCount = m_State->Loops.size();
			for (auto it = contexts.rbegin(); it != contexts.rend() && it->LoopCount >= loopCount; ++it)
			{
				for (; depth > it->TryDepth; depth--)
					EmitOp(OpCode::TryEnd);
				EmitFinallyCopy(*it);
			}
			LoopInfo& loop = m_State->Loops.back();
			for (; depth > loop.TryDepth; depth--)
				EmitOp(OpCode::TryEnd);

			for (int i = static_cast<int>(m_State->Locals.size()) - 1; i >= 0; i--)
			{
				const Local& local = m_State->Locals[static_cast<size_t>(i)];
				if (local.Depth <= loop.ScopeDepth)
					break;
				EmitOp(local.IsCaptured ? OpCode::CloseUpvalue : OpCode::Pop);
			}

			if (isBreak)
				loop.BreakJumps.push_back(EmitJump(OpCode::Jump));
			else if (loop.ContinueKnown)
				EmitLoop(loop.ContinueTarget);
			else
				loop.ContinueJumps.push_back(EmitJump(OpCode::Jump));
		}

		// Compiles a parameter list and body into a function and emits a closure for it.
		void FunctionBody(const std::string& name, ScopeKind kind, const std::vector<std::string>& params, const Token& opener, const char* what,
			const std::string& eventSource = {}, const std::vector<DefaultValue>& defaults = {}, const std::function<void()>& body = {})
		{
			// The parameter list just read ended with 'name...' (read before the body, which may hold more).
			const bool rest = std::exchange(m_RestParam, false);
			Function* fn = m_VM.NewFunction();
			fn->Name = m_VM.Intern(name);
			fn->EventSource = eventSource;
			fn->HasRest = rest && !params.empty();
			fn->Owner = m_Module;
			fn->IsMethod = kind == ScopeKind::Method || kind == ScopeKind::FieldInit;
			fn->Arity = static_cast<int>(params.size());

			FunctionState state;
			state.Enclosing = m_State;
			state.Fn = fn;
			state.Kind = kind;
			m_State = &state;

			BeginScope();
			state.Locals.push_back({ fn->IsMethod ? "self" : "", state.ScopeDepth, false });
			NameLocal(0);
			for (const std::string& param : params)
			{
				if (ResolveLocal(m_State, param) != -1)
					ErrorAt(opener, "The name '" + param + "' is used twice in the same parameter list.");
				AddLocal(param);
			}
			if (params.size() > 255)
				ErrorAt(opener, "A function can take at most 255 values.");
			fn->ParamNames = params;
			if (fn->HasRest)
			{
				// Defaults belong to the values before the one that collects the rest.
				const std::vector<std::string> fixed(params.begin(), params.end() - 1);
				const std::vector<DefaultValue> fixedDefaults(defaults.begin(), defaults.begin() + std::min(defaults.size(), fixed.size()));
				DefaultValues(fn, fixed, fixedDefaults);
			}
			else
				DefaultValues(fn, params, defaults);

			// Every name this function assigns with "set" becomes one of its own variables,
			// unless it already belongs to the whole script or to an outer function.
			std::vector<std::string> names;
			ScanScope(m_Pos, false, names);
			for (const std::string& local : names)
			{
				if (ResolveLocal(m_State, local) != -1 || IsModuleName(local) || IsVisibleInEnclosing(local))
					continue;
				EmitOp(OpCode::Nothing);
				AddLocal(local, c_NoToken);
			}

			fn->BodyIp = CurrentChunk().Size();
			if (body)
				body();
			else
				Block();
			ConsumeEnd(opener, what);
			// Returning at the end belongs to the body's last line; the closure made from it, and what
			// the definition does with it, to the line that opened it.
			m_ClosingLine = LineBeforeEnd();
			EmitOp(OpCode::Nothing);
			EmitOp(OpCode::Return);
			m_ClosingLine = opener.Line;

			m_State = state.Enclosing;
			EmitOp(OpCode::Closure);
			EmitU16(MakeConstant(Value::FromObject(fn)));
			for (const UpvalueInfo& upvalue : state.Upvalues)
			{
				EmitByte(upvalue.IsLocal ? 1 : 0);
				EmitByte(upvalue.Index);
			}
		}

		// The names in "(a, b is 10)". A default value ('is' and a value) is only noted here; FunctionBody
		// compiles it inside the function, so it is worked out at each call that leaves the value out.
		// 'as number' after a parameter's name: the type word (or a kind's name) for checkers. It never
		// changes what runs. A misspelled type word ('nmber') is a mistake.
		std::string ParameterType()
		{
			Advance(); // as
			const Token& type = Current();
			std::string name(type.Lexeme);
			if (type.Type == TokenType::Function || type.Type == TokenType::Kind)
			{
				Advance();
				return name;
			}
			if (type.Type != TokenType::Identifier)
				ErrorAtCurrent("After 'as', write a type, like: amount as number (or text, boolean, list, map, function, vector, object, or a kind's name)");
			static const std::vector<std::string_view> c_Types = { "number", "text", "boolean", "list", "map", "vector", "object" };
			if (std::find(c_Types.begin(), c_Types.end(), name) == c_Types.end() && std::islower(static_cast<unsigned char>(name[0])))
			{
				const std::string closest = ClosestName(name, c_Types);
				if (!closest.empty())
				{
					FixTokens(Current(), Current(), "Change to '" + closest + "'", closest);
					ErrorAtCurrent("'" + name + "' isn't a type. Did you mean '" + closest + "'?");
				}
			}
			Advance();
			return name;
		}

		std::vector<std::string> ParameterList(std::vector<DefaultValue>* defaults = nullptr)
		{
			m_ParamTypes.clear();
			std::vector<std::string> params;
			Consume(TokenType::LeftParen, "Expected '(' after the function name, like: function heal(amount)");
			if (!Check(TokenType::RightParen))
			{
				std::string firstWithDefault;
				do
				{
					const Token& nameToken = Current();
					params.push_back(ConsumeName("Expected a parameter name."));
					DefaultValue value;
					// 'amount as number': a type for checkers only (see ParameterType).
					m_ParamTypes.resize(params.size());
					if (CheckWord("as"))
						m_ParamTypes.back() = ParameterType();
					if (Check(TokenType::Dot) && Peek(1).Type == TokenType::Dot && Peek(2).Type == TokenType::Dot)
					{
						// 'numbers...': the last parameter gets the values left over, as a list.
						if (!m_ParamTypes.back().empty())
							ErrorAtCurrent("'" + params.back() + "...' is always a list of the values left over, so it can't have a type.");
						Advance();
						Advance();
						Advance();
						if (CheckWord("as"))
							ErrorAtCurrent("'" + params.back() + "...' is always a list of the values left over, so it can't have a type.");
						if (Check(TokenType::Is))
							ErrorAtCurrent("'" + params.back() + "...' gets the values left over, so it can't have a default. It is an empty list when there are none.");
						if (!Check(TokenType::RightParen))
							ErrorAtCurrent("Only the last value can collect the rest, like: function total(first, others...)");
						m_RestParam = true;
						if (defaults)
							defaults->push_back(value);
						break;
					}
					if (Check(TokenType::Equal))
					{
						FixTokens(Current(), Current(), "Change '=' to 'is'", "is");
						ErrorAtCurrent("Use 'is' for a default value, like: function attack(target, damage is 10)");
					}
					if (Match(TokenType::Is))
					{
						value.Start = m_Pos;
						int depth = 0;
						while (!Check(TokenType::Eof) && !(depth == 0 && (Check(TokenType::Comma) || Check(TokenType::RightParen))))
						{
							const TokenType type = Current().Type;
							const bool afterBrace = !Current().Lexeme.empty() && Current().Lexeme.front() == '}';
							if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace ||
								(type == TokenType::Interpolation && !afterBrace))
								depth++;
							else if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace ||
								(type == TokenType::String && afterBrace))
								depth--;
							Advance();
						}
						value.End = m_Pos;
						if (value.End == value.Start)
							ErrorAt(nameToken, "Write the default value after 'is', like: damage is 10");
						if (firstWithDefault.empty())
							firstWithDefault = params.back();
					}
					else if (!firstWithDefault.empty())
					{
						ErrorAt(nameToken, "Give '" + params.back() + "' a default value too, or put it before '" + firstWithDefault +
							"': values with defaults come last.");
					}
					if (value.Start && !defaults)
						ErrorAt(nameToken, "Default values can't be used here.");
					if (defaults)
						defaults->push_back(value);
				} while (Match(TokenType::Comma));
			}
			Consume(TokenType::RightParen, "Expected ')' after the parameters.");
			return params;
		}

		// The code that fills in default values, at the start of the function. A call that leaves values
		// out starts at the first one it left out (Function::DefaultIps); each default is worked out, in
		// order, seeing only the values before it, then the body runs. A call with every value skips it all.
		void DefaultValues(Function* fn, const std::vector<std::string>& params, const std::vector<DefaultValue>& defaults)
		{
			size_t first = params.size();
			for (size_t i = 0; i < defaults.size(); i++)
			{
				if (defaults[i].Start)
				{
					first = i;
					break;
				}
			}
			if (first == params.size())
				return;

			fn->MinArity = static_cast<int>(first);
			if (params.size() - first > 64)
				Error("A function can have at most 64 values with defaults.");
			std::vector<Local>& locals = m_State->Locals;
			for (size_t i = first; i < params.size(); i++)
			{
				fn->DefaultIps.push_back(CurrentChunk().Size());
				// A call that names its values may give a later one and leave this one out.
				EmitOp(OpCode::SkipIfGiven);
				EmitByte(static_cast<uint8_t>(i - first));
				EmitByte(0xff);
				EmitByte(0xff);
				const size_t skip = CurrentChunk().Size() - 2;
				// Slot 0 holds the function (or self), so parameter i lives in slot i + 1.
				std::vector<std::string> later;
				for (size_t j = i; j < params.size(); j++)
				{
					later.push_back(std::move(locals[j + 1].Name));
					locals[j + 1].Name.clear();
				}
				const size_t position = m_Pos;
				m_Pos = defaults[i].Start;
				Expression();
				if (m_Pos != defaults[i].End)
					ErrorAtCurrent("Expected ',' or ')' after the default value.");
				m_Pos = position;
				for (size_t j = i; j < params.size(); j++)
					locals[j + 1].Name = std::move(later[j - i]);
				EmitOp(OpCode::SetLocal);
				EmitByte(static_cast<uint8_t>(i + 1));
				EmitOp(OpCode::Pop);
				PatchJump(skip);
			}
			fn->DefaultIps.push_back(CurrentChunk().Size());
		}

		// Stores the value on top of the stack under a name (script variable or local variable).
		void DefineName(const std::string& name)
		{
			if (m_State->Kind == ScopeKind::Script && m_State->ScopeDepth == 0)
			{
				WarnIfHidesBuiltin(name);
				NoteScriptName(LastConsumed(name), name, true);
				EmitWithName(OpCode::SetGlobal, name);
				EmitOp(OpCode::Pop);
			}
			else
			{
				int existing = ResolveLocal(m_State, name);
				if (existing != -1 && m_State->Locals[static_cast<size_t>(existing)].Depth == m_State->ScopeDepth)
				{
					NoteLocal(LastConsumed(name), name, m_State->Locals[static_cast<size_t>(existing)].Id, true);
					EmitOp(OpCode::SetLocal);
					EmitByte(static_cast<uint8_t>(existing));
					EmitOp(OpCode::Pop);
				}
				else if (m_State->Kind == ScopeKind::Script && ResolveLocal(m_State, name) == -1)
				{
					NoteScriptName(LastConsumed(name), name, true);
					EmitWithName(OpCode::SetGlobal, name);
					EmitOp(OpCode::Pop);
				}
				else
				{
					AddLocal(name);
				}
			}
		}

		void FunctionStatement()
		{
			Token opener = Current();
			Advance();
			const size_t nameAt = m_Pos;
			std::string name = ConsumeName("Expected a function name.");

			bool isLocal = !(m_State->Kind == ScopeKind::Script);
			if (!isLocal)
				NoteScriptName(nameAt, name, true);
			int slot = -1;
			if (isLocal)
			{
				// Declare first so the function can call itself.
				EmitOp(OpCode::Nothing);
				slot = AddLocal(name, nameAt);
			}

			std::vector<DefaultValue> defaults;
			std::vector<std::string> params = ParameterList(&defaults);
			// A script function's parameter types, for checking calls by name (read before the body,
			// whose own functions have parameter lists of their own).
			if (!isLocal && m_Options.Warnings &&
				std::any_of(m_ParamTypes.begin(), m_ParamTypes.end(), [](const std::string& type) { return !type.empty(); }))
				m_FunctionTypes[name] = { params, m_ParamTypes };
			FunctionBody(name, ScopeKind::Function, params, opener, "function", {}, defaults);

			if (isLocal)
			{
				EmitOp(OpCode::SetLocal);
				EmitByte(static_cast<uint8_t>(slot));
				EmitOp(OpCode::Pop);
			}
			else
			{
				EmitWithName(OpCode::SetGlobal, name);
				EmitOp(OpCode::Pop);
			}
		}

		void LambdaExpression()
		{
			Token opener = Previous();
			std::vector<DefaultValue> defaults;
			std::vector<std::string> params = ParameterList(&defaults);
			FunctionBody("function", ScopeKind::Function, params, opener, "function", {}, defaults);
		}

		void PhraseStatement()
		{
			Token opener = Current();
			Advance();
			if (m_State->Kind != ScopeKind::Script || m_State->ScopeDepth != 0)
				ErrorAt(opener, "Phrases can only be made at the top level of a script.");

			PhrasePattern pattern;
			std::string error;
			if (!PatternFromTokens(m_Tokens, m_Pos, pattern, error))
				ErrorAt(opener, error);
			FunctionBody(pattern.MangledName(), ScopeKind::Phrase, pattern.SlotNames, opener, "phrase");
			EmitWithName(OpCode::SetGlobal, pattern.MangledName());
			EmitOp(OpCode::Pop);
		}

		void ReturnStatement()
		{
			Token keyword = Current();
			Advance();
			if (m_State->Kind == ScopeKind::FieldInit)
				ErrorAt(keyword, "'return' cannot be used here.");
			if (Check(TokenType::Newline) || IsBlockEnd())
				EmitOp(OpCode::Nothing);
			else
				Expression();
			int depth = m_State->TryDepth;
			if (!m_State->Finally.empty())
			{
				// The value waits in a hidden local while each finally on the way out runs, innermost first.
				AddLocal("");
				const std::vector<FinallyContext> contexts = m_State->Finally;
				for (auto it = contexts.rbegin(); it != contexts.rend(); ++it)
				{
					for (; depth > it->TryDepth; depth--)
						EmitOp(OpCode::TryEnd);
					EmitFinallyCopy(*it);
				}
				m_State->Locals.pop_back();
			}
			for (; depth > 0; depth--)
				EmitOp(OpCode::TryEnd);
			EmitOp(OpCode::Return);
		}

		void KindStatement()
		{
			Token opener = Current();
			Advance();
			const size_t nameAt = m_Pos;
			std::string name = ConsumeName("Expected a name for the kind, like: kind Enemy");

			EmitWithName(OpCode::Kind, name);
			bool isGlobal = m_State->Kind == ScopeKind::Script;
			if (isGlobal)
			{
				NoteScriptName(nameAt, name, true);
				EmitWithName(OpCode::SetGlobal, name);
			}
			else
			{
				int slot = AddLocal(name, nameAt);
				EmitOp(OpCode::GetLocal);
				EmitByte(static_cast<uint8_t>(slot));
			}

			if (Match(TokenType::Extends))
			{
				PushStopWords(nullptr);
				Expression();
				PopStopWords();
				EmitOp(OpCode::Inherit);
			}

			// First pass: compile methods and shared values, remember where the field lines are.
			std::vector<size_t> fieldLines;
			std::set<std::string> sharedNames;
			SkipNewlines();
			while (!Check(TokenType::End))
			{
				if (Check(TokenType::Eof))
				{
					FixMissingEnd(opener);
					ErrorAt(opener, "This kind is missing its 'end'.");
				}

				if (Check(TokenType::Set))
				{
					fieldLines.push_back(m_Pos);
					while (!Check(TokenType::Newline) && !Check(TokenType::Eof) && !Check(TokenType::End))
						Advance();
				}
				else if (CheckWord("shared") && Peek(1).Type == TokenType::Identifier &&
					(Peek(2).Type == TokenType::Is || Peek(2).Type == TokenType::To))
				{
					// 'shared count is 0': worked out once, here, as the kind is made; read as Enemy.count.
					Advance();
					const Token nameToken = Current();
					const std::string sharedName(nameToken.Lexeme);
					if (sharedName == "name")
						ErrorAt(nameToken, "A kind's 'name' is already its name (Enemy.name is \"Enemy\"). Choose another name for the shared value.");
					if (!sharedNames.insert(sharedName).second)
						ErrorAt(nameToken, "'" + sharedName + "' is shared twice in this kind.");
					Advance();
					Advance();
					Expression();
					EmitWithName(OpCode::SharedValue, sharedName);
				}
				else if (Check(TokenType::When) && Peek(1).Type == TokenType::My && Peek(2).Type == TokenType::Identifier &&
					Peek(3).Type == TokenType::Identifier && Peek(3).Lexeme == "changes")
				{
					// 'when my health changes [with now, before]': a method run right after a write that
					// changes the field (see VM::RunChangeHandlers).
					const Token handlerOpener = Current();
					Advance(); // when
					Advance(); // my
					const std::string field(Current().Lexeme);
					Advance();
					Advance(); // changes
					std::vector<std::string> params;
					if (Match(TokenType::With) || Match(TokenType::By))
					{
						do
						{
							params.push_back(ConsumeName("Expected a name for the new value (and then the old one)."));
						} while (Match(TokenType::Comma) || Match(TokenType::And));
						if (params.size() > 2)
							ErrorAt(handlerOpener, "'when my " + field + " changes' gives two values: the new one and the old one, like: with now, before");
					}
					Match(TokenType::Do);
					FunctionBody("when my " + field + " changes", ScopeKind::Method, params, handlerOpener, "'when'");
					EmitWithName(OpCode::ChangeHandler, field);
				}
				else if (Check(TokenType::Function))
				{
					Token methodOpener = Current();
					Advance();
					std::string methodName = ConsumeMemberName();
					std::vector<DefaultValue> defaults;
			std::vector<std::string> params = ParameterList(&defaults);
					FunctionBody(methodName, ScopeKind::Method, params, methodOpener, "function", {}, defaults);
					EmitWithName(OpCode::Method, methodName);
				}
				else
				{
					ErrorAtCurrent("Inside a kind, use 'set name to value' for fields and 'function' for methods, 'shared name is value' for one value all its objects share, or 'when my name changes' to react to a field.");
				}
				EndOfStatement();
				SkipNewlines();
			}
			size_t afterBody = m_Pos;

			// Second pass: compile all field lines into one hidden "set up fields" method.
			Function* fields = m_VM.NewFunction();
			fields->Name = m_VM.Intern(name + " fields");
			fields->Owner = m_Module;
			fields->IsMethod = true;
			fields->IsFieldInit = true;

			FunctionState state;
			state.Enclosing = m_State;
			state.Fn = fields;
			state.Kind = ScopeKind::FieldInit;
			m_State = &state;
			BeginScope();
			state.Locals.push_back({ "self", state.ScopeDepth, false });
			NameLocal(0);

			for (size_t line : fieldLines)
			{
				m_Pos = line;
				Advance(); // set
				Match(TokenType::My);
				std::string field = ConsumeMemberName();
				EmitOp(OpCode::GetLocal);
				EmitByte(0);
				if (Match(TokenType::To))
					Expression();
				else
					EmitOp(OpCode::Nothing);
				EmitWithName(OpCode::SetProperty, field);
				EmitOp(OpCode::Pop);
				if (!Check(TokenType::Newline) && !Check(TokenType::End) && !Check(TokenType::Eof))
					ErrorAtCurrent("Expected the end of the line after this field.");
			}
			EmitOp(OpCode::Nothing);
			EmitOp(OpCode::Return);
			m_State = state.Enclosing;

			EmitOp(OpCode::Closure);
			EmitU16(MakeConstant(Value::FromObject(fields)));
			for (const UpvalueInfo& upvalue : state.Upvalues)
			{
				EmitByte(upvalue.IsLocal ? 1 : 0);
				EmitByte(upvalue.Index);
			}
			EmitOp(OpCode::FieldInit);

			m_Pos = afterBody;
			ConsumeEnd(opener, "kind");
			EmitOp(OpCode::Pop);
		}

		// 'finally' at the start of a line, with nothing after it that would make it a value.
		static bool LooksLikeFinally(const std::vector<Token>& tokens, size_t i)
		{
			if (tokens[i].Type != TokenType::Identifier || tokens[i].Lexeme != "finally" || i == 0 || tokens[i - 1].Type != TokenType::Newline)
				return false;
			if (i + 1 >= tokens.size())
				return true;
			switch (tokens[i + 1].Type)
			{
				case TokenType::Newline: case TokenType::Eof: case TokenType::End: case TokenType::Identifier:
				case TokenType::Set: case TokenType::Constant: case TokenType::Add: case TokenType::Take:
				case TokenType::Multiply: case TokenType::Divide: case TokenType::Return: case TokenType::Fail:
				case TokenType::Trigger: case TokenType::Wait: case TokenType::Start: case TokenType::Stop:
				case TokenType::Skip: case TokenType::For: case TokenType::While: case TokenType::Repeat:
				case TokenType::Try: case TokenType::Use:
					return true;
				default:
					return false;
			}
		}

		struct TryParts
		{
			size_t Catch = 0;   // this try's 'catch', or 0
			size_t Finally = 0; // this try's 'finally', or 0
		};

		// The 'catch' and 'finally' of the 'try' whose code starts at 'start'. 'finally' is not a reserved
		// word: before it existed, a line starting with it used something named 'finally', so it only
		// ends a section where nothing called 'finally' can exist.
		TryParts FindTryParts(size_t start)
		{
			TryParts parts;
			int depth = 0;
			for (size_t i = start; i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof; i++)
			{
				if (IsBlockOpener(m_Tokens, i))
					depth++;
				else if (m_Tokens[i].Type == TokenType::End && m_Tokens[i - 1].Type != TokenType::Dot && m_Tokens[i - 1].Type != TokenType::Possessive)
				{
					if (depth == 0)
						break;
					depth--;
				}
				else if (depth == 0 && m_Tokens[i].Type == TokenType::Catch && !parts.Catch)
					parts.Catch = i;
				else if (depth == 0 && LooksLikeFinally(m_Tokens, i) && !NameMayExist("finally", LooksLikeFinally))
				{
					if (parts.Finally)
						ErrorAt(m_Tokens[i], "A 'try' can have only one 'finally'.");
					parts.Finally = i;
				}
			}
			if (parts.Finally && parts.Catch > parts.Finally)
				ErrorAt(m_Tokens[parts.Catch], "Put 'catch' before 'finally', like: try ... catch problem ... finally ... end");
			return parts;
		}

		void TryStatement()
		{
			Token opener = Current();
			Advance();
			const TryParts parts = FindTryParts(m_Pos);
			if (parts.Finally)
			{
				TryFinallyStatement(opener, parts);
				return;
			}
			size_t handler = EmitJump(OpCode::TryBegin);
			m_State->TryDepth++;
			ScopedBlock();
			m_State->TryDepth--;
			EmitOp(OpCode::TryEnd);
			size_t skipCatch = EmitJump(OpCode::Jump);
			PatchJump(handler);

			// The error value is on the stack when the catch block starts.
			BeginScope();
			if (Match(TokenType::Catch))
			{
				std::string name;
				if (Check(TokenType::Identifier))
					name = ConsumeName("Expected a name for the error.");
				AddLocal(name);
				Block();
			}
			else
			{
				AddLocal("");
			}
			EndScope();
			ConsumeEnd(opener, "'try'");
			PatchJump(skipCatch);
		}

		// try ... [catch problem ...] finally ... end. The finally code runs however the 'try' is left: at
		// the end of the try or catch code, on an error (which then goes on, unless 'catch' took it), and
		// on return, stop or skip. Each way out gets its own copy of the finally code.
		void TryFinallyStatement(const Token& opener, const TryParts& parts)
		{
			m_FinallyMarkers.insert(parts.Finally);
			FinallyContext context;
			context.BodyStart = parts.Finally + 1;
			context.TryDepth = m_State->TryDepth;
			context.LocalCount = m_State->Locals.size();
			context.LoopCount = m_State->Loops.size();
			m_State->Finally.push_back(context);
			std::vector<size_t> toFinally;

			// Without a catch, an error in the try code goes on after the finally code.
			const size_t tryHandler = EmitJump(parts.Catch ? OpCode::TryBegin : OpCode::TryFinally);
			m_State->TryDepth++;
			ScopedBlock();
			m_State->TryDepth--;
			EmitOp(OpCode::TryEnd);
			toFinally.push_back(EmitJump(OpCode::Jump));
			PatchJump(tryHandler);

			if (!parts.Catch)
			{
				EmitFinallyOnError(context, 0);
			}
			else
			{
				// The error is on the stack when the catch code starts. An error in the catch code goes on
				// after the finally code.
				Consume(TokenType::Catch, "Expected 'catch' here.");
				BeginScope();
				std::string name;
				if (Check(TokenType::Identifier) && m_Pos != parts.Finally)
					name = ConsumeName("Expected a name for the error.");
				AddLocal(name);
				const size_t catchHandler = EmitJump(OpCode::TryFinally);
				m_State->TryDepth++;
				Block();
				m_State->TryDepth--;
				EmitOp(OpCode::TryEnd);
				EndScope();
				toFinally.push_back(EmitJump(OpCode::Jump));
				PatchJump(catchHandler);
				EmitFinallyOnError(context, 1);
			}

			m_State->Finally.pop_back();
			for (size_t jump : toFinally)
				PatchJump(jump);
			if (m_Pos != parts.Finally)
				ErrorAtCurrent("Expected 'finally' here.");
			Advance(); // finally
			ScopedBlock();
			ConsumeEnd(opener, "'try'");
		}

		// Where a finally handler starts: the stack holds 'below' hidden values (the caught error, during
		// the catch code), then the error and where it happened. Runs the finally code and raises the
		// error again from where it happened.
		void EmitFinallyOnError(const FinallyContext& context, int below)
		{
			BeginScope();
			for (int i = 0; i < below; i++)
				AddLocal("");
			const int error = AddLocal("");
			const int where = AddLocal("");
			EmitFinallyCopy(context);
			EmitOp(OpCode::GetLocal);
			EmitByte(static_cast<uint8_t>(error));
			EmitOp(OpCode::GetLocal);
			EmitByte(static_cast<uint8_t>(where));
			EmitOp(OpCode::Rethrow);
			EndScope();
		}

		// Compiles a copy of the finally code of 'context' here. Its handlers are already closed, and the
		// copy only sees the names visible where the finally code is written, outside the 'try'. Its
		// mistakes are reported once, where it is written.
		void EmitFinallyCopy(const FinallyContext& context)
		{
			FunctionState& state = *m_State;
			const size_t position = m_Pos;
			const size_t closingPos = m_ClosingPos;
			const int closingLine = m_ClosingLine;
			const CompileError error = m_Error;
			std::vector<CompileError>* allErrors = m_AllErrors;
			const std::vector<FinallyContext> contexts = state.Finally;
			std::vector<LoopInfo> loops = state.Loops;
			const size_t localCount = state.Locals.size();
			const int scopeDepth = state.ScopeDepth;
			const int tryDepth = state.TryDepth;
			const size_t stopWords = m_StopWords.size();

			std::vector<std::string> hidden;
			for (size_t i = context.LocalCount; i < localCount; i++)
			{
				hidden.push_back(std::move(state.Locals[i].Name));
				state.Locals[i].Name.clear();
			}
			// Only the finally code of 'try's around this one runs when the copy itself returns early.
			auto own = std::find_if(state.Finally.begin(), state.Finally.end(),
				[&context](const FinallyContext& other) { return other.BodyStart == context.BodyStart; });
			state.Finally.erase(own, state.Finally.end());
			// 'stop' and 'skip' in it belong to the loops around the 'try'.
			const size_t outerLoops = std::min(state.Loops.size(), context.LoopCount);
			state.Loops.resize(outerLoops);
			state.TryDepth = context.TryDepth;

			std::vector<CompileError> ignored;
			m_AllErrors = &ignored;
			m_Pos = context.BodyStart;
			try
			{
				ScopedBlock();
			}
			catch (const CompileAbort&)
			{
				// Reported where the finally code is written.
			}

			// Jumps that the copy's 'stop' and 'skip' added are patched by their loops.
			for (size_t i = 0; i < outerLoops; i++)
			{
				loops[i].BreakJumps = std::move(state.Loops[i].BreakJumps);
				loops[i].ContinueJumps = std::move(state.Loops[i].ContinueJumps);
			}
			m_AllErrors = allErrors;
			m_Error = error;
			m_Pos = position;
			m_ClosingPos = closingPos;
			m_ClosingLine = closingLine;
			state.Locals.resize(localCount);
			for (size_t i = context.LocalCount; i < localCount; i++)
				state.Locals[i].Name = std::move(hidden[i - context.LocalCount]);
			state.Loops = std::move(loops);
			state.Finally = contexts;
			state.ScopeDepth = scopeDepth;
			state.TryDepth = tryDepth;
			m_StopWords.resize(stopWords);
		}

		void FailStatement()
		{
			Advance();
			Expression();
			EmitOp(OpCode::Fail);
		}

		// Reads words that name an event, like "game starts" or "hit".
		std::string EventName(const char* example)
		{
			std::string name;
			if (Check(TokenType::String))
			{
				name = Current().Text;
				Advance();
				return name;
			}
			while (Current().IsWord() && !Check(TokenType::With) && !Check(TokenType::By) &&
				!Check(TokenType::Then) && !Check(TokenType::Do))
			{
				if (!name.empty())
					name += ' ';
				name += std::string(Current().Lexeme);
				Advance();
			}
			if (name.empty())
				ErrorAtCurrent(std::string("Expected an event name, like: ") + example);
			return name;
		}

		void WhenStatement()
		{
			Token opener = Current();
			Advance();
			// 'when "Start" is clicked' runs only for the event the entity named "Start" raises. A text
			// followed by 'is' is never an event name (those are 'when "door opened"'), so this form
			// changes no script that worked before.
			std::string source;
			if (Check(TokenType::Interpolation))
				ErrorAtCurrent("The text after 'when' must be plain, without { }: an event name, or an entity's name as in when \"Start\" is clicked.");
			if (Check(TokenType::String) && Peek(1).Type == TokenType::Is)
			{
				source = Current().Text;
				if (source.empty())
					ErrorAtCurrent("Write the entity's name in the quotes, like: when \"Start\" is clicked");
				Advance();
				Advance();
			}
			std::string event = EventName(source.empty() ? "when game starts" : "when \"Start\" is clicked");
			std::vector<std::string> params;
			if (Match(TokenType::With) || Match(TokenType::By))
			{
				do
				{
					params.push_back(ConsumeName("Expected a name for the event value."));
				} while (Match(TokenType::Comma) || Match(TokenType::And));
			}
			Match(TokenType::Do);
			FunctionBody(source.empty() ? "when " + event : "when \"" + source + "\" is " + event, ScopeKind::Event, params, opener, "'when'",
				source);
			EmitWithName(OpCode::OnEvent, event);
		}

		void TriggerStatement()
		{
			Advance();
			std::string event = EventName("trigger player died");
			uint8_t count = 0;
			if (Match(TokenType::With))
			{
				do
				{
					Expression();
					if (count == 255)
						Error("An event can carry at most 255 values.");
					count++;
				} while (Match(TokenType::Comma));
			}
			EmitWithName(OpCode::Trigger, event);
			EmitByte(count);
		}

		void WaitStatement()
		{
			Advance();
			if (Match(TokenType::For))
			{
				WaitForEvent();
				return;
			}
			if (Match(TokenType::Until))
			{
				// wait until door_open   ->   check every frame until it is true
				size_t loopStart = CurrentChunk().Size();
				Expression();
				size_t done = EmitJump(OpCode::JumpIfTrue);
				EmitOp(OpCode::Pop);
				EmitConstant(Value::FromNumber(1));
				EmitOp(OpCode::Wait);
				EmitByte(static_cast<uint8_t>(WaitMode::Frames));
				EmitLoop(loopStart);
				PatchJump(done);
				EmitOp(OpCode::Pop);
				return;
			}

			if (Check(TokenType::Newline) || IsBlockEnd())
			{
				EmitConstant(Value::FromNumber(1));
				EmitOp(OpCode::Wait);
				EmitByte(static_cast<uint8_t>(WaitMode::Frames));
				return;
			}

			WaitAmount();
		}

		// 'wait for hit with damage': waits until the script gets the event, then sets its values.
		void WaitForEvent()
		{
			std::string source;
			if (Check(TokenType::Interpolation))
				ErrorAtCurrent("The text after 'wait for' must be plain, without { }: an event name, or an entity's name as in wait for \"Start\" is clicked.");
			if (Check(TokenType::String) && Peek(1).Type == TokenType::Is)
			{
				source = Current().Text;
				if (source.empty())
					ErrorAtCurrent("Write the entity's name in the quotes, like: wait for \"Start\" is clicked");
				Advance();
				Advance();
			}
			std::string event = EventName(source.empty() ? "wait for door opened" : "wait for \"Start\" is clicked");
			std::vector<std::string> names;
			std::vector<size_t> nameTokens;
			if (Match(TokenType::With) || Match(TokenType::By))
			{
				do
				{
					nameTokens.push_back(m_Pos);
					names.push_back(ConsumeName("Expected a name for the event value, like: wait for hit with damage"));
				} while (Match(TokenType::Comma) || Match(TokenType::And));
			}
			if (names.size() > 255)
				Error("An event can carry at most 255 values.");
			EmitWithName(OpCode::WaitEvent, event);
			EmitU16(NameConstant(source));
			EmitByte(static_cast<uint8_t>(names.size()));
			// The values are on the stack in order, so the last name takes the top one.
			for (size_t i = names.size(); i > 0; i--)
			{
				const std::string& name = names[i - 1];
				Target target;
				if (m_State->Kind == ScopeKind::Script && ResolveLocal(m_State, name) == -1)
				{
					target.Type = TargetType::Global;
					target.Name = NameConstant(name);
					NoteScriptName(nameTokens[i - 1], name, true);
				}
				else
				{
					target = ResolveName(name, nameTokens[i - 1]);
				}
				EmitSet(target);
				EmitOp(OpCode::Pop);
			}
		}

		// The amount and unit of 'wait 2 seconds' (also a timer's), then the wait itself.
		void WaitAmount()
		{
			Expression();
			WaitMode mode = WaitMode::Seconds;
			if (MatchWord("frame") || MatchWord("frames"))
			{
				mode = WaitMode::Frames;
			}
			else if (MatchWord("milliseconds") || MatchWord("millisecond") || MatchWord("ms"))
			{
				EmitConstant(Value::FromNumber(1000));
				EmitOp(OpCode::Divide);
			}
			else if (MatchWord("minutes") || MatchWord("minute"))
			{
				EmitConstant(Value::FromNumber(60));
				EmitOp(OpCode::Multiply);
			}
			else
			{
				if (!MatchWord("seconds"))
					MatchWord("second");
			}
			EmitOp(OpCode::Wait);
			EmitByte(static_cast<uint8_t>(mode));
		}

		// 'sort items' is 'set items to items.sort()', and the same for shuffle and reverse, so 'reverse name' on
		// a text changes the variable too (text can't be changed in place). 'sort enemies by health' sorts by
		// each item's field or key; 'sort items by function(x) ... end' by what the function gives.
		void ListVerbStatement()
		{
			const std::string verb(Current().Lexeme);
			Advance();
			TargetChain chain = ParseTarget();
			const Target& target = chain.Final;
			BeginWriteBack(chain);
			if (target.Type == TargetType::Property)
				EmitOp(OpCode::Dup);
			else if (target.Type == TargetType::Index)
				EmitOp(OpCode::Dup2);
			EmitGet(target);
			if (verb == "sort" && Match(TokenType::By))
			{
				if (Check(TokenType::Function))
					Expression();
				else if (Check(TokenType::Identifier) && (Peek(1).Type == TokenType::Newline || Peek(1).Type == TokenType::Eof || Peek(1).Type == TokenType::End))
				{
					EmitFieldGetter(std::string(Current().Lexeme));
					Advance();
				}
				else
				{
					ErrorAtCurrent("After 'by', write a field name or a function, like: sort enemies by health, or: sort words by function(w) return w.length end");
				}
				EmitWithName(OpCode::Invoke, "sort_by");
				EmitByte(1);
			}
			else
			{
				EmitWithName(OpCode::Invoke, verb);
				EmitByte(0);
			}
			EmitSet(target);
			EmitOp(OpCode::Pop);
			FinishWriteBack(chain);
		}

		// A function that gives its value's field: the key of 'sort enemies by health'.
		void EmitFieldGetter(const std::string& field)
		{
			Function* fn = m_VM.NewFunction();
			fn->Name = m_VM.Intern("by " + field);
			fn->Owner = m_Module;
			fn->Arity = 1;
			FunctionState state;
			state.Enclosing = m_State;
			state.Fn = fn;
			state.Kind = ScopeKind::Function;
			m_State = &state;
			BeginScope();
			state.Locals.push_back({ "", state.ScopeDepth, false });
			NameLocal(0);
			AddLocal("item");
			fn->ParamNames = { "item" };
			DefaultValues(fn, fn->ParamNames, {});
			fn->BodyIp = CurrentChunk().Size();
			EmitOp(OpCode::GetLocal);
			EmitByte(1);
			EmitWithName(OpCode::GetProperty, field);
			EmitOp(OpCode::Return);
			m_State = state.Enclosing;
			EmitOp(OpCode::Closure);
			EmitU16(MakeConstant(Value::FromObject(fn)));
		}

		// 'every 3 seconds ... end' and 'after 2 seconds ... end': the body runs as a task of its own, made
		// from a function like an anonymous one. 'every' waits, runs the body, and starts over; the time is
		// worked out again each time, counted from when the body finished. 'stop' inside it ends it.
		void TimerStatement()
		{
			Token opener = Current();
			const bool every = opener.Lexeme == "every";
			Advance();
			// A time written as a negative number runs like 0: 'every' then runs once a frame.
			if (Check(TokenType::Minus) && Peek(1).Type == TokenType::Number)
			{
				Warn(Current(), std::string("A negative time works like 0, so this timer ") +
					(every ? "runs as often as it can" : "runs as soon as it can") + ". Use a time above 0, like: " +
					std::string(opener.Lexeme) + " 1 second");
			}
			FunctionBody(std::string(opener.Lexeme) + " timer", ScopeKind::Function, {}, opener, every ? "'every' timer" : "'after' timer", {}, {},
				[this, every]
				{
					if (every)
					{
						size_t loopStart = CurrentChunk().Size();
						PushLoop(loopStart, true);
						WaitAmount();
						Match(TokenType::Do);
						ScopedBlock();
						EmitLoop(loopStart);
						PopLoop();
					}
					else
					{
						WaitAmount();
						Match(TokenType::Do);
						Block();
					}
				});
			EmitOp(OpCode::Start);
			EmitByte(0);
		}

		void StartStatement()
		{
			Token keyword = Current();
			Advance();
			m_NoInvoke = true;
			m_LastCallPos = static_cast<size_t>(-1);
			// A single call, so not 'start a() if x otherwise b()': only one of those calls could become a start.
			OrExpression();
			m_NoInvoke = false;
			std::vector<uint8_t>& code = CurrentChunk().GetCode();
			if (m_LastCallPos == static_cast<size_t>(-1) || m_LastCallPos + 2 != code.size())
				ErrorAt(keyword, "'start' needs a function call after it, like: start patrol()");
			code[m_LastCallPos] = static_cast<uint8_t>(OpCode::Start);
		}

		void UseStatement()
		{
			Advance();
			if (!Check(TokenType::String))
				ErrorAtCurrent("Expected the script name in quotes, like: use \"helpers\"");
			std::string path = Current().Text;
			const std::string refused = CheckUsePath(path);
			if (!refused.empty())
				ErrorAtCurrent(refused);
			// When checking, a script that cannot be found is a mistake. A normal compile leaves this to
			// the moment the script runs, which reports the same message.
			if (m_Options.ReportMissingScripts)
			{
				std::string resolvedName;
				std::string source;
				if (!m_VM.ResolveModuleSource(path, resolvedName, source))
				{
					const std::string message = "Could not find the script \"" + path + "\" to use.";
					if (!m_AllErrors)
						ErrorAtCurrent(message);
					RecordError(Current(), message);
				}
			}
			Advance();
			bool hasAlias = MatchWord("as");
			std::string alias;
			if (hasAlias)
				alias = ConsumeName("Expected a name after 'as'.");

			EmitOp(OpCode::Use);
			EmitU16(MakeConstant(Value::FromObject(m_VM.Intern(path))));
			EmitByte(hasAlias ? 0 : 1);
			if (hasAlias)
				DefineName(alias);
			else
				EmitOp(OpCode::Pop);
		}

		// ------------------------------------------------------------------ Phrases

		// Whether this token, right after a name, makes it part of a longer value: a call, an index, a
		// member, 'of', or an operator.
		static bool ContinuesValue(const Token& token)
		{
			switch (token.Type)
			{
				case TokenType::LeftParen: case TokenType::LeftBracket: case TokenType::Dot: case TokenType::Possessive:
				case TokenType::Of: case TokenType::Is: case TokenType::And: case TokenType::Or: case TokenType::Contains:
				case TokenType::If: case TokenType::Plus: case TokenType::Minus: case TokenType::Star: case TokenType::Slash:
				case TokenType::Percent: case TokenType::Caret: case TokenType::Equal: case TokenType::EqualEqual:
				case TokenType::BangEqual: case TokenType::Less: case TokenType::LessEqual: case TokenType::Greater:
				case TokenType::GreaterEqual:
					return true;
				default:
					return false;
			}
		}

		bool CanStartExpression(const Token& token) const
		{
			switch (token.Type)
			{
				case TokenType::Number: case TokenType::String: case TokenType::Interpolation:
				case TokenType::Identifier: case TokenType::True: case TokenType::False:
				case TokenType::Yes: case TokenType::No: case TokenType::Nothing:
				case TokenType::Self: case TokenType::My: case TokenType::Parent:
				case TokenType::LeftParen: case TokenType::LeftBracket: case TokenType::LeftBrace:
				case TokenType::Minus: case TokenType::Not: case TokenType::Function: case TokenType::New:
					return true;
				default:
					return false;
			}
		}

		std::string DescribePhrases(const std::string& head) const
		{
			std::string list;
			for (const PhrasePattern& pattern : m_Phrases.GetPatterns())
			{
				if (pattern.Parts.front() != head)
					continue;
				std::string text;
				for (size_t i = 0, slot = 0; i < pattern.Parts.size(); i++)
				{
					if (i > 0)
						text += ' ';
					text += pattern.Parts[i].empty() ? "(" + pattern.SlotNames[slot++] + ")" : pattern.Parts[i];
				}
				if (!list.empty())
					list += "  or  ";
				list += text;
			}
			return list;
		}

		// Tries to compile a phrase call starting at the current word. Leaves the result on the stack.
		// As a whole statement each slot takes any expression ("say health is 0" says true or false).
		// Inside a bigger expression the slots take arithmetic only, so in
		// "if distance from a to b is below 1" the comparison applies to the phrase's result.
		bool TryPhrase(bool isStatement)
		{
			const Token& head = Current();
			if (head.Type != TokenType::Identifier)
				return false;

			std::string word(head.Lexeme);
			const PhraseTable::Node* root = m_Phrases.GetRoot();
			auto it = root->Words.find(word);
			if (it == root->Words.end())
				return false;

			// A local variable with the same name wins over a phrase.
			if (ResolveLocal(m_State, word) != -1 || IsVisibleInEnclosing(word))
				return false;
			// Some phrases start with a word that may be a name too, like 'show': there the word followed by
			// a call or an operator keeps its old meaning, and only "show menu" is the phrase.
			if (m_VM.PhraseYieldsToCalls(word) && ContinuesValue(Peek(1)))
			{
				WarnIfBracketsHidePhrase(word);
				return false;
			}

			const PhraseTable::Node* node = it->second.get();
			const Token& next = Peek(1);
			bool feasible = !node->Terminal.empty() ||
				(next.IsWord() && node->Words.count(std::string(next.Lexeme))) ||
				(node->Slot && CanStartExpression(next));
			if (!feasible)
				return false;

			Token headToken = head;
			Advance();

			// The callee is not known until the last word, so emit a placeholder and patch it.
			EmitOp(OpCode::GetGlobal);
			size_t calleeOffset = CurrentChunk().Size();
			EmitU16(0);

			int count = 0;
			while (true)
			{
				const Token& token = Current();
				if (token.IsWord())
				{
					auto wordIt = node->Words.find(std::string(token.Lexeme));
					if (wordIt != node->Words.end())
					{
						Advance();
						node = wordIt->second.get();
						continue;
					}
				}
				if (node->Slot && CanStartExpression(token) && !IsBlockKeyword(token.Type))
				{
					std::set<std::string> stopWords;
					for (const auto& [childWord, child] : node->Slot->Words)
						stopWords.insert(childWord);
					PushStopWords(&stopWords);
					if (isStatement)
						Expression();
					else
						Term();
					PopStopWords();
					count++;
					node = node->Slot.get();
					continue;
				}
				break;
			}

			if (node->Terminal.empty())
				ErrorAt(headToken, "This phrase is not complete. '" + word + "' can be used as: " + DescribePhrases(word));

			uint16_t name = NameConstant(node->Terminal);
			std::vector<uint8_t>& code = CurrentChunk().GetCode();
			code[calleeOffset] = static_cast<uint8_t>((name >> 8) & 0xff);
			code[calleeOffset + 1] = static_cast<uint8_t>(name & 0xff);

			m_LastCallPos = CurrentChunk().Size();
			EmitOp(OpCode::Call);
			EmitByte(static_cast<uint8_t>(count));
			return true;
		}

		// ------------------------------------------------------------------ Expressions

		// 'value if test otherwise other' picks one of two values. The first value only runs when the
		// test is true, so "widget.name if widget otherwise ..." never reads a name from nothing: its
		// code, compiled before the 'if' was seen, moves after the test. It binds looser than 'or' and
		// groups to the right, so a chain reads in order: "a" if x otherwise "b" if y otherwise "c".
		void Expression()
		{
			Nested nested(*this);
			const size_t start = CurrentChunk().Size();
			OrExpression();
			if (!Check(TokenType::If) || !IfPicksAValue(m_Tokens, m_Pos))
				return;
			Advance();
			const Chunk::Piece value = CurrentChunk().Cut(start);
			OrExpression();
			if (!Match(TokenType::Otherwise) && !Match(TokenType::Else))
				ErrorAtCurrent("Say which value to use when it isn't true, like: set size to \"big\" if x > 3 otherwise \"small\"");
			const size_t otherwise = EmitJump(OpCode::JumpIfFalse);
			EmitOp(OpCode::Pop);
			CurrentChunk().Paste(value);
			const size_t done = EmitJump(OpCode::Jump);
			PatchJump(otherwise);
			EmitOp(OpCode::Pop);
			Expression();
			PatchJump(done);
		}

		void OrExpression()
		{
			AndExpression();
			while (CheckOperator(TokenType::Or))
			{
				Advance();
				size_t jump = EmitJump(OpCode::JumpIfTrue);
				EmitOp(OpCode::Pop);
				AndExpression();
				PatchJump(jump);
			}
		}

		void AndExpression()
		{
			NotExpression();
			while (CheckOperator(TokenType::And))
			{
				Advance();
				size_t jump = EmitJump(OpCode::JumpIfFalse);
				EmitOp(OpCode::Pop);
				NotExpression();
				PatchJump(jump);
			}
		}

		void NotExpression()
		{
			if (Match(TokenType::Not))
			{
				Nested nested(*this);
				NotExpression();
				EmitOp(OpCode::Not);
				return;
			}
			Comparison();
		}

		bool IsTypeWord(const Token& token, TypeCheck& outType) const
		{
			static const std::pair<const char*, TypeCheck> s_Types[] = {
				{ "number", TypeCheck::Number }, { "text", TypeCheck::Text }, { "boolean", TypeCheck::Boolean },
				{ "list", TypeCheck::List }, { "map", TypeCheck::Map }, { "function", TypeCheck::Function },
				{ "vector", TypeCheck::Vector }, { "object", TypeCheck::Object },
			};
			if (token.Type == TokenType::Function) { outType = TypeCheck::Function; return true; }
			if (token.Type == TokenType::Kind) { outType = TypeCheck::Kind; return true; }
			if (token.Type != TokenType::Identifier)
				return false;
			for (const auto& [word, type] : s_Types)
			{
				if (token.Lexeme == word)
				{
					outType = type;
					return true;
				}
			}
			return false;
		}

		bool CheckWordAt(size_t ahead, const char* word) const
		{
			const Token& token = Peek(ahead);
			return token.Type == TokenType::Identifier && token.Lexeme == word;
		}

		// 'even' or 'odd' after 'is' is the number test unless a variable, parameter or script name of
		// that word is in reach, so 'x is even' where 'even' is the script's own value compares as before.
		bool IsNumberWord() const
		{
			if (!CheckWord("even") && !CheckWord("odd"))
				return false;
			const std::string name(Current().Lexeme);
			if (ResolveLocal(m_State, name) != -1 || IsVisibleInEnclosing(name) || IsModuleName(name))
				return false;
			// A value right after it ('is even + 1') means 'even' is a name being worked with.
			const TokenType next = Peek(1).Type;
			switch (next)
			{
				case TokenType::Dot: case TokenType::Possessive: case TokenType::LeftParen: case TokenType::LeftBracket:
				case TokenType::Plus: case TokenType::Minus: case TokenType::Star: case TokenType::Slash: case TokenType::Percent: case TokenType::Caret:
					return false;
				default:
					return true;
			}
		}

		bool IsArticle() const
		{
			if (!(CheckWord("a") || CheckWord("an")))
				return false;
			const Token& next = Peek(1);
			return next.IsWord() && next.Type != TokenType::Then && next.Type != TokenType::Do &&
				next.Type != TokenType::And && next.Type != TokenType::Or && next.Type != TokenType::End;
		}

		// Whether 'is between' here is the range form (x is between 1 and 10) rather than a comparison
		// with something named 'between', which is what it meant before the range form existed. It is
		// the range form only where the old meaning could not have been intended: when the low end is a
		// plain value (a number, text, true, false or nothing, which can never follow a name), or when an
		// 'and' follows on the same line and nothing called 'between' can exist: no mention of it in this
		// script, and none in a script it uses without 'as'.
		bool BetweenIsRange()
		{
			if (!CheckWord("between"))
				return false;
			const Token& next = Peek(1);
			if (!CanStartExpression(next))
				return false;

			// Nothing can follow a name without an operator between them, so this was never a comparison.
			switch (next.Type)
			{
				case TokenType::Number: case TokenType::String: case TokenType::Interpolation:
				case TokenType::True: case TokenType::False: case TokenType::Yes: case TokenType::No: case TokenType::Nothing:
					return true;
				default:
					break;
			}

			int depth = 0;
			bool hasAnd = false;
			for (size_t i = m_Pos + 1; i < m_Tokens.size() && !hasAnd; i++)
			{
				// Text with "{ }" in it: "text{ opens, }text{ closes and reopens, and }text" closes.
				const TokenType type = m_Tokens[i].Type;
				const bool afterBrace = !m_Tokens[i].Lexeme.empty() && m_Tokens[i].Lexeme.front() == '}';
				if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace ||
					(type == TokenType::Interpolation && !afterBrace))
					depth++;
				else if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace ||
					(type == TokenType::String && afterBrace))
					depth--;
				if (depth < 0)
					break;
				if (depth > 0)
					continue;
				if (type == TokenType::And)
					hasAnd = true;
				else if (type == TokenType::Newline || type == TokenType::Eof || type == TokenType::Then || type == TokenType::Do ||
					type == TokenType::Comma || type == TokenType::Or || type == TokenType::Otherwise || type == TokenType::Else)
					break;
			}
			if (!hasAnd)
				return false;

			// A local or parameter named 'between' is written in this script too, so the scan covers it.
			return !NameMayExist("between", [](const std::vector<Token>& tokens, size_t i)
			{
				return i >= 1 && (tokens[i - 1].Type == TokenType::Is ||
					(tokens[i - 1].Type == TokenType::Not && i >= 2 && tokens[i - 2].Type == TokenType::Is));
			});
		}

		// Whether something called 'name' may exist: the word is written in this script or in one it
		// uses without 'as', other than where 'isTheForm' says it is the new form itself. Worked out once
		// per name.
		template<typename IsTheForm>
		bool NameMayExist(const std::string& name, IsTheForm isTheForm)
		{
			auto known = m_NamesMayExist.find(name);
			if (known != m_NamesMayExist.end())
				return known->second;
			std::set<std::string> visited;
			const bool mayExist = MentionsName(m_Tokens, name, isTheForm, visited);
			m_NamesMayExist[name] = mayExist;
			return mayExist;
		}

		// Whether 'name' is written anywhere in these tokens (except where 'isTheForm' says so), or in a
		// script they use without 'as' (which brings in its names). A script that can't be found counts
		// as mentioning it, to be safe.
		template<typename IsTheForm>
		bool MentionsName(const std::vector<Token>& tokens, std::string_view name, IsTheForm isTheForm, std::set<std::string>& visited)
		{
			for (size_t i = 0; i < tokens.size(); i++)
			{
				const Token& token = tokens[i];
				if (token.Type == TokenType::Identifier && token.Lexeme == name && !isTheForm(tokens, i))
					return true;
				if (token.Type == TokenType::Use && i + 1 < tokens.size() && tokens[i + 1].Type == TokenType::String)
				{
					const bool aliased = i + 2 < tokens.size() && tokens[i + 2].Type == TokenType::Identifier && tokens[i + 2].Lexeme == "as";
					const std::string path = tokens[i + 1].Text;
					if (aliased || !visited.insert(path).second)
						continue;
					std::string resolvedName;
					std::string source;
					if (!m_VM.ResolveModuleSource(path, resolvedName, source))
						return true;
					Lexer lexer(source);
					if (MentionsName(lexer.Tokenize(), name, isTheForm, visited))
						return true;
				}
			}
			return false;
		}

		void IsOperator()
		{
			Advance(); // is
			IsTail();
		}

		// What follows 'is' in a comparison ("above 5", "not empty", "between 1 and 10", "in names", or a
		// value), with the left side already on the stack. A match's choices use it too.
		void IsTail()
		{
			bool negate = Match(TokenType::Not);
			OpCode op = OpCode::Equal;

			if (BetweenIsRange())
			{
				Advance(); // between
				Term();
				if (!CheckOperator(TokenType::And))
					ErrorAtCurrent("Expected 'and' between the two ends, like: if x is between 1 and 10");
				Advance();
				Term();
				EmitOp(OpCode::Between);
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}

			if (MatchWord("above"))
				op = OpCode::Greater;
			else if (MatchWord("below"))
				op = OpCode::Less;
			else if (MatchWord("greater"))
			{
				if (!MatchWord("than"))
					ErrorAtCurrent("Expected 'than' after 'greater'.");
				op = OpCode::Greater;
			}
			else if (MatchWord("less") || MatchWord("fewer"))
			{
				if (!MatchWord("than"))
					ErrorAtCurrent("Expected 'than' after 'less'.");
				op = OpCode::Less;
			}
			else if (CheckWord("at") && (Peek(1).Lexeme == "least" || Peek(1).Lexeme == "most"))
			{
				Advance();
				op = MatchWord("least") ? OpCode::GreaterEqual : (Advance(), OpCode::LessEqual);
			}
			else if (MatchWord("equal"))
			{
				Consume(TokenType::To, "Expected 'to' after 'equal'.");
			}
			else if (IsNumberWord())
			{
				// 'x is even', 'x is odd': IsNumberWord already checked that no name 'even' or 'odd' is in reach.
				const bool even = Current().Lexeme == "even";
				Advance();
				EmitOp(OpCode::NumberTest);
				EmitByte(static_cast<uint8_t>(even ? NumberTest::Even : NumberTest::Odd));
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}
			else if (IsArticle() && CheckWordAt(1, "whole") && CheckWordAt(2, "number"))
			{
				// 'x is a whole number': 'a whole' followed by 'number' was always a mistake.
				Advance();
				Advance();
				Advance();
				EmitOp(OpCode::NumberTest);
				EmitByte(static_cast<uint8_t>(NumberTest::Whole));
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}
			else if (IsArticle())
			{
				Advance();
				TypeCheck type;
				if (IsTypeWord(Current(), type))
				{
					Advance();
					EmitOp(OpCode::IsType);
					EmitByte(static_cast<uint8_t>(type));
				}
				else
				{
					Term();
					EmitOp(OpCode::IsKind);
				}
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}
			else if (Match(TokenType::In))
			{
				Term();
				EmitOp(OpCode::In);
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}
			else if (CheckWord("empty") && !(ResolveLocal(m_State, "empty") != -1))
			{
				Advance();
				EmitOp(OpCode::IsEmpty);
				if (negate)
					EmitOp(OpCode::Not);
				return;
			}

			Term();
			EmitOp(op);
			if (negate)
				EmitOp(OpCode::Not);
		}

		void Comparison()
		{
			Term();
			ComparisonRest();
		}

		// The comparisons after a value already on the stack: 'is above 3', 'contains x', '< 5'.
		void ComparisonRest()
		{
			while (true)
			{
				if (CheckOperator(TokenType::Is))
				{
					IsOperator();
					continue;
				}
				if (CheckOperator(TokenType::Contains))
				{
					Advance();
					Term();
					EmitOp(OpCode::Contains);
					continue;
				}

				OpCode op;
				switch (Current().Type)
				{
					case TokenType::EqualEqual:   op = OpCode::Equal; break;
					case TokenType::BangEqual:    op = OpCode::NotEqual; break;
					case TokenType::Less:         op = OpCode::Less; break;
					case TokenType::LessEqual:    op = OpCode::LessEqual; break;
					case TokenType::Greater:      op = OpCode::Greater; break;
					case TokenType::GreaterEqual: op = OpCode::GreaterEqual; break;
					case TokenType::Equal:
						FixTokens(Current(), Current(), "Change '=' to 'is'", "is");
						ErrorAtCurrent("'=' is not used in ForgeScript. Use 'is' to compare, like: if health is 0");
					default:
						return;
				}
				Advance();
				Term();
				EmitOp(op);
			}
		}

		void Term()
		{
			Factor();
			while (Check(TokenType::Plus) || Check(TokenType::Minus))
			{
				bool plus = Check(TokenType::Plus);
				Advance();
				Factor();
				EmitOp(plus ? OpCode::Add : OpCode::Subtract);
			}
		}

		void Factor()
		{
			Unary();
			while (Check(TokenType::Star) || Check(TokenType::Slash) || Check(TokenType::Percent))
			{
				TokenType type = Current().Type;
				Advance();
				Unary();
				EmitOp(type == TokenType::Star ? OpCode::Multiply : type == TokenType::Slash ? OpCode::Divide : OpCode::Modulo);
			}
		}

		void Unary()
		{
			if (Match(TokenType::Minus))
			{
				Nested nested(*this);
				Unary();
				EmitOp(OpCode::Negate);
				return;
			}
			PowerExpression();
		}

		void PowerExpression()
		{
			OfExpression();
			if (Match(TokenType::Caret))
			{
				Nested nested(*this);
				Unary();
				EmitOp(OpCode::Power);
			}
		}

		// "length of names" calls length(names).
		void OfExpression()
		{
			Postfix();
			if (CheckOperator(TokenType::Of))
			{
				Advance();
				Unary();
				m_LastCallPos = CurrentChunk().Size();
				EmitOp(OpCode::Call);
				EmitByte(1);
			}
		}

		// The values of a call. Values can be named, after the others: attack(goblin, damage: 25). Names
		// are matched to the function's parameters when it runs (NamedArgs, just before the call).
		uint8_t ArgumentList()
		{
			PushStopWords(nullptr);
			int count = 0;
			std::vector<std::string> names;
			if (!Check(TokenType::RightParen))
			{
				do
				{
					if (Check(TokenType::Identifier) && Peek(1).Type == TokenType::Colon)
					{
						const Token nameToken = Current();
						std::string name(nameToken.Lexeme);
						if (std::find(names.begin(), names.end(), name) != names.end())
							ErrorAt(nameToken, "'" + name + "' is named twice in this call.");
						names.push_back(std::move(name));
						Advance();
						Advance();
					}
					else if (!names.empty())
					{
						ErrorAtCurrent("Put the named values after the others, like: attack(goblin, damage: 25)");
					}
					Expression();
					if (count == 255)
						Error("A function can take at most 255 values.");
					count++;
				} while (Match(TokenType::Comma));
			}
			PopStopWords();
			Consume(TokenType::RightParen, "Expected ')' after the values.");
			if (!names.empty())
			{
				Value list = m_VM.MakeList();
				// Interned names, so they compare by pointer like other names.
				for (const std::string& name : names)
					As<List>(list)->Items.push_back(Value::FromObject(m_VM.Intern(name)));
				EmitOp(OpCode::NamedArgs);
				EmitU16(MakeConstant(list));
			}
			return static_cast<uint8_t>(count);
		}

		void MemberAccess()
		{
			std::string name = ConsumeMemberName();
			if (Check(TokenType::LeftParen) && !m_NoInvoke)
			{
				Advance();
				uint8_t count = ArgumentList();
				EmitWithName(OpCode::Invoke, name);
				EmitByte(count);
			}
			else
			{
				EmitWithName(OpCode::GetProperty, name);
			}
		}

		void Postfix()
		{
			Primary();
			while (true)
			{
				if (Match(TokenType::LeftParen))
				{
					uint8_t count = ArgumentList();
					m_LastCallPos = CurrentChunk().Size();
					EmitOp(OpCode::Call);
					EmitByte(count);
				}
				else if (Match(TokenType::Dot) || Match(TokenType::Possessive))
				{
					MemberAccess();
				}
				else if (Match(TokenType::LeftBracket))
				{
					PushStopWords(nullptr);
					Expression();
					PopStopWords();
					Consume(TokenType::RightBracket, "Expected ']' after the position.");
					EmitOp(OpCode::GetIndex);
				}
				else
				{
					break;
				}
			}
		}

		void InterpolatedText()
		{
			int parts = 0;
			while (Check(TokenType::Interpolation))
			{
				if (!Current().Text.empty())
				{
					EmitConstant(Value::FromObject(m_VM.Intern(Current().Text)));
					parts++;
				}
				Advance();
				PushStopWords(nullptr);
				Expression();
				PopStopWords();
				parts++;
			}
			if (!Check(TokenType::String))
			{
				// "{price:.2}" is how other languages pick decimals.
				if (Check(TokenType::Colon))
				{
					const std::string value = Previous().Type == TokenType::Identifier ? std::string(Previous().Lexeme) : "price";
					ErrorAtCurrent("Expected '}' to close the {value} inside this text. To show a number with 2 decimals, write: {format_number(" +
						value + ", 2)}");
				}
				ErrorAtCurrent("Expected '}' to close the {value} inside this text.");
			}
			if (!Current().Text.empty() || parts == 0)
			{
				EmitConstant(Value::FromObject(m_VM.Intern(Current().Text)));
				parts++;
			}
			Advance();
			if (parts > 255)
				Error("This text has too many {values} inside it.");
			EmitOp(OpCode::Interpolate);
			EmitByte(static_cast<uint8_t>(parts));
		}

		void ListLiteral()
		{
			PushStopWords(nullptr);
			EmitOp(OpCode::NewList);
			SkipNewlines();
			while (!Check(TokenType::RightBracket))
			{
				Expression();
				EmitOp(OpCode::ListAppend);
				if (!Match(TokenType::Comma))
					break;
			}
			Consume(TokenType::RightBracket, "Expected ']' at the end of the list.");
			PopStopWords();
		}

		void MapLiteral()
		{
			PushStopWords(nullptr);
			EmitOp(OpCode::NewMap);
			while (!Check(TokenType::RightBrace))
			{
				if (Current().IsWord() && Peek(1).Type == TokenType::Colon)
				{
					EmitConstant(Value::FromObject(m_VM.Intern(std::string(Current().Lexeme))));
					Advance();
				}
				else if (Match(TokenType::LeftBracket))
				{
					Expression();
					Consume(TokenType::RightBracket, "Expected ']' after the key.");
				}
				else if (Check(TokenType::String) || Check(TokenType::Number))
				{
					Primary();
				}
				else
				{
					ErrorAtCurrent("Expected a key, like:  { name: \"Bob\", health: 10 }");
				}
				Consume(TokenType::Colon, "Expected ':' between the key and its value.");
				Expression();
				EmitOp(OpCode::MapSet);
				if (!Match(TokenType::Comma))
					break;
			}
			Consume(TokenType::RightBrace, "Expected '}' at the end of the map.");
			PopStopWords();
		}

		void NewExpression()
		{
			if (!Check(TokenType::Identifier))
				ErrorAtCurrent("Expected the name of a kind after 'new', like: new Enemy()");
			const size_t at = m_Pos;
			std::string name = ConsumeName("Expected the name of a kind.");
			EmitGet(ResolveName(name, at));
			while (Match(TokenType::Dot))
				EmitWithName(OpCode::GetProperty, ConsumeMemberName());

			uint8_t count = 0;
			if (Match(TokenType::LeftParen))
				count = ArgumentList();
			EmitOp(OpCode::New);
			EmitByte(count);
		}

		// Whether a word names nothing a script could mean by it: no local, parameter, enclosing local,
		// script name (in this script or one it uses) or built-in function. Only such words take the
		// sentence forms below, so a script's own 'first' or 'the' keeps its meaning.
		bool IsFreeWord(const std::string& name) const
		{
			return ResolveLocal(m_State, name) == -1 && !IsVisibleInEnclosing(name) && !IsModuleName(name) &&
				!IsBuiltinFunction(name);
		}

		// Sentence forms that read a list, each an error before: 'the' before a name is left out
		// ('the length of items' is length(items)); 'first of items' and 'the last item of items' are
		// items.first and items.last; 'item 2 of items' is items[2].
		bool TryOfPhrase()
		{
			const std::string word(Current().Lexeme);
			// Each form reads another value inside itself, so each counts as a level (audit S-4).
			if (word == "the" && Peek(1).Type == TokenType::Identifier && IsFreeWord(word))
			{
				Nested nested(*this);
				Advance();
				Primary();
				return true;
			}

			if ((word == "first" || word == "last") && IsFreeWord(word))
			{
				const size_t ofAt = CheckWordAt(1, "item") ? 2 : 1;
				if (Peek(ofAt).Type != TokenType::Of)
					return false;
				Nested nested(*this);
				for (size_t i = 0; i <= ofAt; i++)
					Advance();
				Unary();
				EmitWithName(OpCode::GetProperty, word);
				return true;
			}

			if (word == "item" && IsFreeWord(word))
			{
				const TokenType next = Peek(1).Type;
				if (next != TokenType::Number && next != TokenType::Identifier && next != TokenType::LeftParen)
					return false;
				const size_t ofPos = FindOfOnThisLine(m_Pos + 1);
				if (ofPos == 0)
					return false;

				// The list goes on the stack before the position, so read it first, then come back.
				Nested nested(*this);
				const size_t indexStart = m_Pos + 1;
				m_Pos = ofPos + 1;
				Unary();
				const size_t listEnd = m_Pos;
				m_Pos = indexStart;
				Postfix();
				if (m_Pos != ofPos)
					ErrorAtCurrent("Put brackets around a worked-out position, like: item (i + 1) of items");
				m_Pos = listEnd;
				EmitOp(OpCode::GetIndex);
				return true;
			}
			return false;
		}

		// 'every enemy in enemies is above 0' and 'any word in words contains "a"': list.all and list.any
		// with the comparison after the list, worked out for each item under the name. Only where
		// 'every' or 'any' names nothing in reach, a name and 'in' follow, and the line isn't a timer
		// ('every' with a time word at the end); each was an error before.
		bool TryEveryOrAny()
		{
			const std::string word(Current().Lexeme);
			if ((word != "every" && word != "any") || Peek(1).Type != TokenType::Identifier || Peek(2).Type != TokenType::In ||
				!IsFreeWord(word))
				return false;
			Nested nested(*this);
			const std::string name(Peek(1).Lexeme);
			const size_t nameAt = m_Pos + 1;
			Advance();
			Advance();
			Advance();
			Term();

			Function* fn = m_VM.NewFunction();
			fn->Name = m_VM.Intern(word + " " + name);
			fn->Owner = m_Module;
			fn->Arity = 1;
			FunctionState state;
			state.Enclosing = m_State;
			state.Fn = fn;
			state.Kind = ScopeKind::Function;
			m_State = &state;
			BeginScope();
			state.Locals.push_back({ "", state.ScopeDepth, false });
			NameLocal(0);
			AddLocal(name, nameAt);
			fn->ParamNames = { name };
			DefaultValues(fn, fn->ParamNames, {});
			fn->BodyIp = CurrentChunk().Size();

			const TokenType next = Current().Type;
			if (next != TokenType::Is && next != TokenType::Contains && next != TokenType::EqualEqual && next != TokenType::BangEqual &&
				next != TokenType::Less && next != TokenType::LessEqual && next != TokenType::Greater && next != TokenType::GreaterEqual)
			{
				ErrorAtCurrent("After '" + word + " " + name + " in ...', say what each one is, like: " + word + " " + name +
					" in list is above 0");
			}
			EmitOp(OpCode::GetLocal);
			EmitByte(1);
			if (!TryFieldIsTrue())
				ComparisonRest();
			EmitOp(OpCode::Return);

			m_State = state.Enclosing;
			EmitOp(OpCode::Closure);
			EmitU16(MakeConstant(Value::FromObject(fn)));
			for (const UpvalueInfo& upvalue : state.Upvalues)
			{
				EmitByte(upvalue.IsLocal ? 1 : 0);
				EmitByte(upvalue.Index);
			}
			EmitWithName(OpCode::Invoke, word == "every" ? "all" : "any");
			EmitByte(1);
			return true;
		}

		// Inside 'every'/'any': 'is defeated' (or 'is not defeated') with nothing after it, where
		// 'defeated' names nothing in reach, reads the item's field: enemy.defeated.
		bool TryFieldIsTrue()
		{
			if (!Check(TokenType::Is))
				return false;
			const bool negate = Peek(1).Type == TokenType::Not;
			const size_t at = negate ? 2 : 1;
			const Token& field = Peek(at);
			if (field.Type != TokenType::Identifier || field.Lexeme == "empty" || field.Lexeme == "even" || field.Lexeme == "odd" ||
				!IsFreeWord(std::string(field.Lexeme)))
				return false;
			switch (Peek(at + 1).Type)
			{
				case TokenType::Then: case TokenType::Do: case TokenType::And: case TokenType::Or: case TokenType::Newline:
				case TokenType::Eof: case TokenType::RightParen: case TokenType::RightBracket: case TokenType::Comma:
				case TokenType::Otherwise: case TokenType::Else:
					break;
				default:
					return false;
			}
			const std::string name(field.Lexeme);
			for (size_t i = 0; i <= at; i++)
				Advance();
			EmitWithName(OpCode::GetProperty, name);
			if (negate)
				EmitOp(OpCode::Not);
			return true;
		}

		// The position of the first 'of' outside brackets from 'start' to the end of the line, or 0.
		size_t FindOfOnThisLine(size_t start) const
		{
			int depth = 0;
			for (size_t i = start; i < m_Tokens.size(); i++)
			{
				const TokenType type = m_Tokens[i].Type;
				if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace)
					depth++;
				else if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace)
				{
					if (--depth < 0)
						return 0;
				}
				else if (depth == 0 && type == TokenType::Of)
					return i;
				else if (type == TokenType::Newline || type == TokenType::Eof)
					return 0;
			}
			return 0;
		}

		void Primary()
		{
			const Token& token = Current();
			switch (token.Type)
			{
				case TokenType::Number:
					Advance();
					EmitConstant(Value::FromNumber(Previous().Number));
					return;
				case TokenType::String:
					Advance();
					EmitConstant(Value::FromObject(m_VM.Intern(Previous().Text)));
					return;
				case TokenType::Interpolation:
					InterpolatedText();
					return;
				case TokenType::True: case TokenType::Yes:
					Advance();
					EmitOp(OpCode::True);
					return;
				case TokenType::False: case TokenType::No:
					Advance();
					EmitOp(OpCode::False);
					return;
				case TokenType::Nothing:
					Advance();
					EmitOp(OpCode::Nothing);
					return;
				case TokenType::Identifier:
				{
					if (TryPhrase(false))
						return;
					if (TryOfPhrase())
						return;
					if (TryEveryOrAny())
						return;
					std::string name(token.Lexeme);
					const size_t nameIndex = m_Pos;
					Advance();
					const Target target = ResolveName(name, nameIndex);
					WarnIfCallFindsOwnValue(name, target, nameIndex);
					if (target.Type == TargetType::Global)
					{
						CheckChoiceName(name);
						if (m_Options.Warnings && Check(TokenType::LeftParen))
							NoteCallValues(name, m_Pos);
					}
					EmitGet(target);
					return;
				}
				case TokenType::Self:
					Advance();
					EmitSelf();
					return;
				case TokenType::My:
					Advance();
					EmitSelf();
					MemberAccess();
					return;
				case TokenType::Parent:
				{
					Token keyword = token;
					Advance();
					if (m_State->Kind != ScopeKind::Method)
						ErrorAt(keyword, "'parent' can only be used inside a function that belongs to a kind.");
					Consume(TokenType::Dot, "Expected '.' after 'parent', like: parent.setup()");
					std::string name = ConsumeMemberName();
					EmitSelf();
					if (Match(TokenType::LeftParen))
					{
						uint8_t count = ArgumentList();
						EmitWithName(OpCode::InvokeParent, name);
						EmitByte(count);
					}
					else
					{
						EmitWithName(OpCode::GetParent, name);
					}
					return;
				}
				case TokenType::LeftParen:
					Advance();
					PushStopWords(nullptr);
					Expression();
					PopStopWords();
					Consume(TokenType::RightParen, "Expected ')' to close the '('.");
					return;
				case TokenType::LeftBracket:
					Advance();
					ListLiteral();
					return;
				case TokenType::LeftBrace:
					Advance();
					MapLiteral();
					return;
				case TokenType::Function:
					Advance();
					LambdaExpression();
					return;
				case TokenType::New:
					Advance();
					NewExpression();
					return;
				case TokenType::Newline:
				case TokenType::Eof:
					// 'x--' ends the line after the second '-'.
					if (Previous().Type == TokenType::Minus && m_Pos >= 2 && m_Tokens[m_Pos - 2].Type == TokenType::Minus && m_Pos >= 3)
					{
						const std::string target(m_Tokens[m_Pos - 3].Lexeme);
						FixGuessedValue(m_Pos - 3, "take 1 from " + target, "Write: take 1 from " + target);
						ErrorAtCurrent("Expected a value here, but the line ended. ForgeScript has no --. Write: take 1 from " +
							std::string(m_Tokens[m_Pos - 3].Lexeme));
					}
					ErrorAtCurrent("Expected a value here, but the line ended.");
				default:
					break;
			}

			if (token.IsWord())
				ErrorAtCurrent("Expected a value here, but found the keyword '" + std::string(token.Lexeme) + "'.");
			FixGuessedValueAtCurrent();
			ErrorAtCurrent("Expected a value here, but found '" + std::string(token.Lexeme) + "'." + GuessedValueHint());
		}

	private:
		VM& m_VM;
		Module* m_Module;
		std::vector<Token> m_Tokens;
		size_t m_Pos = 0;
		// For CompileOptions::NameUses: the tokens ConsumeName read, the tokens already noted, and the
		// next Local::Id.
		std::vector<size_t> m_ConsumedNames;
		std::unordered_set<size_t> m_NotedTokens;
		int m_NextLocalId = 0;
		std::string m_FileName;
		CompileOptions m_Options;
		PhraseTable m_Phrases;
		std::unordered_set<std::string> m_ModuleNames;
		// Each 'choices State are idle, running' of this script: the set's name and its choices, in order.
		std::unordered_map<std::string, std::vector<std::string>> m_Choices;
		std::unordered_map<std::string, std::unordered_set<std::string>> m_ChoiceSets; // the same names, to look up
		bool m_ChoicesIsPhrase = false;
		bool m_TestIsPhrase = false;
		bool m_ExpectIsPhrase = false;
		// Set by ParameterList when its last name ends in '...', for the FunctionBody that follows.
		bool m_RestParam = false;
		// The types ('amount as number') of the parameter list ParameterList last read, one per parameter
		// ("" for none); and, for checkers, each script function's names and types, and the plain values
		// calls by name give, to compare once every function is known.
		std::vector<std::string> m_ParamTypes;
		struct FunctionTypes
		{
			std::vector<std::string> Params;
			std::vector<std::string> Types;
		};
		std::unordered_map<std::string, FunctionTypes> m_FunctionTypes;
		struct CallValue
		{
			std::string Name;      // the parameter's name, for a named value; empty when given in order
			size_t Position = 0;   // its place among the values given in order
			std::string Type;      // the plain value's type ("number", "text", ... "nothing")
			size_t Token = 0;
		};
		struct NotedCall
		{
			std::string Function;
			std::vector<CallValue> Values;
		};
		std::vector<NotedCall> m_NotedCalls;
		FunctionState* m_State = nullptr;
		std::vector<const std::set<std::string>*> m_StopWords;
		bool m_NoInvoke = false;
		// Right after an 'end' (at token m_ClosingPos), code belongs to m_ClosingLine; see CodeLine.
		size_t m_ClosingPos = 0;
		int m_ClosingLine = 0;
		size_t m_LastCallPos = static_cast<size_t>(-1);
		// Whether something with a name may exist (see NameMayExist), worked out once per name.
		std::unordered_map<std::string, bool> m_NamesMayExist;
		// The 'finally' tokens that end a try or catch section.
		std::set<size_t> m_FinallyMarkers;
		int m_MatchDepth = 0; // open match blocks, where a line starting with 'is' starts the next choice
		// How deeply the code being read is nested (brackets, lists, calls, blocks, 'not' and '-' chains).
		// The parser goes one level down the C++ stack for each, so a limit keeps a hostile file from
		// overflowing it. Frames differ in size between compilers, so the stack used since Compile began
		// is limited too, well inside the 1 MB a Windows program's main thread gets.
		static constexpr int c_MaxNesting = 256;
		static constexpr std::uintptr_t c_MaxStackBytes = 512 * 1024;
		int m_Nesting = 0;
		std::uintptr_t m_StackStart = 0;

		// An address in the current frame of the real stack. Not a local's address: sanitizers may keep
		// locals on a separate "fake stack".
		static std::uintptr_t StackAddress()
		{
#if defined(_MSC_VER)
			return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());
#else
			return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#endif
		}

		bool StackIsDeep() const
		{
			const std::uintptr_t now = StackAddress();
			const std::uintptr_t used = now < m_StackStart ? m_StackStart - now : now - m_StackStart;
			return m_StackStart != 0 && used > c_MaxStackBytes;
		}

		class Nested
		{
		public:
			explicit Nested(Compiler& compiler) : m_Compiler(compiler)
			{
				if (m_Compiler.m_Nesting >= c_MaxNesting || m_Compiler.StackIsDeep())
					m_Compiler.ErrorAtCurrent("This is nested too deeply (more than " + std::to_string(c_MaxNesting) +
						" levels of brackets or blocks). Split it into smaller pieces, like a function for the inner part.");
				m_Compiler.m_Nesting++;
			}
			~Nested() { m_Compiler.m_Nesting--; }
			Nested(const Nested&) = delete;
			Nested& operator=(const Nested&) = delete;
		private:
			Compiler& m_Compiler;
		};
		bool m_ChoicelessMatchIsBlock = false; // set once the phrases are known
		size_t m_StatementStart = 0;           // where the statement being compiled starts, for hints
		bool m_EveryIsPhrase = false;          // set once the phrases are known
		bool m_AfterIsPhrase = false;
		std::set<std::string> m_ListVerbPhrases;  // 'sort', 'shuffle' or 'reverse' when a phrase starts with it
		std::set<std::string> m_ScriptPhraseWords; // first words of phrases the scripts make (those may wait)
		static constexpr size_t c_NoResume = static_cast<size_t>(-1);
		size_t m_ResumeAfter = c_NoResume; // where to go on after the mistake being reported, if past its statement
		CompileError m_Error;
		CompileError m_Fix;   // only its Fix fields: the change the next ErrorAt offers (see FixTokens)
		Token m_MisplacedEndOpener;   // the last block that took an 'end' indented less than itself (see FixMissingEnd)
		std::vector<CompileError>* m_AllErrors = nullptr;
		// A file with this many mistakes is probably not ForgeScript at all; stop there.
		static constexpr size_t c_MaxCollectedErrors = 100;
	};

	Function* CompileScript(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options, CompileError& outError)
	{
		Lexer lexer(source);
		std::vector<Token> tokens = lexer.Tokenize();
		Compiler compiler(vm, module, std::move(tokens), fileName, options);
		return compiler.Compile(outError);
	}

	std::vector<CompileError> FindCompileErrors(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options)
	{
		Lexer lexer(source);
		std::vector<Token> tokens = lexer.Tokenize();
		Compiler compiler(vm, module, std::move(tokens), fileName, options);
		return compiler.FindErrors();
	}

}
