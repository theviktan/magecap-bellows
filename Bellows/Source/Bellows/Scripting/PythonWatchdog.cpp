#include "bwpch.h"
#include "Bellows/Scripting/PythonWatchdog.h"

#include <pybind11/pybind11.h>

#include <condition_variable>
#include <thread>

namespace Bellows::Scripting {

	namespace {

		using Clock = std::chrono::steady_clock;

		// Written on the main thread, read by the watching thread.
		std::atomic<int> s_Depth = 0;					// Nesting of PythonRun; 0 when no Python is watched
		std::atomic<uint64_t> s_RunID = 0;				// Changes with every outermost run
		std::atomic<int64_t> s_StartTicks = 0;			// When the current run (or its last frame) started
		std::atomic<double> s_TimeLimit = PythonWatchdog::DefaultTimeLimit;
		std::atomic<uint64_t> s_RequestedRun = 0;		// The run a check was queued for (0 for none)
		std::atomic<uint32_t> s_StopCount = 0;

		std::thread s_Thread;
		std::mutex s_Mutex;
		std::condition_variable s_Wake;
		bool s_Running = false;

		constexpr auto c_CheckInterval = std::chrono::milliseconds(100);

		int64_t Now()
		{
			return Clock::now().time_since_epoch().count();
		}

		bool IsOverLimit()
		{
			const double elapsed = std::chrono::duration<double>(Clock::duration(Now() - s_StartTicks.load())).count();
			return elapsed > s_TimeLimit.load();
		}

		// Runs on the main thread between two Python instructions (a Python "pending call"). By then the
		// run may have ended or been restarted, so it checks again; only a run still over its limit stops.
		int StopRunIfOverLimit(void* data)
		{
			const uint64_t runID = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(data));
			uint64_t expected = runID;
			s_RequestedRun.compare_exchange_strong(expected, 0);
			if (s_Depth.load() == 0 || s_RunID.load() != runID || !IsOverLimit())
				return 0;

			// The message travels with the exception into the traceback or the script error report, so the
			// stop is logged once, where the code that was stopped reports its errors.
			s_StopCount++;
			const std::string message = fmt::format("Python ran for more than {:g} seconds without returning, so it was stopped "
				"(a loop with no end?). Long work can be split over frames.", s_TimeLimit.load());
			PyErr_SetString(PyExc_KeyboardInterrupt, message.c_str());
			return -1;
		}

		void Watch()
		{
			std::unique_lock lock(s_Mutex);
			while (s_Running)
			{
				s_Wake.wait_for(lock, c_CheckInterval);
				if (!s_Running || s_Depth.load() == 0 || !IsOverLimit())
					continue;
				// One queued check per run at a time. Py_AddPendingCall needs neither the GIL nor a Python
				// thread state, so this thread never waits for Python.
				const uint64_t runID = s_RunID.load();
				uint64_t none = 0;
				if (s_RequestedRun.compare_exchange_strong(none, runID)
					&& Py_AddPendingCall(&StopRunIfOverLimit, reinterpret_cast<void*>(static_cast<uintptr_t>(runID))) != 0)
					s_RequestedRun = 0;	// The queue was full: try again next time
			}
		}

	}

	void PythonWatchdog::Start()
	{
		std::scoped_lock lock(s_Mutex);
		if (s_Running)
			return;
		s_Running = true;
		s_Thread = std::thread(&Watch);
	}

	void PythonWatchdog::Stop()
	{
		{
			std::scoped_lock lock(s_Mutex);
			if (!s_Running)
				return;
			s_Running = false;
		}
		s_Wake.notify_all();
		s_Thread.join();
		s_RequestedRun = 0;
	}

	void PythonWatchdog::SetTimeLimit(double seconds)
	{
		s_TimeLimit = std::max(seconds, 0.0);
	}

	double PythonWatchdog::GetTimeLimit()
	{
		return s_TimeLimit.load();
	}

	void PythonWatchdog::Restart()
	{
		s_StartTicks = Now();
	}

	uint32_t PythonWatchdog::GetStopCount()
	{
		return s_StopCount.load();
	}

	PythonRun::PythonRun()
	{
		if (s_Depth.load() == 0)
		{
			s_StartTicks = Now();
			s_RunID++;
		}
		s_Depth++;
	}

	PythonRun::~PythonRun()
	{
		s_Depth--;
	}

}
