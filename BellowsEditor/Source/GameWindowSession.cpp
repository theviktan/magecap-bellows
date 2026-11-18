#include "GameWindowSession.h"

#include <array>
#include <utility>

namespace Bellows {

	GameWindowSession::~GameWindowSession()
	{
		Stop();
	}

	bool GameWindowSession::Start(const Options& options, std::string* outError)
	{
		Stop();
		Process::StartInfo info;
		info.Program = options.RuntimeProgram;
		info.Arguments = BuildArguments(options);
		info.WorkingDirectory = options.ProjectFile.parent_path();
		std::string error;
		m_Process = Process::Start(info, &error);
		if (!m_Process)
		{
			BW_ERROR("Could not start the game window: {}", error);
			if (outError)
				*outError = error;
			return false;
		}
		m_StoppedByEditor = false;
		m_LastExitCode.reset();
		BW_INFO("Game window started");
		return true;
	}

	void GameWindowSession::Stop()
	{
		if (!m_Process)
			return;
		if (m_Process->IsRunning())
		{
			m_StoppedByEditor = true;
			m_Process->Terminate();
			m_Process->Wait(std::chrono::seconds(5));
		}
		ReportExit();
	}

	bool GameWindowSession::IsRunning()
	{
		return m_Process && m_Process->IsRunning();
	}

	void GameWindowSession::Update()
	{
		if (!m_Process)
			return;
		if (m_Process->IsRunning())
			ForwardLines(m_Process->ReadLines());
		else
			ReportExit();
	}

	void GameWindowSession::ForwardLines(const std::vector<std::string>& lines)
	{
		for (const std::string& line : lines)
		{
			const GameLogLine parsed = ParseLogLine(line);
			switch (parsed.Level)
			{
				case LogLevel::Trace:		BW_TRACE("[Game] {}", parsed.Text); break;
				case LogLevel::Info:		BW_INFO("[Game] {}", parsed.Text); break;
				case LogLevel::Warn:		BW_WARN("[Game] {}", parsed.Text); break;
				case LogLevel::Error:		BW_ERROR("[Game] {}", parsed.Text); break;
				case LogLevel::Critical:	BW_CRITICAL("[Game] {}", parsed.Text); break;
			}
		}
	}

	void GameWindowSession::ReportExit()
	{
		if (!m_Process)
			return;
		// The game's last lines (often why it failed) come before the note that it ended.
		ForwardLines(m_Process->ReadRemainingLines());
		const std::optional<int> exitCode = m_Process->GetExitCode();
		m_Process.reset();
		m_LastExitCode = exitCode;
		if (m_StoppedByEditor)
			BW_INFO("Game window stopped");
		else if (exitCode == 0)
			BW_INFO("Game window closed");
		else
			BW_ERROR("The game window ended with exit code {}: the game failed or crashed; its messages are above", exitCode.value_or(-1));
	}

	std::vector<std::string> GameWindowSession::BuildArguments(const Options& options)
	{
		std::vector<std::string> arguments = { "--project", options.ProjectFile.string(), "--editor", "--windowed" };
		if (!options.SceneFile.empty())
		{
			arguments.push_back("--scene-file");
			arguments.push_back(options.SceneFile.string());
		}
		if (!options.Title.empty())
		{
			arguments.push_back("--title");
			arguments.push_back(options.Title);
		}
		if (options.Headless)
			arguments.push_back("--headless");
		if (options.Frames > 0)
		{
			arguments.push_back("--frames");
			arguments.push_back(std::to_string(options.Frames));
		}
		return arguments;
	}

	GameWindowSession::GameLogLine GameWindowSession::ParseLogLine(std::string_view line)
	{
		// "[hh:mm:ss] [level] NAME: text"
		GameLogLine result{ LogLevel::Info, std::string(line) };
		if (line.size() < 2 || line[0] != '[')
			return result;
		const size_t timeEnd = line.find("] [");
		if (timeEnd == std::string_view::npos)
			return result;
		const size_t levelStart = timeEnd + 3;
		const size_t levelEnd = line.find(']', levelStart);
		if (levelEnd == std::string_view::npos)
			return result;
		const std::string_view level = line.substr(levelStart, levelEnd - levelStart);
		static constexpr std::array<std::pair<std::string_view, LogLevel>, 7> c_Levels = { {
			{ "trace", LogLevel::Trace }, { "debug", LogLevel::Trace }, { "info", LogLevel::Info },
			{ "warning", LogLevel::Warn }, { "warn", LogLevel::Warn }, { "error", LogLevel::Error }, { "critical", LogLevel::Critical } } };
		for (const auto& [name, value] : c_Levels)
		{
			if (level == name)
			{
				std::string_view text = line.substr(levelEnd + 1);
				if (!text.empty() && text.front() == ' ')
					text.remove_prefix(1);
				result.Level = value;
				result.Text = std::string(text);
				return result;
			}
		}
		return result;
	}

}
