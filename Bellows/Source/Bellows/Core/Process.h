#pragma once

#include "Bellows/Core/Base.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Bellows {

	// A program started by the engine, such as the game the editor runs in its own window. Its
	// standard output and error are read on a background thread and handed out as whole lines, so
	// the caller polls once a frame and never blocks. Destroying a Process that is still running
	// ends it: a game window never outlives the editor that started it.
	class Process
	{
	public:
		struct StartInfo
		{
			std::filesystem::path Program;
			std::vector<std::string> Arguments;		// Passed as they are, without shell interpretation
			std::filesystem::path WorkingDirectory;	// Empty: the current directory
		};

		// Returns null and sets outError when the program could not be started.
		static Scope<Process> Start(const StartInfo& info, std::string* outError = nullptr);

		~Process();
		Process(const Process&) = delete;
		Process& operator=(const Process&) = delete;

		bool IsRunning();
		// The exit code once the program has ended (a program ended by a signal reports 128 + signal).
		std::optional<int> GetExitCode();
		// Ends the program at once; a no-op once it has ended.
		void Terminate();
		// Waits until the program ends or the timeout passes; true when it ended.
		bool Wait(std::chrono::milliseconds timeout);

		// Lines written to standard output or error since the last call, without line endings.
		std::vector<std::string> ReadLines();
		// Once the program has ended: waits until all of its output has been read and returns the
		// lines ReadLines has not handed out yet. Returns nothing while the program still runs.
		std::vector<std::string> ReadRemainingLines();
	private:
		Process() = default;
		void ReadOutput();
		void PollExit();
	private:
#if defined(BW_PLATFORM_WINDOWS)
		void* m_ProcessHandle = nullptr;
		void* m_JobHandle = nullptr;
		void* m_OutputRead = nullptr;
#else
		int m_ProcessID = -1;
		int m_OutputRead = -1;
#endif
		std::optional<int> m_ExitCode;
		std::thread m_Reader;
		std::mutex m_OutputMutex;
		std::string m_PartialLine;
		std::vector<std::string> m_Lines;
	};

}
