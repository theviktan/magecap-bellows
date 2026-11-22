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
	//
	// Code that catches everything (a bare 'except:' in a loop) swallows the interrupt. If the same run
	// is still going a moment after it was stopped, every line it runs raises the interrupt again, so
	// it can't be caught where it is raised and reaches the code that started the run.
	//
	// The limit is on in the editor (and in games it starts) and off in a shipped game, where a long
	// load such as generating a world must not be cut off. A game can change it for the current play
	// with bellows.set_python_time_limit; the editor puts the default back when play stops.
	class PythonWatchdog
	{
	public:
		static constexpr double EditorTimeLimit = 10.0;	// Seconds

		// Starts and stops the watching thread, after Python starts and before it stops.
		static void Start();
		static void Stop();

		// The limit in seconds; 0 turns it off.
		static void SetTimeLimit(double seconds);
		static double GetTimeLimit();
		// The limit ResetTimeLimit goes back to: EditorTimeLimit unless the program says otherwise.
		static void SetDefaultTimeLimit(double seconds);
		static double GetDefaultTimeLimit();
		static void ResetTimeLimit();

		// The running Python is still making progress: restarts the current run's clock.
		static void Restart();

		// How many times a run was stopped since startup (tests).
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
