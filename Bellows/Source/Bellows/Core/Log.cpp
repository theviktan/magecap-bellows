#include "bwpch.h"
#include "Bellows/Core/Log.h"

#include "Bellows/Core/Platform.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Bellows {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;
	std::mutex Log::s_HistoryMutex;
	std::vector<LogMessage> Log::s_History;
	size_t Log::s_HistoryCapacity = 4096;
	uint64_t Log::s_NextSequence = 1;
	std::atomic<uint32_t> Log::s_AssertionFailures = 0;
	std::atomic<uint32_t> Log::s_ErrorCount = 0;
	bool Log::s_BreakOnAssert = true;

	namespace {

		using Clock = std::chrono::steady_clock;
		Clock::time_point s_StartTime = Clock::now();

		LogLevel ConvertLevel(spdlog::level::level_enum level)
		{
			switch (level)
			{
				case spdlog::level::trace:
				case spdlog::level::debug:		return LogLevel::Trace;
				case spdlog::level::info:		return LogLevel::Info;
				case spdlog::level::warn:		return LogLevel::Warn;
				case spdlog::level::err:		return LogLevel::Error;
				case spdlog::level::critical:	return LogLevel::Critical;
				default:						return LogLevel::Info;
			}
		}

	}

	// Captures every log message into a bounded in-memory history.
	class HistorySink : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		explicit HistorySink(LogSource source)
			: m_Source(source) {}
	protected:
		void sink_it_(const spdlog::details::log_msg& message) override
		{
			LogMessage entry;
			entry.Level = ConvertLevel(message.level);
			entry.Source = m_Source;
			entry.Text = std::string(message.payload.data(), message.payload.size());
			entry.TimeSeconds = std::chrono::duration<double>(Clock::now() - s_StartTime).count();
			Log::PushHistory(std::move(entry));
		}

		void flush_() override {}
	private:
		LogSource m_Source;
	};

	void Log::Init()
	{
		Init(Specification{});
	}

	void Log::Init(const Specification& specification)
	{
		if (IsInitialized())
			return;

		s_StartTime = Clock::now();
		s_HistoryCapacity = std::max<size_t>(specification.HistoryCapacity, 16);

		std::vector<spdlog::sink_ptr> sharedSinks;
		if (specification.EnableConsole)
		{
			auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			consoleSink->set_pattern(specification.ConsoleIncludesLevel ? "[%T] [%l] %n: %v" : "%^[%T] %n: %v%$");
			sharedSinks.push_back(consoleSink);
		}

		if (!specification.LogFilePath.empty())
		{
			try
			{
				auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(specification.LogFilePath, true);
				fileSink->set_pattern("[%T] [%l] %n: %v");
				sharedSinks.push_back(fileSink);
			}
			catch (const spdlog::spdlog_ex& exception)
			{
				fprintf(stderr, "Failed to open log file '%s': %s\n", specification.LogFilePath.c_str(), exception.what());
			}
		}

		auto createLogger = [&sharedSinks](const char* name, LogSource source)
		{
			std::vector<spdlog::sink_ptr> sinks = sharedSinks;
			sinks.push_back(std::make_shared<HistorySink>(source));
			auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
			logger->set_level(spdlog::level::trace);
			logger->flush_on(spdlog::level::warn);
			return logger;
		};

		s_CoreLogger = createLogger("BELLOWS", LogSource::Core);
		s_ClientLogger = createLogger("APP", LogSource::Client);

#ifdef BW_DEBUG
		s_BreakOnAssert = true;
#else
		s_BreakOnAssert = false;
#endif
	}

	void Log::Shutdown()
	{
		if (s_CoreLogger)
			s_CoreLogger->flush();
		if (s_ClientLogger)
			s_ClientLogger->flush();
		s_CoreLogger.reset();
		s_ClientLogger.reset();
	}

	std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
	{
		if (!s_CoreLogger)
			Init();
		return s_CoreLogger;
	}

	std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
	{
		if (!s_ClientLogger)
			Init();
		return s_ClientLogger;
	}

	void Log::PushHistory(LogMessage&& message)
	{
		if (message.Level == LogLevel::Error || message.Level == LogLevel::Critical)
			s_ErrorCount++;

		std::scoped_lock lock(s_HistoryMutex);
		message.Sequence = s_NextSequence++;
		if (s_History.size() >= s_HistoryCapacity)
			s_History.erase(s_History.begin(), s_History.begin() + static_cast<std::ptrdiff_t>(s_HistoryCapacity / 4));
		s_History.push_back(std::move(message));
	}

	std::vector<LogMessage> Log::GetMessagesSince(uint64_t sequence)
	{
		std::scoped_lock lock(s_HistoryMutex);
		std::vector<LogMessage> result;
		for (const LogMessage& message : s_History)
		{
			if (message.Sequence > sequence)
				result.push_back(message);
		}
		return result;
	}

	uint64_t Log::GetLatestSequence()
	{
		std::scoped_lock lock(s_HistoryMutex);
		return s_NextSequence - 1;
	}

	void Log::ClearHistory()
	{
		std::scoped_lock lock(s_HistoryMutex);
		s_History.clear();
	}

	void Log::OnAssertionFailed()
	{
		s_AssertionFailures++;
		if (s_BreakOnAssert && Platform::IsDebuggerAttached())
			BW_DEBUGBREAK();
	}

}
