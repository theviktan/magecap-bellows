#pragma once

#include "ForgeScript/Compiler/Lexer.h"

#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript {

	// True when tokens[index] starts a block that an 'end' closes (if, while, function, kind ...),
	// following the compiler's rule. For tools that read scripts from their tokens.
	bool TokenOpensBlock(const std::vector<Token>& tokens, size_t index);
	// Whether tokens[index] is 'property name' alone on its line directly inside a kind, which opens a block.
	bool PropertyOpensBlock(const std::vector<Token>& tokens, size_t index);
	// 'contract Damageable ... end' (Roadmap 3.0 item 4), for tools that pair blocks without compiling.
	bool ContractOpensBlock(const std::vector<Token>& tokens, size_t index);

	// A script variable set to a plain value at the top of a script, like
	//     set speed to 2.5
	// Hosts show these as editable settings (the Bellows Inspector does) without running the script.
	struct ScriptVariable
	{
		enum class ValueKind
		{
			Number,       // 2.5
			WholeNumber,  // 3     (written without a decimal point)
			Text,         // "Hero"
			Boolean,      // true, false, yes, no
			Vector2,      // vector(1, 2)
			Vector3,      // vector(1, 2, 3)
			Color         // [1, 0.5, 0] or [1, 0.5, 0, 1]
		};

		std::string Name;
		ValueKind Kind = ValueKind::Number;
		double Number = 0.0;
		bool Boolean = false;
		std::string Text;
		double Components[4] = { 0.0, 0.0, 0.0, 1.0 };
		int Line = 0;
		std::string Type; // the line's 'as' type ('set speed as number to 5'), or empty
	};

	// Finds every "set name to <plain value>" line at the top level of a script (not inside functions,
	// loops or other blocks), in order. Names starting with '_' are private and left out; when a name
	// is set more than once, the first line counts. Does not check the rest of the script for mistakes.
	std::vector<ScriptVariable> FindScriptVariables(std::string_view source);

	// A 'when' block that waits, directly or through the script's own functions it calls or starts.
	struct WaitingHandler
	{
		std::string Event;	// The event as written after 'when', without its values: "clicked", "\"Resume\" is clicked"
		int Line = 0;		// The line of its 'when'
	};

	// Finds the handlers for the given events (the event's own name, like "clicked", also matches
	// 'when "Resume" is clicked') that contain a 'wait', or call or start a function of this script
	// that does, in order. A host uses it to warn about handlers that would hold while the game is
	// paused. Reads the tokens only; does not check the script for mistakes.
	std::vector<WaitingHandler> FindHandlersThatWait(std::string_view source, const std::vector<std::string>& events);

}
