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
		std::atomic<double> s_TimeLimit = PythonWatchdog::EditorTimeLimit;
		std::atomic<double> s_DefaultTimeLimit = PythonWatchdog::EditorTimeLimit;
		std::atomic<uint64_t> s_RequestedRun = 0;		// The run a check was queued for (0 for none)
		std::atomic<uint32_t> s_StopCount = 0;

		// Main thread only: when the current run was first stopped, and whether every line now raises.
		uint64_t s_StoppedRun = 0;
		Clock::time_point s_FirstStopTime;
		bool s_TraceInstalled = false;

		std::thread s_Thread;
		std::mutex s_Mutex;
		std::condition_variable s_Wake;
		bool s_Running = false;

		constexpr auto c_CheckInterval = std::chrono::milliseconds(100);
		// How long a stopped run may go on (handling the interrupt, cleaning up) before every line raises.
		constexpr auto c_GraceTime = std::chrono::seconds(1);

		int64_t Now()
		{
			return Clock::now().time_since_epoch().count();
		}

		bool IsOverLimit()
		{
			const double limit = s_TimeLimit.load();
			if (limit <= 0.0)
				return false;
			const double elapsed = std::chrono::duration<double>(Clock::duration(Now() - s_StartTicks.load())).count();
			return elapsed > limit;
		}

		std::string StopMessage(bool again)
		{
			if (again)
			{
				return fmt::format("Python was stopped after {:g} seconds but kept running (an 'except:' that catches everything?), "
					"so it is being stopped on every line.", s_TimeLimit.load());
			}
			return fmt::format("Python ran for more than {:g} seconds without returning, so it was stopped (a loop with no end?). "
				"Long work can be split over frames, or allowed with bellows.set_python_time_limit.", s_TimeLimit.load());
		}

		// The trace function of a run that swallowed its interrupt: each line raises it again, so it can't
		// be caught where it is raised (an 'except:' block's own lines raise too).
		int RaiseOnEveryLine(PyObject*, PyFrameObject*, int what, PyObject*)
		{
			if (what != PyTrace_LINE)
				return 0;
			PyErr_SetString(PyExc_KeyboardInterrupt, StopMessage(true).c_str());
			return -1;
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
			const bool again = s_StoppedRun == runID;
			if (!again)
			{
				s_StoppedRun = runID;
				s_FirstStopTime = Clock::now();
				s_StopCount++;
			}
			else if (!s_TraceInstalled && Clock::now() - s_FirstStopTime > c_GraceTime)
			{
				PyEval_SetTrace(&RaiseOnEveryLine, nullptr);
				s_TraceInstalled = true;
			}
			PyErr_SetString(PyExc_KeyboardInterrupt, StopMessage(again).c_str());
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
		s_TimeLimit = std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0;
	}

	double PythonWatchdog::GetTimeLimit()
	{
		return s_TimeLimit.load();
	}

	void PythonWatchdog::SetDefaultTimeLimit(double seconds)
	{
		s_DefaultTimeLimit = std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0;
		ResetTimeLimit();
	}

	double PythonWatchdog::GetDefaultTimeLimit()
	{
		return s_DefaultTimeLimit.load();
	}

	void PythonWatchdog::ResetTimeLimit()
	{
		s_TimeLimit = s_DefaultTimeLimit.load();
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
		if (--s_Depth == 0 && s_TraceInstalled)
		{
			// The stuck run has ended: lines run normally again.
			PyEval_SetTrace(nullptr, nullptr);
			s_TraceInstalled = false;
		}
	}

}
