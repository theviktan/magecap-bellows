#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript {

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

	// Compiles ForgeScript source code into a top-level function that belongs to the given module.
	// Returns nullptr and fills outError when the code has a mistake.
	Function* CompileScript(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options, CompileError& outError);

	// Finds every mistake it can, in order of line and column, without producing code: after a mistake
	// it skips to the end of that top-level statement or definition and carries on. Empty means none.
	// The first mistake CompileScript would report is always included.
	std::vector<CompileError> FindCompileErrors(VM& vm, Module* module, std::string_view source, const std::string& fileName,
		const CompileOptions& options);

}
