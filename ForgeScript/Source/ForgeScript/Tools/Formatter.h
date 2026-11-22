#pragma once

#include <string>
#include <string_view>

namespace ForgeScript {

	// What FormatScript did. When Error is set, Text is the script unchanged.
	struct FormatResult
	{
		std::string Text;
		bool Changed = false;
		std::string Error; // why the script was left alone, or ""
		int ErrorLine = 0;
	};

	// Lays out a script the standard way, changing only whitespace at the start and end of lines:
	// a tab for each open block (and each open bracket), 'else', 'otherwise', 'catch', 'finally' and a
	// match's 'is' lines one level out, no spaces at line ends, at most two blank lines in a row, and
	// one newline at the end. Text inside quotes, long text and #[ ]# comments is never touched, nor is
	// spacing inside a line. The result is checked to read as exactly the same tokens; a script whose
	// blocks don't add up, or that the lexer can't read, is left alone with a reason.
	FormatResult FormatScript(std::string_view source);

}
