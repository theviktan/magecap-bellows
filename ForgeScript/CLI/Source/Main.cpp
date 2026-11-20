#include "ForgeScript/ForgeScript.h"
#include "ForgeScript/Tools/LanguageServer.h"
#include "ForgeScript/Tools/ProjectPaths.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
	// NOMINMAX and WIN32_LEAN_AND_MEAN come from BellowsCompileOptions.cmake.
	#include <fcntl.h>
	#include <io.h>
	#include <windows.h>
#endif

// The forgescript command line tool.
//
//   forgescript                      start the interactive prompt
//   forgescript game.forge           run a script
//   forgescript --check game.forge   check a script for mistakes without running it
//   forgescript --check --json game.forge                      the mistakes as JSON, for editors
//   forgescript --check --stdin --name Scripts/game.forge      check text from standard input
//   forgescript --realtime game.forge   run with real time, so "wait 2 seconds" really waits
//
// Exit codes (tools depend on them): 0 no mistakes / ran fine, 64 wrong usage, 65 the script has
// mistakes, 66 the file could not be opened, 70 the script failed while running.

namespace {

	bool ReadFile(const std::filesystem::path& path, std::string& outText)
	{
		return ForgeScript::ReadTextFile(path, outText);
	}

	// "use "helpers"" looks for helpers.forge in 'baseFolder' (see FindUseFolder).
	void InstallFileLoader(ForgeScript::VM& vm, const std::filesystem::path& baseFolder)
	{
		vm.SetModuleLoader([baseFolder](const std::string& path, std::string& outName, std::string& outSource)
		{
			const std::filesystem::path candidate = ForgeScript::ResolveUsePath(baseFolder, path);
			if (!ReadFile(candidate, outSource))
				return false;
			outName = candidate.filename().string();
			return true;
		});
	}

	// The folder this program is in, where the engine's forgescript-api.json is installed.
	std::filesystem::path ProgramFolder(const char* argv0)
	{
		std::error_code error;
#ifdef _WIN32
		wchar_t buffer[32768];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
		if (length > 0 && length < std::size(buffer))
			return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#else
		const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
		if (!error)
			return self.parent_path();
#endif
		return std::filesystem::absolute(argv0, error).parent_path();
	}

	int RunLanguageServer(const std::filesystem::path& apiFile)
	{
#ifdef _WIN32
		// LSP counts bytes, so Windows must not turn \n into \r\n on the way in or out.
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		ForgeScript::LanguageServer::Options options;
		options.ApiFile = apiFile;
		ForgeScript::LanguageServer server(options);
		return server.Run(std::cin, std::cout);
	}

	void PrintUsage()
	{
		std::cout <<
			"ForgeScript " FORGESCRIPT_VERSION "\n"
			"\n"
			"Usage:\n"
			"  forgescript                         start the interactive prompt\n"
			"  forgescript <script.forge>          run a script\n"
			"  forgescript --check <script.forge>  check a script for mistakes\n"
			"      --json                          print the mistakes as JSON\n"
			"      --stdin                         read the script from standard input\n"
			"      --name <path>                   the script's path, for messages and 'use' (with --stdin)\n"
			"  forgescript --lsp                   run as a language server for editors (LSP over stdin/stdout)\n"
			"      --api <file>                    the engine API description (default: forgescript-api.json\n"
			"                                      next to this program)\n"
			"  forgescript --realtime <script>     run using real time for 'wait'\n"
			"  forgescript --version               show the version\n";
	}

