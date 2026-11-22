#pragma once

#include <cstdint>

namespace Bellows::Scripting {

	// Stops Python that runs too long on the main thread, where it freezes the editor or the game: a
	// loop with no end in the editor's Console, or in a game script's on_update. ForgeScript has the
	// same guard ("This script ran too long without waiting").
	//
	// Code that runs Python wraps it in a PythonRun. When the outermost run goes past the time limit,
	// KeyboardInterrupt is raised in it, between two Python instructions, and the error says why. A
	// single long call into C (sum(range(10**10))) can't be interrupted until it returns. Running
	// editor frames (bellows.editor.run_frames) restarts the clock, so automation that lets time pass
	// is never stopped. Only Python inside a run is watched: automation scripts started with
	// --run-script may take as long as they need.
	class PythonWatchdog
	{
	public:
		static constexpr double DefaultTimeLimit = 10.0;	// Seconds

		// Starts and stops the watching thread, after Python starts and before it stops.
		static void Start();
		static void Stop();

		static void SetTimeLimit(double seconds);
		static double GetTimeLimit();

		// The running Python is still making progress: restarts the current run's clock.
		static void Restart();

		// How many runs were stopped since startup (tests).
		static uint32_t GetStopCount();
	};

	// Marks Python running on the main thread. Runs nest; the outermost one is timed.
	class PythonRun
	{
	public:
		PythonRun();
		~PythonRun();
		PythonRun(const PythonRun&) = delete;
		PythonRun& operator=(const PythonRun&) = delete;
	};

}
