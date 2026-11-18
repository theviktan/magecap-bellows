#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Platform.h"
#include "Bellows/Core/Process.h"

#include "GameWindowSession.h"

#include <algorithm>
#include <fstream>
#include <string>

using namespace Bellows;

namespace {

	// forgescript is built next to the tests on every platform, so it serves as the child program.
	std::filesystem::path ForgeScriptProgram()
	{
		return Platform::GetExecutableDirectory() / ("forgescript" + Platform::GetExecutableExtension());
	}

	// A script in a folder whose name has spaces and an apostrophe, so arguments must be passed intact.
	std::filesystem::path WriteScript(const std::string& name, const std::string& source)
	{
		const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Process Tests (Ron's)";
		std::filesystem::create_directories(folder);
		const std::filesystem::path path = folder / name;
		std::ofstream(path, std::ios::binary) << source;
		return path;
	}

	bool Contains(const std::vector<std::string>& lines, const std::string& text)
	{
		return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
	}

	std::vector<std::string> ReadAll(Process& process)
	{
		std::vector<std::string> lines;
		REQUIRE(process.Wait(std::chrono::seconds(30)));
		// The reader may still be draining the pipe right after the program ends.
		for (int attempt = 0; attempt < 200; attempt++)
		{
			std::vector<std::string> more = process.ReadLines();
			lines.insert(lines.end(), more.begin(), more.end());
			if (!more.empty() && Contains(lines, "end of output"))
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return lines;
	}

}

TEST_CASE("Process runs a program, passes its arguments and returns its output and exit code")
{
	const std::filesystem::path script = WriteScript("hello.forge", "say \"hello from a child\"\nsay \"caf\xC3\xA9\"\nsay \"end of output\"\n");
	std::string error;
	Scope<Process> process = Process::Start({ ForgeScriptProgram(), { script.string() } }, &error);
	REQUIRE_MESSAGE(process, error);

	const std::vector<std::string> lines = ReadAll(*process);
	CHECK(Contains(lines, "hello from a child"));
	CHECK(Contains(lines, "caf\xC3\xA9"));
	CHECK_FALSE(Contains(lines, "\r"));
	CHECK(process->GetExitCode() == 0);
	CHECK_FALSE(process->IsRunning());
}

TEST_CASE("Process reads standard error and reports a failing exit code")
{
	const std::filesystem::path script = WriteScript("broken.forge", "say (1 +\n");
	Scope<Process> process = Process::Start({ ForgeScriptProgram(), { "--check", script.string() } });
	REQUIRE(process);
	REQUIRE(process->Wait(std::chrono::seconds(30)));
	CHECK(process->GetExitCode() == 65);	// forgescript --check: the script has mistakes

	std::vector<std::string> lines;
	for (int attempt = 0; attempt < 200 && lines.empty(); attempt++)
	{
		lines = process->ReadLines();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CHECK(Contains(lines, "broken.forge:"));
}

TEST_CASE("Process can end a program that would run forever")
{
	const std::filesystem::path script = WriteScript("forever.forge", "repeat forever\nend\n");
	Scope<Process> process = Process::Start({ ForgeScriptProgram(), { script.string() } });
	REQUIRE(process);
	CHECK(process->IsRunning());
	CHECK_FALSE(process->Wait(std::chrono::milliseconds(200)));

	process->Terminate();
	CHECK(process->Wait(std::chrono::seconds(10)));
	CHECK_FALSE(process->IsRunning());
	CHECK(process->GetExitCode() != 0);
	// Ending it twice is harmless.
	process->Terminate();

	// Destroying a running process ends it instead of leaving it behind.
	Scope<Process> second = Process::Start({ ForgeScriptProgram(), { script.string() } });
	REQUIRE(second);
	CHECK(second->IsRunning());
	second.reset();
}

TEST_CASE("Process reports a program that does not exist")
{
	std::string error;
	CHECK_FALSE(Process::Start({ BellowsTests::GetTestOutputDirectory() / "no-such-program" }, &error));
	CHECK(error.find("does not exist") != std::string::npos);
}

TEST_CASE("The game window session passes the editor's choices to the runtime")
{
	GameWindowSession::Options options;
	options.ProjectFile = "C:/Games/My Game/My Game.bproject";
	options.SceneFile = "C:/Games/My Game/.bellows/PlayScene.bscene";
	options.Title = "My Game (DEBUG)";
	const std::vector<std::string> arguments = GameWindowSession::BuildArguments(options);
	const std::vector<std::string> expected = { "--project", "C:/Games/My Game/My Game.bproject", "--editor", "--windowed",
		"--scene-file", "C:/Games/My Game/.bellows/PlayScene.bscene", "--title", "My Game (DEBUG)" };
	CHECK(arguments == expected);

	options.SceneFile.clear();
	options.Title.clear();
	options.Headless = true;
	options.Frames = 30;
	const std::vector<std::string> automated = { "--project", "C:/Games/My Game/My Game.bproject", "--editor", "--windowed",
		"--headless", "--frames", "30" };
	CHECK(GameWindowSession::BuildArguments(options) == automated);
}

TEST_CASE("The game window session reads the runtime's log lines")
{
	const GameWindowSession::GameLogLine warning = GameWindowSession::ParseLogLine("[12:34:56] [warning] APP: low on health");
	CHECK(warning.Level == LogLevel::Warn);
	CHECK(warning.Text == "APP: low on health");
	CHECK(GameWindowSession::ParseLogLine("[12:34:56] [error] BELLOWS: no scene").Level == LogLevel::Error);
	CHECK(GameWindowSession::ParseLogLine("[12:34:56] [critical] BELLOWS: x").Level == LogLevel::Critical);
	CHECK(GameWindowSession::ParseLogLine("[12:34:56] [info] APP: [1, 2]").Text == "APP: [1, 2]");

	// Anything else a program prints arrives whole, as information.
	const GameWindowSession::GameLogLine plain = GameWindowSession::ParseLogLine("Vulkan loader: [maybe] a note");
	CHECK(plain.Level == LogLevel::Info);
	CHECK(plain.Text == "Vulkan loader: [maybe] a note");
	CHECK(GameWindowSession::ParseLogLine("[12:34:56] [shouting] APP: x").Text == "[12:34:56] [shouting] APP: x");
	CHECK(GameWindowSession::ParseLogLine("").Text.empty());
}

TEST_CASE("The game window session forwards the game's output and reports how it ended")
{
	// forgescript stands in for the runtime: it rejects the runtime's options with exit code 64.
	GameWindowSession session;
	GameWindowSession::Options options;
	options.RuntimeProgram = ForgeScriptProgram();
	options.ProjectFile = BellowsTests::GetTestOutputDirectory() / "Game.bproject";
	const uint64_t before = Log::GetLatestSequence();
	REQUIRE(session.Start(options));
	for (int frame = 0; frame < 3000 && session.IsRunning(); frame++)
	{
		session.Update();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	session.Update();
	CHECK_FALSE(session.IsRunning());
	CHECK(session.GetLastExitCode() == 64);

	bool sawGameLine = false;
	bool sawExit = false;
	for (const LogMessage& message : Log::GetMessagesSince(before))
	{
		sawGameLine = sawGameLine || message.Text.find("[Game] ") != std::string::npos;
		sawExit = sawExit || (message.Level == LogLevel::Error && message.Text.find("exit code 64") != std::string::npos);
	}
	CHECK(sawGameLine);
	CHECK(sawExit);

	// A program that is not there is an error, not a crash.
	options.RuntimeProgram = BellowsTests::GetTestOutputDirectory() / "no-such-runtime";
	std::string error;
	CHECK_FALSE(session.Start(options, &error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(session.IsRunning());
}