	int RunFile(const std::string& fileName, bool realtime)
	{
		std::string source;
		if (!ReadFile(fileName, source))
		{
			std::cerr << "Could not open '" << fileName << "'." << std::endl;
			return 66;
		}

		ForgeScript::VM vm;
		std::filesystem::path path(fileName);
		InstallFileLoader(vm, ForgeScript::FindUseFolder(fileName));

		bool hadError = false;
		vm.SetErrorHandler([&hadError](const ForgeScript::ScriptError& error)
		{
			hadError = true;
			std::cerr << error.ToString() << std::endl;
		});

		ForgeScript::LoadResult result = vm.Load(source, path.filename().string());
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

	std::string JsonString(const std::string& text)
	{
		std::string out = "\"";
		for (unsigned char c : text)
		{
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20)
					{
						char escaped[8];
						std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
						out += escaped;
					}
					else
					{
						out += static_cast<char>(c);
					}
			}
		}
		return out + "\"";
	}

	// The same shape as "BellowsEditor --check-script --json", so editors read both tools alike:
	// { "file": ..., "diagnostics": [ { "line", "column", "endColumn", "severity", "message" } ] }.
	// endColumn is one past the end of the mistake's line, since the checker reports where it starts.
	// Warnings come after the mistakes, with "severity":"warning"; they never change the exit code.
	std::string MistakesToJson(const std::string& fileName, const std::string& source, const std::vector<ForgeScript::CompileError>& mistakes,
		const std::vector<ForgeScript::CompileError>& warnings)
	{
		std::vector<size_t> lineLengths;
		size_t start = 0;
		while (true)
		{
			const size_t end = source.find('\n', start);
			const size_t lineEnd = end == std::string::npos ? source.size() : end;
			size_t length = lineEnd - start;
			if (length > 0 && source[lineEnd - 1] == '\r')
				length--;
			lineLengths.push_back(length);
			if (end == std::string::npos)
				break;
			start = end + 1;
		}

		std::string json = "{\"file\":" + JsonString(fileName) + ",\"diagnostics\":[";
		for (size_t i = 0; i < mistakes.size() + warnings.size(); i++)
		{
			const bool warning = i >= mistakes.size();
			const ForgeScript::CompileError& mistake = warning ? warnings[i - mistakes.size()] : mistakes[i];
			const size_t line = static_cast<size_t>(std::max(mistake.Line, 0));
			const size_t column = static_cast<size_t>(std::max(mistake.Column, 0));
			const size_t lineLength = line > 0 && line <= lineLengths.size() ? lineLengths[line - 1] : 0;
			if (i > 0)
				json += ",";
			json += "{\"line\":" + std::to_string(line) + ",\"column\":" + std::to_string(column) +
				",\"endColumn\":" + std::to_string(std::max(lineLength + 1, column + 1)) +
				",\"severity\":\"" + (warning ? "warning" : "error") + "\",\"message\":" + JsonString(mistake.Message) + "}";
		}
		return json + "]}";
	}

	struct CheckOptions
	{
		std::string FileName;   // as given; may be empty with --stdin
		bool Json = false;
		bool FromStdin = false;
	};

	int CheckScript(const CheckOptions& options)
	{
		std::string source;
		if (options.FromStdin)
		{
			source.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
		}
		else if (!ReadFile(options.FileName, source))
		{
			std::cerr << "Could not open '" << options.FileName << "'." << std::endl;
			return 66;
		}

		const std::string fileName = options.FileName.empty() ? std::string("stdin") : options.FileName;
		ForgeScript::VM vm;
		std::filesystem::path path(fileName);
		// "use" is resolved from the checked file's place, even when its text came from standard input.
		InstallFileLoader(vm, ForgeScript::FindUseFolder(fileName));
		const std::vector<ForgeScript::CompileError> mistakes = vm.FindMistakes(source, path.filename().string());
		// Warnings only for a script without mistakes, as in the language server: fix those first.
		const std::vector<ForgeScript::CompileError> warnings = mistakes.empty() ?
			vm.FindWarnings(source, path.filename().string()) : std::vector<ForgeScript::CompileError>();

		if (options.Json)
		{
			std::cout << MistakesToJson(fileName, source, mistakes, warnings) << std::endl;
		}
		else
		{
			for (const ForgeScript::CompileError& mistake : mistakes)
				std::cerr << mistake.ToString() << std::endl;
			for (const ForgeScript::CompileError& warning : warnings)
			{
				// The mistakes' layout with "warning: " after the place, as C compilers write it.
				ForgeScript::CompileError shown = warning;
				shown.Message = "warning: " + shown.Message;
				std::cerr << shown.ToString() << std::endl;
			}
			if (mistakes.empty())
				std::cout << fileName << ": no mistakes found." << std::endl;
		}
		return mistakes.empty() ? 0 : 65;
	}

	bool NeedsMoreInput(const std::string& message)
	{
		return message.find("is missing its 'end'") != std::string::npos ||
			message.find("missing its closing") != std::string::npos ||
			message.find("but the line ended") != std::string::npos;
	}

	int RunPrompt()
	{
		ForgeScript::VM vm;
		InstallFileLoader(vm, std::filesystem::current_path());
		vm.SetErrorHandler([](const ForgeScript::ScriptError& error) { std::cerr << error.ToString() << std::endl; });
		ForgeScript::Module* module = vm.CreateModule("prompt");

		std::cout << "ForgeScript " FORGESCRIPT_VERSION " - type code and press Enter. Type 'quit' to leave." << std::endl;
		ForgeScript::CompileOptions options;
		options.Repl = true;

		std::string buffer;
		std::string line;
		while (true)
		{
			std::cout << (buffer.empty() ? "forgescript> " : "  ...> ") << std::flush;
			if (!std::getline(std::cin, line))
				break;
			if (buffer.empty() && (line == "quit" || line == "exit"))
				break;

			buffer += line + "\n";
			ForgeScript::CompileError error;
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
	bool lsp = false;
	std::filesystem::path apiFile;
	bool apiGiven = false;
	CheckOptions checkOptions;
	std::string file;
	for (int i = 1; i < argc; i++)
	{
		if (std::strcmp(argv[i], "--version") == 0)
		{
			std::cout << "ForgeScript " FORGESCRIPT_VERSION << std::endl;
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
		else if (std::strcmp(argv[i], "--lsp") == 0)
			lsp = true;
		else if (std::strcmp(argv[i], "--api") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--api needs the path of forgescript-api.json after it." << std::endl;
				return 64;
			}
			apiFile = argv[++i];
			apiGiven = true;
		}
		else if (std::strcmp(argv[i], "--json") == 0)
			checkOptions.Json = true;
		else if (std::strcmp(argv[i], "--stdin") == 0)
			checkOptions.FromStdin = true;
		else if (std::strcmp(argv[i], "--name") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "--name needs the script's path after it, like: --name Scripts/player.forge" << std::endl;
				return 64;
			}
			file = argv[++i];
		}
		else if (std::strncmp(argv[i], "--", 2) == 0)
		{
			std::cerr << "Unknown option '" << argv[i] << "'." << std::endl;
			PrintUsage();
			return 64;
		}
		else
			file = argv[i];
	}

	if (lsp)
	{
		if (check || !file.empty())
		{
			std::cerr << "--lsp takes its scripts from the editor; use it without --check or a file name." << std::endl;
			return 64;
		}
		if (!apiGiven)
		{
			const std::filesystem::path installed = ProgramFolder(argv[0]) / "forgescript-api.json";
			std::error_code error;
			if (std::filesystem::exists(installed, error))
				apiFile = installed;
		}
		return RunLanguageServer(apiFile);
	}
	if (apiGiven)
	{
		std::cerr << "--api works together with --lsp." << std::endl;
		return 64;
	}
	if ((checkOptions.Json || checkOptions.FromStdin) && !check)
	{
		std::cerr << "--json and --stdin work together with --check." << std::endl;
		return 64;
	}
	if (check)
	{
		checkOptions.FileName = file;
		if (file.empty() && !checkOptions.FromStdin)
		{
			PrintUsage();
			return 64;
		}
		return CheckScript(checkOptions);
	}
	if (file.empty())
	{
		PrintUsage();
		return 64;
	}
	return RunFile(file, realtime);
}
