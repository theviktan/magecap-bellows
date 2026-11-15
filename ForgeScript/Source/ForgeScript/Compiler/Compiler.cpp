#include "ForgeScript/Compiler/Compiler.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Compiler/Phrase.h"
#include "ForgeScript/Core/Object.h"
#include "ForgeScript/VM/VM.h"

#include <algorithm>
#include <set>
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
		};

		struct UpvalueInfo
		{
			uint8_t Index = 0;
			bool IsLocal = false;
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

		struct FunctionState
		{
			FunctionState* Enclosing = nullptr;
			Function* Fn = nullptr;
			ScopeKind Kind = ScopeKind::Script;
			std::vector<Local> Locals;
			std::vector<UpvalueInfo> Upvalues;
			std::vector<LoopInfo> Loops;
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

	class Compiler
	{
	public:
		Compiler(VM& vm, Module* module, std::vector<Token> tokens, const std::string& fileName, const CompileOptions& options)
			: m_VM(vm), m_Module(module), m_Tokens(std::move(tokens)), m_FileName(fileName), m_Options(options),
			  m_Phrases(vm.GetPhraseTable())
		{
			m_StopWords.push_back(&s_NoStopWords);
		}

		Function* Compile(CompileError& outError)
		{
			try
			{
				for (const Token& token : m_Tokens)
				{
					if (token.Type == TokenType::Error)
						ErrorAt(token, token.Text);
				}

				std::unordered_set<std::string> visited;
				PrescanPhrases(m_Tokens, visited);
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
					Statement();
				}

				m_Pos = 0;
				size_t nextHoisted = 0;
				SkipNewlines();
				while (!Check(TokenType::Eof))
				{
					if (nextHoisted < hoisted.size() && m_Pos == hoisted[nextHoisted].first)
					{
						m_Pos = hoisted[nextHoisted].second;
						nextHoisted++;
						EndOfStatement();
						SkipNewlines();
						continue;
					}
					Statement();
					SkipNewlines();
				}

				EmitOp(OpCode::Nothing);
				EmitOp(OpCode::Return);
				m_State = nullptr;
				return fn;
			}
			catch (const CompileAbort&)
			{
				outError = m_Error;
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
			Advance();
			return name;
		}

		// Property and method names may be any word, including keywords (list.add, map.end ...).
		std::string ConsumeMemberName()
		{
			if (!Current().IsWord())
				ErrorAtCurrent("Expected a property name after '.'.");
			std::string name(Current().Lexeme);
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
				default:
					return false;
			}
		}

		void EndOfStatement()
		{
			if (Match(TokenType::Newline) || IsBlockEnd())
				return;
			if (Check(TokenType::Equal))
				ErrorAtCurrent("'=' is not used in ForgeScript. Use 'set x to 5' to change a value, or 'is' to compare.");
			ErrorAtCurrent("Expected the end of the line, but found '" + std::string(Current().Lexeme) + "'.");
		}

		// ------------------------------------------------------------------ Errors

		[[noreturn]] void ErrorAt(const Token& token, const std::string& message)
		{
			m_Error.Message = message;
			m_Error.File = m_FileName;
			m_Error.Line = token.Line;
			m_Error.Column = token.Column;
			throw CompileAbort();
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

			switch (token.Type)
			{
				case TokenType::If: case TokenType::While: case TokenType::Repeat: case TokenType::For:
				case TokenType::Function: case TokenType::Phrase: case TokenType::When: case TokenType::Try:
					return true;
				case TokenType::Until:
					return !(i > 0 && tokens[i - 1].Type == TokenType::Wait);
				case TokenType::Kind:
					return !(i > 0 && tokens[i - 1].Type == TokenType::Identifier &&
						(tokens[i - 1].Lexeme == "a" || tokens[i - 1].Lexeme == "an"));
				default:
					return false;
			}
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
		void ScanScope(size_t start, bool isScript, std::vector<std::string>& outNames) const
		{
			int depth = 0;
			size_t i = start;
			while (i < m_Tokens.size() && m_Tokens[i].Type != TokenType::Eof)
			{
				const Token& token = m_Tokens[i];
				if (IsBlockOpener(m_Tokens, i))
				{
					bool functionLike = token.Type == TokenType::Function || token.Type == TokenType::Phrase ||
						token.Type == TokenType::When || token.Type == TokenType::Kind;
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
						m_Phrases.Add(pattern);
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

		void EmitByte(uint8_t byte) { CurrentChunk().Write(byte, Previous().Line); }
		void EmitOp(OpCode op) { CurrentChunk().Write(op, Previous().Line); }
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
				EmitOp(locals.back().IsCaptured ? OpCode::CloseUpvalue : OpCode::Pop);
				locals.pop_back();
			}
		}

		// Declares a local variable for the value that is currently on top of the stack.
		int AddLocal(const std::string& name)
		{
			if (m_State->Locals.size() >= 256)
				Error("Too many variables in one function (the limit is 255). Split it into smaller functions.");
			m_State->Locals.push_back({ name, m_State->ScopeDepth, false });
			return static_cast<int>(m_State->Locals.size()) - 1;
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

		int AddUpvalue(FunctionState* state, uint8_t index, bool isLocal)
		{
			for (size_t i = 0; i < state->Upvalues.size(); i++)
			{
				if (state->Upvalues[i].Index == index && state->Upvalues[i].IsLocal == isLocal)
					return static_cast<int>(i);
			}
			if (state->Upvalues.size() >= 256)
				Error("Too many outside variables used by one function (the limit is 255).");
			state->Upvalues.push_back({ index, isLocal });
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
				return AddUpvalue(state, static_cast<uint8_t>(local), true);
			}

			int upvalue = ResolveUpvalue(state->Enclosing, name);
			if (upvalue != -1)
				return AddUpvalue(state, static_cast<uint8_t>(upvalue), false);
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

		Target ResolveName(const std::string& name)
		{
			Target target;
			int local = ResolveLocal(m_State, name);
			if (local != -1)
			{
				target.Type = TargetType::Local;
				target.Slot = local;
				return target;
			}
			int upvalue = ResolveUpvalue(m_State, name);
			if (upvalue != -1)
			{
				target.Type = TargetType::Upvalue;
				target.Slot = upvalue;
				return target;
			}
			target.Type = TargetType::Global;
			target.Name = NameConstant(name);
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
				std::string name = ConsumeName("Expected a variable name.");
				target = ResolveName(name);
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
				default:
					if (TryPhrase(true))
						EmitOp(OpCode::Pop);
					else
						ExpressionStatement();
					break;
			}
			EndOfStatement();
		}

		// Parses statements until 'end', 'else', 'otherwise', 'catch' or the end of the file.
		void Block()
		{
			SkipNewlines();
			while (!IsBlockEnd())
			{
				Statement();
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
				Advance();
				return;
			}
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

		void SetStatement()
		{
			Advance(); // set
			if (m_State->Kind == ScopeKind::Script && Check(TokenType::Identifier) && Peek(1).Type == TokenType::To)
			{
				std::string name(Current().Lexeme);
				if (ResolveLocal(m_State, name) == -1)
				{
					// A script level variable: "set score to 0".
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
				ErrorAtCurrent("Use 'to' instead of '=', like: set health to 100");
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
				PushLoop(loopStart, true);
				ScopedBlock();
				EmitLoop(loopStart);
				PopLoop();
				ConsumeEnd(opener, "'repeat' loop");
				return;
			}

			if (Check(TokenType::While) || Check(TokenType::Until))
				ErrorAtCurrent("Write 'while ... do' or 'until ... do' without 'repeat' in front.");

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

			LoopInfo& loop = m_State->Loops.back();
			for (int i = m_State->TryDepth; i > loop.TryDepth; i--)
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
		void FunctionBody(const std::string& name, ScopeKind kind, const std::vector<std::string>& params, const Token& opener, const char* what)
		{
			Function* fn = m_VM.NewFunction();
			fn->Name = m_VM.Intern(name);
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
			for (const std::string& param : params)
			{
				if (ResolveLocal(m_State, param) != -1)
					ErrorAt(opener, "The name '" + param + "' is used twice in the same parameter list.");
				AddLocal(param);
			}
			if (params.size() > 255)
				ErrorAt(opener, "A function can take at most 255 values.");

			// Every name this function assigns with "set" becomes one of its own variables,
			// unless it already belongs to the whole script or to an outer function.
			std::vector<std::string> names;
			ScanScope(m_Pos, false, names);
			for (const std::string& local : names)
			{
				if (ResolveLocal(m_State, local) != -1 || IsModuleName(local) || IsVisibleInEnclosing(local))
					continue;
				EmitOp(OpCode::Nothing);
				AddLocal(local);
			}

			Block();
			ConsumeEnd(opener, what);
			EmitOp(OpCode::Nothing);
			EmitOp(OpCode::Return);

			m_State = state.Enclosing;
			EmitOp(OpCode::Closure);
			EmitU16(MakeConstant(Value::FromObject(fn)));
			for (const UpvalueInfo& upvalue : state.Upvalues)
			{
				EmitByte(upvalue.IsLocal ? 1 : 0);
				EmitByte(upvalue.Index);
			}
		}

		std::vector<std::string> ParameterList()
		{
			std::vector<std::string> params;
			Consume(TokenType::LeftParen, "Expected '(' after the function name, like: function heal(amount)");
			if (!Check(TokenType::RightParen))
			{
				do
				{
					params.push_back(ConsumeName("Expected a parameter name."));
				} while (Match(TokenType::Comma));
			}
			Consume(TokenType::RightParen, "Expected ')' after the parameters.");
			return params;
		}

		// Stores the value on top of the stack under a name (script variable or local variable).
		void DefineName(const std::string& name)
		{
			if (m_State->Kind == ScopeKind::Script && m_State->ScopeDepth == 0)
			{
				EmitWithName(OpCode::SetGlobal, name);
				EmitOp(OpCode::Pop);
			}
			else
			{
				int existing = ResolveLocal(m_State, name);
				if (existing != -1 && m_State->Locals[static_cast<size_t>(existing)].Depth == m_State->ScopeDepth)
				{
					EmitOp(OpCode::SetLocal);
					EmitByte(static_cast<uint8_t>(existing));
					EmitOp(OpCode::Pop);
				}
				else if (m_State->Kind == ScopeKind::Script && ResolveLocal(m_State, name) == -1)
				{
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
			std::string name = ConsumeName("Expected a function name.");

			bool isLocal = !(m_State->Kind == ScopeKind::Script);
			int slot = -1;
			if (isLocal)
			{
				// Declare first so the function can call itself.
				EmitOp(OpCode::Nothing);
				slot = AddLocal(name);
			}

			std::vector<std::string> params = ParameterList();
			FunctionBody(name, ScopeKind::Function, params, opener, "function");

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
			std::vector<std::string> params = ParameterList();
			FunctionBody("function", ScopeKind::Function, params, opener, "function");
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
			for (int i = m_State->TryDepth; i > 0; i--)
				EmitOp(OpCode::TryEnd);
			EmitOp(OpCode::Return);
		}

		void KindStatement()
		{
			Token opener = Current();
			Advance();
			std::string name = ConsumeName("Expected a name for the kind, like: kind Enemy");

			EmitWithName(OpCode::Kind, name);
			bool isGlobal = m_State->Kind == ScopeKind::Script;
			if (isGlobal)
			{
				EmitWithName(OpCode::SetGlobal, name);
			}
			else
			{
				int slot = AddLocal(name);
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

			// First pass: compile methods, remember where the field lines are.
			std::vector<size_t> fieldLines;
			SkipNewlines();
			while (!Check(TokenType::End))
			{
				if (Check(TokenType::Eof))
					ErrorAt(opener, "This kind is missing its 'end'.");

				if (Check(TokenType::Set))
				{
					fieldLines.push_back(m_Pos);
					while (!Check(TokenType::Newline) && !Check(TokenType::Eof) && !Check(TokenType::End))
						Advance();
				}
				else if (Check(TokenType::Function))
				{
					Token methodOpener = Current();
					Advance();
					std::string methodName = ConsumeMemberName();
					std::vector<std::string> params = ParameterList();
					FunctionBody(methodName, ScopeKind::Method, params, methodOpener, "function");
					EmitWithName(OpCode::Method, methodName);
				}
				else
				{
					ErrorAtCurrent("Inside a kind, use 'set name to value' for fields and 'function' for methods.");
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

			FunctionState state;
			state.Enclosing = m_State;
			state.Fn = fields;
			state.Kind = ScopeKind::FieldInit;
			m_State = &state;
			BeginScope();
			state.Locals.push_back({ "self", state.ScopeDepth, false });

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

		void TryStatement()
		{
			Token opener = Current();
			Advance();
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
			std::string event = EventName("when game starts");
			std::vector<std::string> params;
			if (Match(TokenType::With) || Match(TokenType::By))
			{
				do
				{
					params.push_back(ConsumeName("Expected a name for the event value."));
				} while (Match(TokenType::Comma) || Match(TokenType::And));
			}
			Match(TokenType::Do);
			FunctionBody("when " + event, ScopeKind::Event, params, opener, "'when'");
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

		void StartStatement()
		{
			Token keyword = Current();
			Advance();
			m_NoInvoke = true;
			m_LastCallPos = static_cast<size_t>(-1);
			Expression();
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

		void Expression() { OrExpression(); }

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

		bool IsArticle() const
		{
			if (!(CheckWord("a") || CheckWord("an")))
				return false;
			const Token& next = Peek(1);
			return next.IsWord() && next.Type != TokenType::Then && next.Type != TokenType::Do &&
				next.Type != TokenType::And && next.Type != TokenType::Or && next.Type != TokenType::End;
		}

		void IsOperator()
		{
			Advance(); // is
			bool negate = Match(TokenType::Not);
			OpCode op = OpCode::Equal;

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

		uint8_t ArgumentList()
		{
			PushStopWords(nullptr);
			int count = 0;
			if (!Check(TokenType::RightParen))
			{
				do
				{
					Expression();
					if (count == 255)
						Error("A function can take at most 255 values.");
					count++;
				} while (Match(TokenType::Comma));
			}
			PopStopWords();
			Consume(TokenType::RightParen, "Expected ')' after the values.");
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
				ErrorAtCurrent("Expected '}' to close the {value} inside this text.");
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
			std::string name = ConsumeName("Expected the name of a kind.");
			EmitGet(ResolveName(name));
			while (Match(TokenType::Dot))
				EmitWithName(OpCode::GetProperty, ConsumeMemberName());

			uint8_t count = 0;
			if (Match(TokenType::LeftParen))
				count = ArgumentList();
			EmitOp(OpCode::New);
			EmitByte(count);
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
					std::string name(token.Lexeme);
					Advance();
					EmitGet(ResolveName(name));
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
					ErrorAtCurrent("Expected a value here, but the line ended.");
				default:
					break;
			}

			if (token.IsWord())
				ErrorAtCurrent("Expected a value here, but found the keyword '" + std::string(token.Lexeme) + "'.");
			ErrorAtCurrent("Expected a value here, but found '" + std::string(token.Lexeme) + "'.");
		}

	private:
		VM& m_VM;
		Module* m_Module;
		std::vector<Token> m_Tokens;
		size_t m_Pos = 0;
		std::string m_FileName;
		CompileOptions m_Options;
		PhraseTable m_Phrases;
		std::unordered_set<std::string> m_ModuleNames;
		FunctionState* m_State = nullptr;
		std::vector<const std::set<std::string>*> m_StopWords;
		bool m_NoInvoke = false;
		size_t m_LastCallPos = static_cast<size_t>(-1);
		CompileError m_Error;
	};

	Function* CompileScript(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options, CompileError& outError)
	{
		Lexer lexer(source);
		std::vector<Token> tokens = lexer.Tokenize();
		Compiler compiler(vm, module, std::move(tokens), fileName, options);
		return compiler.Compile(outError);
	}

}
