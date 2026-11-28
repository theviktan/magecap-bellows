#include "bwpch.h"
#include "Bellows/Core/Process.h"

#if defined(BW_PLATFORM_WINDOWS)
	#include <Windows.h>
#else
	#include <csignal>
	#include <fcntl.h>
	#include <spawn.h>
	#include <sys/wait.h>
	#include <unistd.h>

extern char** environ;
#endif

namespace Bellows {

	namespace {

#if defined(BW_PLATFORM_WINDOWS)
		std::wstring Widen(const std::string& text)
		{
			if (text.empty())
				return {};
			const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring result(static_cast<size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
			return result;
		}

		// Quotes one argument the way CommandLineToArgvW and the C runtime split command lines:
		// backslashes are literal except before a quote, where they must be doubled.
		void AppendQuotedArgument(std::wstring& commandLine, const std::wstring& argument)
		{
			if (!commandLine.empty())
				commandLine += L' ';
			if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
			{
				commandLine += argument;
				return;
			}
			commandLine += L'"';
			size_t backslashes = 0;
			for (const wchar_t character : argument)
			{
				if (character == L'\\')
				{
					++backslashes;
					continue;
				}
				if (character == L'"')
					commandLine.append(backslashes * 2 + 1, L'\\');
				else
					commandLine.append(backslashes, L'\\');
				backslashes = 0;
				commandLine += character;
			}
			commandLine.append(backslashes * 2, L'\\');
			commandLine += L'"';
		}

		std::string LastErrorText()
		{
			return "Windows error " + std::to_string(::GetLastError());
		}
#endif

		void SetError(std::string* outError, std::string message)
		{
			if (outError)
				*outError = std::move(message);
		}

	}

	bool Process::StartDetached(const StartInfo& info)
	{
#if defined(BW_PLATFORM_WINDOWS)
		std::wstring commandLine;
		AppendQuotedArgument(commandLine, info.Program.wstring());
		for (const std::string& argument : info.Arguments)
			AppendQuotedArgument(commandLine, Widen(argument));
		const std::wstring workingDirectory = info.WorkingDirectory.wstring();
		STARTUPINFOW startup {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process {};
		// Outside any job the editor runs in, so closing the editor doesn't end it.
		const DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
		auto create = [&](DWORD creationFlags)
		{
			return ::CreateProcessW(info.Program.wstring().c_str(), commandLine.data(), nullptr, nullptr, FALSE, creationFlags, nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process) != 0;
		};
		// Some launchers' jobs don't allow leaving them: then it starts inside the job.
		if (!create(flags) && !create(flags & ~static_cast<DWORD>(CREATE_BREAKAWAY_FROM_JOB)))
			return false;
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		return true;
#else
		// A grandchild's failed exec can't be reported back, so check what can be checked first.
		if (access(info.Program.c_str(), X_OK) != 0)
			return false;
		std::vector<std::string> argv { info.Program.string() };
		argv.insert(argv.end(), info.Arguments.begin(), info.Arguments.end());
		std::vector<char*> pointers;
		for (std::string& argument : argv)
			pointers.push_back(argument.data());
		pointers.push_back(nullptr);
		const std::string workingDirectory = info.WorkingDirectory.string();
		// A double fork: the grandchild belongs to init, in its own session, and no one waits for it.
		const pid_t child = fork();
		if (child < 0)
			return false;
		if (child == 0)
		{
			setsid();
			if (fork() != 0)
				_exit(0);
			const int devNull = open("/dev/null", O_RDWR);
			if (devNull >= 0)
			{
				dup2(devNull, STDIN_FILENO);
				dup2(devNull, STDOUT_FILENO);
				dup2(devNull, STDERR_FILENO);
			}
			if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
				_exit(127);
			execv(pointers[0], pointers.data());
			_exit(127);
		}
		int status = 0;
		waitpid(child, &status, 0);
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
	}

	Scope<Process> Process::Start(const StartInfo& info, std::string* outError)
	{
		if (!std::filesystem::is_regular_file(info.Program))
		{
			SetError(outError, "'" + info.Program.string() + "' does not exist");
			return nullptr;
		}

		Scope<Process> process(new Process());
#if defined(BW_PLATFORM_WINDOWS)
		SECURITY_ATTRIBUTES security{};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;
		HANDLE outputRead = nullptr;
		HANDLE outputWrite = nullptr;
		if (!::CreatePipe(&outputRead, &outputWrite, &security, 0))
		{
			SetError(outError, "Could not create an output pipe (" + LastErrorText() + ")");
			return nullptr;
		}
		// Only the child's end is inherited.
		::SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

		std::wstring commandLine;
		AppendQuotedArgument(commandLine, info.Program.wstring());
		for (const std::string& argument : info.Arguments)
			AppendQuotedArgument(commandLine, Widen(argument));

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = nullptr;
		startup.hStdOutput = outputWrite;
		startup.hStdError = outputWrite;
		PROCESS_INFORMATION processInfo{};
		const std::wstring workingDirectory = info.WorkingDirectory.wstring();
		// Suspended until it is in the job; no console window for console-subsystem builds.
		const BOOL started = ::CreateProcessW(info.Program.wstring().c_str(), commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &processInfo);
		::CloseHandle(outputWrite);
		if (!started)
		{
			SetError(outError, "Could not start '" + info.Program.string() + "' (" + LastErrorText() + ")");
			::CloseHandle(outputRead);
			return nullptr;
		}

		// The job ends the program if the editor itself goes away without cleaning up (a crash).
		process->m_JobHandle = ::CreateJobObjectW(nullptr, nullptr);
		if (process->m_JobHandle)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			::SetInformationJobObject(process->m_JobHandle, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
			::AssignProcessToJobObject(process->m_JobHandle, processInfo.hProcess);
		}
		::ResumeThread(processInfo.hThread);
		::CloseHandle(processInfo.hThread);
		process->m_ProcessHandle = processInfo.hProcess;
		process->m_OutputRead = outputRead;
#else
		int pipeEnds[2] = { -1, -1 };
		if (::pipe2(pipeEnds, O_CLOEXEC) != 0)
		{
			SetError(outError, std::string("Could not create an output pipe (") + std::strerror(errno) + ")");
			return nullptr;
		}

		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
		posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDERR_FILENO);
		if (!info.WorkingDirectory.empty())
			posix_spawn_file_actions_addchdir_np(&actions, info.WorkingDirectory.c_str());

		const std::string program = info.Program.string();
		std::vector<char*> argv;
		argv.push_back(const_cast<char*>(program.c_str()));
		for (const std::string& argument : info.Arguments)
			argv.push_back(const_cast<char*>(argument.c_str()));
		argv.push_back(nullptr);

		pid_t processID = -1;
		const int result = ::posix_spawn(&processID, program.c_str(), &actions, nullptr, argv.data(), environ);
		posix_spawn_file_actions_destroy(&actions);
		::close(pipeEnds[1]);
		if (result != 0)
		{
			SetError(outError, "Could not start '" + program + "' (" + std::strerror(result) + ")");
			::close(pipeEnds[0]);
			return nullptr;
		}
		process->m_ProcessID = processID;
		process->m_OutputRead = pipeEnds[0];
#endif

		process->m_Reader = std::thread([raw = process.get()]() { raw->ReadOutput(); });
		return process;
	}

