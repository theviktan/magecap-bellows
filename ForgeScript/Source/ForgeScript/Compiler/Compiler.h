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
		// Checkers that want warnings (VM::FindWarnings) point this at a list. A warning never stops a
		// script or changes what it does, and compiling to run leaves it null.
		std::vector<CompileError>* Warnings = nullptr;
		// More built-in functions for the warnings to know, beside the VM's own (the engine's, in the
		// language server).
		const std::vector<std::string>* BuiltinFunctions = nullptr;
		// In the interactive prompt, a line that is just a value prints that value.
		bool Repl = false;
		// Treat a 'use' whose script the module loader cannot find as a mistake now, instead of
		// when the script runs. Checkers (VM::Check, VM::FindMistakes) turn this on.
		bool ReportMissingScripts = false;
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
