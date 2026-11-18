#pragma once

#include "Bellows/Core/Log.h"
#include "Bellows/Core/Process.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	// Play in a game window: the editor runs the game as BellowsRuntime in its own process and window,
	// the way it runs once exported, without the editor's panels. A crash in the game never takes the
	// editor down. Each frame Update() moves the game's log lines into the editor's Console.
	// UI-free, so BellowsTests drives it directly.
	class GameWindowSession
	{
	public:
		struct Options
		{
			std::filesystem::path RuntimeProgram;	// BellowsRuntime next to the editor
			std::filesystem::path ProjectFile;		// The .bproject
			std::filesystem::path SceneFile;		// The scene to start from; empty plays the project's start scene
			std::string Title;						// The game window's title
			bool Headless = false;					// No window, a fixed time step (automation and tests)
			uint64_t Frames = 0;					// Stop after this many frames (0 = run until closed)
		};

		// A line the game wrote to its console, split into its level and text.
		struct GameLogLine
		{
			LogLevel Level = LogLevel::Info;
			std::string Text;
		};

		~GameWindowSession();

		// Starts the game; a game that is already running is stopped first. False with outError set
		// when the program could not be started.
		bool Start(const Options& options, std::string* outError = nullptr);
		// Ends the game (the Stop button, F8 or closing the project).
		void Stop();
		bool IsRunning();
		// Forwards new log lines to the editor's log and reports, once, how the game ended.
		void Update();
		// The exit code of the last game once it has ended.
		std::optional<int> GetLastExitCode() const { return m_LastExitCode; }

		// The runtime's arguments for these options.
		static std::vector<std::string> BuildArguments(const Options& options);
		// Reads a line in the runtime's --editor log format, "[12:00:00] [warning] APP: text". Other
		// lines (from a library printing directly, say) come back as Info with the whole line as text.
		static GameLogLine ParseLogLine(std::string_view line);
	private:
		void ForwardLines(const std::vector<std::string>& lines);
		void ReportExit();
	private:
		Scope<Process> m_Process;
		bool m_StoppedByEditor = false;
		std::optional<int> m_LastExitCode;
	};

}
