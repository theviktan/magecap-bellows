#include "Forge/Forge.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// The forge command line tool.
//
//   forge                      start the interactive prompt
//   forge game.forge           run a script
//   forge --check game.forge   check a script for mistakes without running it
//   forge --realtime game.forge   run with real time, so "wait 2 seconds" really waits

namespace {

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

	// "use "helpers"" looks for helpers.forge next to the script that is running.
	void InstallFileLoader(Forge::VM& vm, const std::filesystem::path& baseFolder)
	{
		vm.SetModuleLoader([baseFolder](const std::string& path, std::string& outName, std::string& outSource)
		{
			std::filesystem::path candidate = baseFolder / path;
			if (!candidate.has_extension())
				candidate += ".forge";
			if (!ReadFile(candidate, outSource))
				return false;
			outName = candidate.filename().string();
			return true;
		});
	}

	void PrintUsage()
	{
		std::cout <<
			"Forge Language " FORGE_VERSION "\n"
			"\n"
			"Usage:\n"
			"  forge                         start the interactive prompt\n"
			"  forge <script.forge>          run a script\n"
			"  forge --check <script.forge>  check a script for mistakes\n"
			"  forge --realtime <script>     run using real time for 'wait'\n"
			"  forge --version               show the version\n";
	}

	int RunFile(const std::string& fileName, bool realtime)
	{
		std::string source;
		if (!ReadFile(fileName, source))
		{
			std::cerr << "Could not open '" << fileName << "'." << std::endl;
			return 66;
		}

		Forge::VM vm;
		std::filesystem::path path(fileName);
		InstallFileLoader(vm, path.has_parent_path() ? path.parent_path() : std::filesystem::path("."));

		bool hadError = false;
		vm.SetErrorHandler([&hadError](const Forge::ScriptError& error)
		{
			hadError = true;
			std::cerr << error.ToString() << std::endl;
		});

		Forge::LoadResult result = vm.Load(source, path.filename().string());
		if (!result.Success)
			return 70;

		// Keep the "game loop" going while scripts are waiting.
		constexpr double step = 1.0 / 60.0;
		constexpr double maxSeconds = 60.0 * 60.0;
		auto last = std::chrono::steady_clock::now();
		while (vm.GetTaskCount() > 0 && vm.GetTime() < maxSeconds)
		{
			if (realtime)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				auto now = std::chrono::steady_clock::now();
				double elapsed = std::chrono::duration<double>(now - last).count();
				last = now;
				vm.Update(elapsed);
			}
			else
			{
				vm.Update(step);
			}
		}
		return hadError ? 70 : 0;
	}

	int CheckFile(const std::string& fileName)
	{
		std::string source;
		if (!ReadFile(fileName, source))
		{
			std::cerr << "Could not open '" << fileName << "'." << std::endl;
			return 66;
		}
		Forge::VM vm;
		std::filesystem::path path(fileName);
		InstallFileLoader(vm, path.has_parent_path() ? path.parent_path() : std::filesystem::path("."));
		Forge::CompileError error;
		if (!vm.Check(source, path.filename().string(), error))
		{
			std::cerr << error.ToString() << std::endl;
			return 65;
		}
		std::cout << fileName << ": no mistakes found." << std::endl;
		return 0;
	}

	bool NeedsMoreInput(const std::string& message)
	{
		return message.find("is missing its 'end'") != std::string::npos ||
			message.find("missing its closing") != std::string::npos ||
			message.find("but the line ended") != std::string::npos;
	}

	int RunPrompt()
	{
		Forge::VM vm;
		InstallFileLoader(vm, std::filesystem::current_path());
		vm.SetErrorHandler([](const Forge::ScriptError& error) { std::cerr << error.ToString() << std::endl; });
		Forge::Module* module = vm.CreateModule("prompt");

		std::cout << "Forge Language " FORGE_VERSION " - type code and press Enter. Type 'quit' to leave." << std::endl;
		Forge::CompileOptions options;
		options.Repl = true;

		std::string buffer;
		std::string line;
		while (true)
		{
			std::cout << (buffer.empty() ? "forge> " : "  ...> ") << std::flush;
			if (!std::getline(std::cin, line))
				break;
			if (buffer.empty() && (line == "quit" || line == "exit"))
				break;

			buffer += line + "\n";
			Forge::CompileError error;
			if (!vm.Check(buffer, "prompt", error) && NeedsMoreInput(error.Message))
				continue;

			vm.RunInModule(module, buffer, "prompt", options);
			buffer.clear();
			// Let short waits finish so the prompt feels responsive.
			for (int i = 0; i < 600 && vm.GetTaskCount() > 0; i++)
				vm.Update(1.0 / 60.0);
		}
		std::cout << std::endl;
		return 0;
	}

}

int main(int argc, char** argv)
{
	if (argc == 1)
		return RunPrompt();

	bool realtime = false;
	bool check = false;
	std::string file;
	for (int i = 1; i < argc; i++)
	{
		if (std::strcmp(argv[i], "--version") == 0)
		{
			std::cout << "Forge Language " FORGE_VERSION << std::endl;
			return 0;
		}
		if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
		{
			PrintUsage();
			return 0;
		}
		if (std::strcmp(argv[i], "--realtime") == 0)
			realtime = true;
		else if (std::strcmp(argv[i], "--check") == 0)
			check = true;
		else
			file = argv[i];
	}

	if (file.empty())
	{
		PrintUsage();
		return 64;
	}
	return check ? CheckFile(file) : RunFile(file, realtime);
}
