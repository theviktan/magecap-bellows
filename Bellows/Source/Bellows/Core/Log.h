#pragma once

#include "Bellows/Core/Base.h"

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Bellows {

	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical
	};

	enum class LogSource : uint8_t
	{
		Core = 0,	// Engine code
		Client,		// Game code (scripts, runtime, editor tools)
	};

	struct LogMessage
	{
		uint64_t Sequence = 0;
		LogLevel Level = LogLevel::Info;
		LogSource Source = LogSource::Core;
		std::string Text;
		double TimeSeconds = 0.0;	// Seconds since Log::Init
	};

	class Log
	{
	public:
		struct Specification
		{
			std::string LogFilePath;	// Empty disables file logging
			bool EnableConsole = true;
			size_t HistoryCapacity = 4096;
		};

		static void Init(const Specification& specification);
		static void Init();
		static void Shutdown();
		static bool IsInitialized() { return s_CoreLogger != nullptr; }

		static std::shared_ptr<spdlog::logger>& GetCoreLogger();
		static std::shared_ptr<spdlog::logger>& GetClientLogger();

		// Message history used by the editor console and the agent server.
		static std::vector<LogMessage> GetMessagesSince(uint64_t sequence);
		static uint64_t GetLatestSequence();
		static void ClearHistory();

		// Assertion bookkeeping. Tests verify that no assertion fired.
		static void OnAssertionFailed();
		static uint32_t GetAssertionFailureCount() { return s_AssertionFailures.load(); }
		static void ResetAssertionFailureCount() { s_AssertionFailures = 0; }
		static void SetBreakOnAssert(bool enabled) { s_BreakOnAssert = enabled; }

		// Number of error/critical messages written since startup (or the last reset).
		static uint32_t GetErrorCount() { return s_ErrorCount.load(); }
		static void ResetErrorCount() { s_ErrorCount = 0; }
	private:
		friend class HistorySink;
		static void PushHistory(LogMessage&& message);
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;

		static std::mutex s_HistoryMutex;
		static std::vector<LogMessage> s_History;
		static size_t s_HistoryCapacity;
		static uint64_t s_NextSequence;

		static std::atomic<uint32_t> s_AssertionFailures;
		static std::atomic<uint32_t> s_ErrorCount;
		static bool s_BreakOnAssert;
	};

}

// Core (engine) log macros
#define BW_CORE_TRACE(...)		::Bellows::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define BW_CORE_INFO(...)		::Bellows::Log::GetCoreLogger()->info(__VA_ARGS__)
#define BW_CORE_WARN(...)		::Bellows::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define BW_CORE_ERROR(...)		::Bellows::Log::GetCoreLogger()->error(__VA_ARGS__)
#define BW_CORE_CRITICAL(...)	::Bellows::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client (game, editor, scripts) log macros
#define BW_TRACE(...)			::Bellows::Log::GetClientLogger()->trace(__VA_ARGS__)
#define BW_INFO(...)			::Bellows::Log::GetClientLogger()->info(__VA_ARGS__)
#define BW_WARN(...)			::Bellows::Log::GetClientLogger()->warn(__VA_ARGS__)
#define BW_ERROR(...)			::Bellows::Log::GetClientLogger()->error(__VA_ARGS__)
#define BW_CRITICAL(...)		::Bellows::Log::GetClientLogger()->critical(__VA_ARGS__)
