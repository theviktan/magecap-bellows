#pragma once

#include "ForgeScript/Core/Object.h"

#include <cstdint>
#include <vector>

namespace ForgeScript {

	enum CallFrameFlags : uint8_t
	{
		FrameNone = 0,
		FrameInitializer = 1 << 0, // "setup": the call returns the new object instead of its result.
		FrameDiscard = 1 << 1      // Field set up: the result is thrown away.
	};

	struct CallFrame
	{
		Closure* Function = nullptr;
		size_t Ip = 0;
		size_t Base = 0; // Stack index of slot 0 (the function itself or "self").
		uint8_t Flags = FrameNone;
		int DebugLine = 0; // The line the debugger last saw this frame on (only kept while debugging).
	};

	struct TryHandler
	{
		size_t FrameCount = 0;
		size_t StackSize = 0;
		size_t CatchIp = 0;
	};

	enum class TaskState : uint8_t
	{
		Ready = 0,   // Can run on the next update.
		Waiting,     // Paused by "wait".
		Running,     // Currently executing.
		Done,        // Finished normally.
		Failed,      // Stopped by an error nobody caught.
		Cancelled,   // Stopped by the host.
		Stopped      // Held by the debugger at a breakpoint, a step or a pause.
	};

	enum class StepMode : uint8_t
	{
		None = 0,
		In,   // Stop at the next line, in this function or one it calls.
		Over, // Stop at the next line in this function or a caller.
		Out   // Stop at the next line in a caller.
	};

	// A Task is one independent line of execution (a coroutine).
	// Every event handler and every "start" runs in its own task, which is what lets
	// scripts "wait 2 seconds" without freezing the game.
	struct Task : public Object
	{
		std::vector<Value> Stack;
		std::vector<CallFrame> Frames;
		std::vector<TryHandler> Handlers;
		Upvalue* OpenUpvalues = nullptr;

		TaskState State = TaskState::Ready;
		WaitMode Mode = WaitMode::Seconds;
		double WakeTime = 0.0;
		uint64_t WakeFrame = 0;
		bool CanYield = true;
		bool IsNested = false;    // Run by native code: errors go back to that code instead of the host.
		// How many 'trigger's this handler runs inside (0 for a task not started by an event, or after a
		// 'wait', when the trigger has returned). Kept by a debugger stop, which leaves the task inside.
		size_t EventDepth = 0;
		StepMode Step = StepMode::None;
		size_t StepDepth = 0;     // How many frames the task had when the step began.
		std::string FailureMessage;
		Value Result;
		Module* Owner = nullptr;

		Task() : Object(ObjectType::Task) {}
	};

}
