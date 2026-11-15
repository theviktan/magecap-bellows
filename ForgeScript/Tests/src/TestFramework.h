#pragma once

#include "ForgeScript/ForgeScript.h"

#include <functional>
#include <sstream>
#include <string>
#include <vector>

// A deliberately small test framework: register with FORGESCRIPT_TEST, check with CHECK / CHECK_EQ.

namespace ForgeTest {

	struct TestCase
	{
		const char* Name;
		void (*Fn)();
	};

	std::vector<TestCase>& Registry();
	void ReportFailure(const char* file, int line, const std::string& message);

	struct Registrar
	{
		Registrar(const char* name, void (*fn)()) { Registry().push_back({ name, fn }); }
	};

	template<typename T>
	std::string Show(const T& value)
	{
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}

	inline std::string Show(const std::string& value) { return "\"" + value + "\""; }
	inline std::string Show(const char* value) { return "\"" + std::string(value) + "\""; }

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

#define FORGESCRIPT_TEST(name)                                                         \
	static void name();                                                          \
	static ForgeTest::Registrar name##Registrar(#name, name);                    \
	static void name()

#define CHECK(condition)                                                         \
	do                                                                           \
	{                                                                            \
		if (!(condition))                                                        \
			ForgeTest::ReportFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
	} while (0)

#define CHECK_EQ(actual, expected)                                               \
	do                                                                           \
	{                                                                            \
		auto&& forgeActual = (actual);                                           \
		auto&& forgeExpected = (expected);                                       \
		if (!(forgeActual == forgeExpected))                                     \
			ForgeTest::ReportFailure(__FILE__, __LINE__, "CHECK_EQ(" #actual ", " #expected ")\n      actual:   " + \
				ForgeTest::Show(forgeActual) + "\n      expected: " + ForgeTest::Show(forgeExpected)); \
	} while (0)

#define CHECK_CONTAINS(text, part)                                               \
	do                                                                           \
	{                                                                            \
		std::string forgeText = (text);                                          \
		std::string forgePart = (part);                                          \
		if (forgeText.find(forgePart) == std::string::npos)                      \
			ForgeTest::ReportFailure(__FILE__, __LINE__, "CHECK_CONTAINS(" #text ", " #part ")\n      text: " + \
				ForgeTest::Show(forgeText) + "\n      missing: " + ForgeTest::Show(forgePart)); \
	} while (0)
