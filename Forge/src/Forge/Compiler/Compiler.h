#pragma once

#include <string>
#include <string_view>

namespace Forge {

	class VM;
	struct Function;
	struct Module;

	struct CompileError
	{
		std::string Message;
		std::string File;
		int Line = 0;
		int Column = 0;

		std::string ToString() const;
	};

	struct CompileOptions
	{
		// In the interactive prompt, a line that is just a value prints that value.
		bool Repl = false;
	};

	// Compiles Forge source code into a top-level function that belongs to the given module.
	// Returns nullptr and fills outError when the code has a mistake.
	Function* CompileScript(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options, CompileError& outError);

}
