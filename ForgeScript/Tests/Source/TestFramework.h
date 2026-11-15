#pragma once

// ForgeScript tests use doctest, like the rest of MageCap Bellows. This header adds the few helpers
// the ForgeScript tests share: running a script in a fresh VM and checking text contents.

#include "ForgeScript/ForgeScript.h"

#include <doctest/doctest.h>

#include <functional>
#include <string>
#include <vector>

namespace ForgeTest {

	// Records a failed check at the given place without stopping the test case.
	inline void ReportFailure(const char* file, int line, const std::string& message)
	{
		ADD_FAIL_CHECK_AT(file, line, message);
	}

	// The result of running a script inside a fresh VM.
	struct ScriptRun
	{
		bool Success = false;
		std::string Output;              // Everything printed, one line per print.
		std::vector<std::string> Errors; // Every error reported to the host.

		std::string AllErrors() const
		{
			std::string text;
			for (const std::string& error : Errors)
				text += error + "\n";
			return text;
		}
	};

	struct RunOptions
	{
		double SimulateSeconds = 30.0;   // How long to keep calling Update after loading.
		bool StressGC = false;           // Collect garbage on every instruction.
		std::function<void(ForgeScript::VM&)> Setup;
	};

	ScriptRun RunScript(const std::string& source, const RunOptions& options = RunOptions());

}

#define CHECK_CONTAINS(text, part)                                               \
	do                                                                           \
	{                                                                            \
		const std::string forgeText = (text);                                    \
		const std::string forgePart = (part);                                    \
		CHECK_MESSAGE(forgeText.find(forgePart) != std::string::npos,            \
			"expected \"" << forgeText << "\" to contain \"" << forgePart << "\""); \
	} while (0)
