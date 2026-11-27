#pragma once

#include "ForgeScript/Core/Object.h"
#include "ForgeScript/Core/ValueStack.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
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
		// Which values with defaults the call gave (bit i: the i-th of them), so their default code is skipped.
		uint64_t Given = ~uint64_t(0);
		int DebugLine = 0; // The line the debugger last saw this frame on (only kept while debugging).
	};

	// A task's call frames. It acts like std::vector<CallFrame> for the parts the VM uses, but adding a
	// frame is small enough to be inlined into the VM's main loop: with std::vector it was a function
	// call on every script function call (about a tenth of a call's cost).
	static_assert(std::is_trivially_copyable_v<CallFrame> && std::is_trivially_destructible_v<CallFrame>,
		"FrameStack keeps CallFrames in raw memory, which only works while a CallFrame is plain data.");

	class FrameStack
	{
	public:
		FrameStack() = default;
		FrameStack(const FrameStack&) = delete;
		FrameStack& operator=(const FrameStack&) = delete;

		size_t size() const { return m_Size; }
		bool empty() const { return m_Size == 0; }

		CallFrame& operator[](size_t index) { assert(index < m_Size); return m_Data.get()[index]; }
		const CallFrame& operator[](size_t index) const { assert(index < m_Size); return m_Data.get()[index]; }
		CallFrame& back() { assert(m_Size > 0); return m_Data.get()[m_Size - 1]; }
		const CallFrame& back() const { assert(m_Size > 0); return m_Data.get()[m_Size - 1]; }
		CallFrame& front() { assert(m_Size > 0); return m_Data.get()[0]; }
		const CallFrame& front() const { assert(m_Size > 0); return m_Data.get()[0]; }

		CallFrame* begin() { return m_Data.get(); }
		CallFrame* end() { return m_Data.get() + m_Size; }
		const CallFrame* begin() const { return m_Data.get(); }
		const CallFrame* end() const { return m_Data.get() + m_Size; }
		std::reverse_iterator<CallFrame*> rbegin() { return std::reverse_iterator<CallFrame*>(end()); }
		std::reverse_iterator<CallFrame*> rend() { return std::reverse_iterator<CallFrame*>(begin()); }

		// A new frame, as a default-made CallFrame.
		FORGESCRIPT_ALWAYS_INLINE CallFrame& emplace_back()
		{
			if (m_Size == m_Capacity)
				Grow();
			CallFrame* frame = m_Data.get() + m_Size++;
			*frame = CallFrame();
			return *frame;
		}
		FORGESCRIPT_ALWAYS_INLINE void push_back(const CallFrame& frame) { emplace_back() = frame; }
		void pop_back() { assert(m_Size > 0); m_Size--; }
		void clear() { m_Size = 0; }
		// Only ever shrinks: the VM uses it to drop the frames above a 'try'.
		void resize(size_t count) { assert(count <= m_Size); m_Size = count; }

	private:
		// Doubles the room; rare, since a task starts with room for 16 frames.
		void Grow()
		{
			const size_t capacity = m_Capacity ? m_Capacity * 2 : 16;
			std::unique_ptr<CallFrame, FreeMemory> data(static_cast<CallFrame*>(::operator new(capacity * sizeof(CallFrame))));
			if (m_Size)
				std::memcpy(data.get(), m_Data.get(), m_Size * sizeof(CallFrame));
			m_Data = std::move(data);
			m_Capacity = capacity;
		}

		struct FreeMemory
		{
			void operator()(CallFrame* data) const { ::operator delete(data); }
		};
		std::unique_ptr<CallFrame, FreeMemory> m_Data;
		size_t m_Size = 0;
		size_t m_Capacity = 0;
	};

	struct TryHandler
	{
		size_t FrameCount = 0;
		size_t StackSize = 0;
		size_t CatchIp = 0;
		// A 'finally' handler also receives where the error happened, so it can be raised again from there.
		bool Finally = false;
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
		ValueStack Stack;
		FrameStack Frames;
		std::vector<TryHandler> Handlers;
		// Names for the last values of the next call, set by NamedArgs just before it (kept alive by the
		// function's constants). Taken by the call, and cleared by any error.
		List* PendingNames = nullptr;
		Upvalue* OpenUpvalues = nullptr;

		TaskState State = TaskState::Ready;
		WaitMode Mode = WaitMode::Seconds;
		double WakeTime = 0.0;
		uint64_t WakeFrame = 0;
		bool CanYield = true;
		bool IsNested = false;    // Run by native code: errors go back to that code instead of the host.
		// 'task.cancel()' asked while this task is running (it cancelled itself, or it is inside a built-in
		// function that called the script that cancelled it): it stops before its next instruction.
		bool CancelRequested = false;
		// The function 'start' ran, for the task's text ('<task patrol>'), kept after its frames are gone.
		std::string EntryName;
		// The message of the error that ended it, as 'catch problem' would get it ('task.problem').
		std::string Problem;
		// How many 'trigger's this handler runs inside (0 for a task not started by an event, or after a
		// 'wait', when the trigger has returned). Kept by a debugger stop, which leaves the task inside.
		size_t EventDepth = 0;
		// For a task running a 'when my field changes' handler: the object and field it handles, so a write
		// to that field inside it doesn't run it again.
		Instance* ChangeInstance = nullptr;
		String* ChangeField = nullptr;
		// For a task running a 'when my field is set to value' handler: the object and field, so a write
		// to that field inside it stores the value instead of running the handler again.
		Instance* SetInstance = nullptr;
		String* SetField = nullptr;
		StepMode Step = StepMode::None;
		size_t StepDepth = 0;     // How many frames the task had when the step began.
		std::string FailureMessage;
		Value Result;
		Module* Owner = nullptr;
		// 'wait for hit with damage': the event, the entity that must raise it ("wait for "Start" is clicked";
		// empty for any), and how many of its values the task takes.
		String* WaitEvent = nullptr;
		std::string WaitSource;
		uint8_t WaitCount = 0;

		Task() : Object(ObjectType::Task) {}
	};

}