	Process::~Process()
	{
		Terminate();
		Wait(std::chrono::milliseconds(5000));
		// The pipe reports its end once the program has ended, which ends the reader.
		if (m_Reader.joinable())
			m_Reader.join();
#if defined(BW_PLATFORM_WINDOWS)
		if (m_OutputRead)
			::CloseHandle(m_OutputRead);
		if (m_ProcessHandle)
			::CloseHandle(m_ProcessHandle);
		if (m_JobHandle)
			::CloseHandle(m_JobHandle);
#else
		if (m_OutputRead >= 0)
			::close(m_OutputRead);
#endif
	}

	void Process::ReadOutput()
	{
		char buffer[4096];
		while (true)
		{
#if defined(BW_PLATFORM_WINDOWS)
			DWORD count = 0;
			if (!::ReadFile(m_OutputRead, buffer, sizeof(buffer), &count, nullptr) || count == 0)
				break;
#else
			const ssize_t count = ::read(m_OutputRead, buffer, sizeof(buffer));
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				break;
#endif
			std::lock_guard lock(m_OutputMutex);
			m_PartialLine.append(buffer, static_cast<size_t>(count));
			size_t start = 0;
			for (size_t end = m_PartialLine.find('\n'); end != std::string::npos; end = m_PartialLine.find('\n', start))
			{
				size_t length = end - start;
				if (length > 0 && m_PartialLine[end - 1] == '\r')
					--length;
				m_Lines.emplace_back(m_PartialLine.substr(start, length));
				start = end + 1;
			}
			m_PartialLine.erase(0, start);
		}

		std::lock_guard lock(m_OutputMutex);
		if (!m_PartialLine.empty())
		{
			m_Lines.push_back(std::move(m_PartialLine));
			m_PartialLine.clear();
		}
	}

	void Process::PollExit()
	{
		if (m_ExitCode)
			return;
#if defined(BW_PLATFORM_WINDOWS)
		if (::WaitForSingleObject(m_ProcessHandle, 0) == WAIT_OBJECT_0)
		{
			DWORD code = 0;
			::GetExitCodeProcess(m_ProcessHandle, &code);
			m_ExitCode = static_cast<int>(code);
		}
#else
		int status = 0;
		const pid_t result = ::waitpid(m_ProcessID, &status, WNOHANG);
		if (result == m_ProcessID)
		{
			if (WIFEXITED(status))
				m_ExitCode = WEXITSTATUS(status);
			else if (WIFSIGNALED(status))
				m_ExitCode = 128 + WTERMSIG(status);
			else
				m_ExitCode = -1;
		}
#endif
	}

	bool Process::IsRunning()
	{
		PollExit();
		return !m_ExitCode.has_value();
	}

	std::optional<int> Process::GetExitCode()
	{
		PollExit();
		return m_ExitCode;
	}

	void Process::Terminate()
	{
		if (!IsRunning())
			return;
#if defined(BW_PLATFORM_WINDOWS)
		::TerminateProcess(m_ProcessHandle, 1);
#else
		::kill(m_ProcessID, SIGKILL);
#endif
	}

	bool Process::Wait(std::chrono::milliseconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (IsRunning())
		{
			if (std::chrono::steady_clock::now() >= deadline)
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return true;
	}

	std::vector<std::string> Process::ReadRemainingLines()
	{
		if (IsRunning())
			return {};
		// The pipe reports its end once the program has ended, which ends the reader.
		if (m_Reader.joinable())
			m_Reader.join();
		return ReadLines();
	}

	std::vector<std::string> Process::ReadLines()
	{
		std::lock_guard lock(m_OutputMutex);
		std::vector<std::string> lines;
		lines.swap(m_Lines);
		return lines;
	}

}
