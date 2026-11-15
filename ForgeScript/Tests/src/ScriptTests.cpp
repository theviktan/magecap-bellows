#include "TestFramework.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// Runs every .forge file in Tests/scripts. Each script lists what it should print:
//
//     say "hello"        # expect: hello
//     fail "boom"        # expect error: boom
//
// Every script runs twice: once normally and once with the garbage collector running on
// every single instruction, which flushes out objects that are not kept alive correctly.

namespace {

	struct Expectations
	{
		std::vector<std::string> Lines;
		std::vector<std::string> Errors;
	};

	Expectations ReadExpectations(const std::string& source)
	{
		Expectations expectations;
		std::istringstream stream(source);
		std::string line;
		const std::string outputMarker = "# expect: ";
		const std::string errorMarker = "# expect error: ";
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			size_t at = line.find(errorMarker);
			if (at != std::string::npos)
			{
				expectations.Errors.push_back(line.substr(at + errorMarker.size()));
				continue;
			}
			at = line.find(outputMarker);
			if (at != std::string::npos)
				expectations.Lines.push_back(line.substr(at + outputMarker.size()));
			else if (line.size() >= 9 && line.compare(line.size() - 9, 9, "# expect:") == 0)
				expectations.Lines.emplace_back();
		}
		return expectations;
	}

	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
			lines.push_back(line);
		return lines;
	}

	bool ReadFile(const std::filesystem::path& path, std::string& outText)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		std::stringstream buffer;
		buffer << file.rdbuf();
		outText = buffer.str();
		return true;
	}

	void CheckScript(const std::filesystem::path& path, bool stressGC)
	{
		std::string source;
		if (!ReadFile(path, source))
		{
			ForgeTest::ReportFailure(__FILE__, __LINE__, "could not read " + path.string());
			return;
		}
		Expectations expected = ReadExpectations(source);

		ForgeTest::RunOptions options;
		options.StressGC = stressGC;
		std::filesystem::path folder = path.parent_path() / "modules";
		options.Setup = [folder](ForgeScript::VM& vm)
		{
			vm.SetRandomSeed(42);
			vm.SetModuleLoader([folder](const std::string& name, std::string& outName, std::string& outSource)
			{
				std::filesystem::path file = folder / (name + ".forge");
				outName = name + ".forge";
				return ReadFile(file, outSource);
			});
		};
		ForgeTest::ScriptRun run = ForgeTest::RunScript(source, options);

		std::string label = path.filename().string() + (stressGC ? " (GC stress)" : "");
		std::vector<std::string> actual = SplitLines(run.Output);
		if (actual != expected.Lines)
		{
			std::string message = label + ": output does not match.\n      expected:\n";
			for (const std::string& line : expected.Lines)
				message += "        | " + line + "\n";
			message += "      actual:\n";
			for (const std::string& line : actual)
				message += "        | " + line + "\n";
			if (!run.Errors.empty())
				message += "      errors:\n        " + run.AllErrors();
			ForgeTest::ReportFailure(__FILE__, __LINE__, message);
		}

		if (run.Errors.size() != expected.Errors.size())
		{
			ForgeTest::ReportFailure(__FILE__, __LINE__, label + ": expected " + std::to_string(expected.Errors.size()) +
				" error(s) but got " + std::to_string(run.Errors.size()) + ":\n        " + run.AllErrors());
			return;
		}
		for (size_t i = 0; i < expected.Errors.size(); i++)
		{
			if (run.Errors[i].find(expected.Errors[i]) == std::string::npos)
			{
				ForgeTest::ReportFailure(__FILE__, __LINE__, label + ": error " + std::to_string(i + 1) +
					" should contain \"" + expected.Errors[i] + "\" but was:\n        " + run.Errors[i]);
			}
		}
	}

	std::vector<std::filesystem::path> ScriptFiles()
	{
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(FORGESCRIPT_TEST_SCRIPTS_DIR))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".forge")
				files.push_back(entry.path());
		}
		std::sort(files.begin(), files.end());
		return files;
	}

}

FORGESCRIPT_TEST(ScriptExpectations)
{
	std::vector<std::filesystem::path> files = ScriptFiles();
	CHECK(!files.empty());
	for (const std::filesystem::path& file : files)
	{
		std::cout << "    script " << file.filename().string() << std::endl;
		CheckScript(file, false);
	}
}

FORGESCRIPT_TEST(ScriptExpectationsUnderGCStress)
{
	for (const std::filesystem::path& file : ScriptFiles())
		CheckScript(file, true);
}
