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

	// One place a script names one of its variables, as the compiler resolved it (VM::FindNameUses).
	// Two uses with the same Key mean the same variable, so an editor can find and rename them all.
	struct NameUse
	{
		int Line = 0;      // 1-based
		int Column = 0;    // 1-based bytes, as the lexer counts them
		int Length = 0;    // in bytes
		size_t Token = 0;  // its index in the script's tokens
		// "L<n>": a local variable, parameter or loop variable (n tells them apart, even with the same
		// name); "G:<name>": a script-level name, or a built-in or a used script's when not set here;
		// "F:<name>": a field or method, of whichever kind has one by that name.
		std::string Key;
		bool Declares = false; // where it is made: a parameter, a loop name, 'set' at script level ...
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
		// Editors (the language server's references and rename) point this at a list to learn which
		// variable each name means.
		std::vector<NameUse>* NameUses = nullptr;
	};

	// Why 'use "path"' may not load that path, or "" when it may. A script can only use other ForgeScript
	// scripts in its own place (the project's asset folder, or the folder it is in, and below): the path
	// must be relative, never step up with "..", and end in .forge or have no extension. The compiler
	// reports it as a mistake and VM::ResolveModuleSource refuses it, so no host's loader ever sees one.
	std::string CheckUsePath(const std::string& path);

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
