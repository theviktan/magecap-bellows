#pragma once

#include "ForgeScript/API/Native.h"
#include "ForgeScript/Compiler/Compiler.h"
#include "ForgeScript/Compiler/Phrase.h"
#include "ForgeScript/Core/Object.h"
#include "ForgeScript/Core/Value.h"
#include "ForgeScript/VM/Task.h"

#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ForgeScript {

	// One step of the way to an error: where a function was when it called the next one (or failed).
	struct StackFrame
	{
		std::string File;     // the script's name, as it was loaded
		int Line = 0;         // 0 when unknown
		std::string Function; // the function's name, or "" for code at the top of a script
		int Repeats = 1;      // a function calling itself shows once, with how many times in a row
	};

	// A runtime error that was not caught by the script, reported to the host.
	struct ScriptError
	{
		std::string Message;
		std::string File;
		int Line = 0;
		std::vector<std::string> Trace; // "in function hurt (enemy.forge line 12)", innermost first.
		std::vector<StackFrame> Frames; // The same way, as data for tools, innermost first.

		std::string ToString() const;
	};

	// Result of loading or running a script.
	struct LoadResult
	{
		Module* LoadedModule = nullptr;
		bool Success = false;
		std::string ErrorMessage;
		Value ReturnValue;
	};

	using PrintHandler = std::function<void(const std::string&)>;
	using ErrorHandler = std::function<void(const ScriptError&)>;
	// Given the text after "use", find the script. Return false if it does not exist.
	using ModuleLoader = std::function<bool(const std::string& path, std::string& outName, std::string& outSource)>;
	// Called by "trigger name with values". The default sends the event to the same script. A handler
	// may throw RuntimeError: it becomes an error on the 'trigger' line, which scripts can catch.
	using TriggerHandler = std::function<void(Module* source, const std::string& event, const std::vector<Value>& values)>;

	// ---------------------------------------------------------------- Debugger

	// Why the debugger stopped a task.
	enum class DebugStopReason
	{
		Breakpoint,
		Step,
		Pause
	};

	struct DebugStop
	{
		DebugStopReason Reason = DebugStopReason::Breakpoint;
		std::string File; // The name the script was loaded with, as in error messages.
		int Line = 0;
	};

	// One function call on the stopped task's stack.
	struct DebugFrame
	{
		std::string Function; // Empty for a script's own top level.
		std::string File;
		int Line = 0;
		Module* Owner = nullptr; // For GetGlobals.
	};

	// A variable as the debugger shows it.
	struct DebugVariable
	{
		std::string Name;
		std::string Type; // As type() names it, or the kind's name.
		std::string Text; // As 'say' would print it, with text in quotes; long values are shortened.
	};

	// Called when a task stops. It must not wait for the user: note the stop, pause the game and return.
	// It may call Continue or a step, which take effect when it returns.
	using DebugHandler = std::function<void(const DebugStop&)>;

	class VM;

	// Keeps a value alive while C++ holds it. Values stored in C++ between frames must use a Handle,
	// otherwise the garbage collector may free them.
	class Handle
	{
	public:
		Handle() = default;
		Handle(VM& vm, Value value);
		~Handle();

		Handle(const Handle& other);
		Handle& operator=(const Handle& other);
		Handle(Handle&& other) noexcept;
		Handle& operator=(Handle&& other) noexcept;

		Value Get() const { return m_Value; }
		bool IsValid() const { return m_VM != nullptr; }
		void Reset();

	private:
		VM* m_VM = nullptr;
		Value m_Value;
	};

	struct VMConfig
	{
		// Stop runaway recursion with a clear error instead of a crash.
		size_t MaxCallDepth = 512;
		// Limit for built-in functions calling back into scripts (sort_by, map, to_text ...), and for
		// 'trigger' inside 'when' handlers, which run inside the triggering line.
		size_t MaxNestedCalls = 64;
		// Garbage collection starts after this many bytes have been allocated.
		size_t InitialGCThreshold = 1024 * 1024;
		// Stop a single run after this many instructions without a wait (0 = unlimited).
		// Protects the game from frozen frames caused by "while true do ... end" without a wait.
		uint64_t InstructionLimit = 0;
	};

	class VM
	{
	public:
		explicit VM(const VMConfig& config = VMConfig());
		~VM();

		VM(const VM&) = delete;
		VM& operator=(const VM&) = delete;

		// ---------------------------------------------------------- Host setup

		void SetPrintHandler(PrintHandler handler) { m_PrintHandler = std::move(handler); }
		void SetErrorHandler(ErrorHandler handler) { m_ErrorHandler = std::move(handler); }
		void SetModuleLoader(ModuleLoader loader) { m_ModuleLoader = std::move(loader); }
		void SetTriggerHandler(TriggerHandler handler) { m_TriggerHandler = std::move(handler); }

		// Makes a C++ function callable from every script:  random(1, 6)
		void RegisterFunction(const std::string& name, int arity, NativeFn fn);
		// Makes a C++ phrase usable from every script:  RegisterPhrase("move (thing) toward (target) by (amount)", ...)
		// Returns false (and reports why through outError) if the pattern is not valid.
		// With yieldToCalls, the phrase's first word stays a plain name wherever a call or value could
		// follow it, as it was before the phrase existed: "show(value)" still calls the show function,
		// and only "show menu" is the phrase. For phrases whose first word may already be a name.
		bool RegisterPhrase(const std::string& pattern, NativeFn fn, std::string* outError = nullptr, bool yieldToCalls = false);
		bool PhraseYieldsToCalls(const std::string& firstWord) const { return m_YieldingPhraseWords.count(firstWord) > 0; }
		// Shares a value with every script, like "player" or "gravity".
		void SetSharedGlobal(const std::string& name, Value value);
		Value GetSharedGlobal(const std::string& name);
		// Every name shared by all scripts (built-in functions, values like pi, host functions), sorted.
		// For tools such as the language server.
		std::vector<std::string> GetSharedGlobalNames() const;

		// ---------------------------------------------------------- Making values

		Value MakeText(std::string_view text);
		Value MakeList(std::vector<Value> items = {});
		Value MakeMap();
		Value MakeVector(double x, double y, double z);
		Value MakeNative(const NativeType& type, void* data);
		Value MakeFunction(const std::string& name, int arity, NativeFn fn);
		// Attaches a value to a native function so scripts can reach it as function.name.
		// Returns false when 'function' is not a native function.
		bool SetFunctionMember(const Value& function, const std::string& name, Value member);

		std::string ToText(const Value& value);
		std::string TypeOf(const Value& value) const;

		// ---------------------------------------------------------- Scripts

		// Creates an empty module (one per entity script is typical).
		Module* CreateModule(const std::string& name);
		// Compiles and runs code inside a module. The module keeps its variables and event handlers.
		LoadResult RunInModule(Module* module, std::string_view source, const std::string& fileName, const CompileOptions& options = CompileOptions());
		// Convenience: creates a module and runs the code in it.
		LoadResult Load(std::string_view source, const std::string& fileName);
		// Checks code for mistakes without running it, including a 'use' whose script the module
		// loader cannot find. Reports the first mistake.
		bool Check(std::string_view source, const std::string& fileName, CompileError& outError);
		// Like Check, but reports every mistake it can find (sorted by line), not only the first.
		std::vector<CompileError> FindMistakes(std::string_view source, const std::string& fileName);
		// Removes a module, cancels its tasks and lets the garbage collector free it.
		void UnloadModule(Module* module);
		// Forgets every script loaded with "use", so the next "use" reads the file again (hot reload).
		void ClearModuleCache();
		// The names of a module's script variables, functions and other globals, in the order they were made.
		std::vector<std::string> GetGlobalNames(Module* module) const;
		// Hot reload: gives every variable of 'to' that 'from' also has the value it has in 'from', so a
		// reloaded script keeps its state. Skipped: names in 'skip', constants, and functions, phrases, kinds
		// and modules (the new code wins), and values whose type changed (2 became "fast": the new value).
		// Returns the names that were carried over.
		std::vector<std::string> CarryOverVariables(Module* from, Module* to, const std::vector<std::string>& skip = {});

		Value GetGlobal(Module* module, const std::string& name);
		void SetGlobal(Module* module, const std::string& name, Value value);
		bool HasHandler(Module* module, const std::string& event);

		// Runs every "when <event>" handler in the module. Returns false if one of them failed. 'source' is
		// the name of the entity that raised the event, for handlers written 'when "Start" is clicked':
		// those run only when it matches their name, ignoring letter case, and never for an event
		// without a source.
		bool Emit(Module* module, const std::string& event, const std::vector<Value>& values = {}, const std::string& source = {});
		// Sends an event to every loaded module.
		void EmitAll(const std::string& event, const std::vector<Value>& values = {});

		// Calls a ForgeScript function and waits for its result. The function may not use "wait".
		// Returns false if it failed; outResult receives the return value.
		bool Call(const Value& callee, const std::vector<Value>& args, Value* outResult = nullptr);

		// Advances time: resumes every task whose "wait" has finished. Call once per frame.
		void Update(double deltaSeconds);
		double GetTime() const { return m_Time; }
		uint64_t GetFrame() const { return m_Frame; }

		size_t GetTaskCount() const { return m_Tasks.size(); }
		void CancelTasks(Module* module);

		// ---------------------------------------------------------- Debugger
		//
		// A stop holds one task where it is, like a 'wait' with no end, until Continue or a step. While
		// a task is stopped, Update runs nothing and time does not move, so the game behind it stays
		// still. Only code that may wait can stop: code that native functions call back (sort_by, map,
		// filter, to_text) and VM::Call never stop, and their breakpoints are passed over. Lines are
		// only checked while there are breakpoints, a pause request or a step, so a game without them
		// runs at full speed. Another task that reaches a breakpoint while one is stopped stops too and
		// waits its turn: when the first one goes on, the handler is called for the next.

		void SetDebugHandler(DebugHandler handler) { m_DebugHandler = std::move(handler); }
		// The lines (1-based) to stop at in one script, by the name it was loaded with. Replaces that
		// script's earlier breakpoints; an empty list removes them.
		void SetBreakpoints(const std::string& file, const std::vector<int>& lines);
		void ClearBreakpoints();
		// Stops the next task that runs a line.
		void Pause();
		// Turns the debugger off for what this VM runs from now on, for example while a game shuts down:
		// removes every breakpoint, the pause request and every step, and cancels the stopped tasks
		// (the current stop and those waiting their turn) without calling the handler. The handler
		// stays set.
		void StopDebugging();
		bool IsStopped() const { return m_StoppedTask != nullptr; }
		const DebugStop& GetStop() const { return m_Stop; }
		// Lets the stopped task go on. Each returns false when no task is stopped.
		bool Continue();
		bool StepOver();
		bool StepIn();
		bool StepOut();
		// The stopped task's calls, innermost first. Empty when nothing is stopped.
		std::vector<DebugFrame> GetCallStack() const;
		// The variables of one frame of the stopped task (0 = innermost): parameters, its own
		// variables that are in use at its line, 'self' in a kind's functions, and the outer variables
		// it uses.
		std::vector<DebugVariable> GetVariables(size_t frame);
		// A script's variables. Functions, phrases, kinds and used scripts are left out.
		std::vector<DebugVariable> GetGlobals(Module* module);

		void CollectGarbage();
		size_t GetBytesAllocated() const { return m_BytesAllocated; }
		size_t GetObjectCount() const { return m_ObjectCount; }

		void Print(const std::string& text);

		// Makes random numbers repeatable (useful for tests and replays).
		void SetRandomSeed(uint64_t seed) { m_Random.seed(seed); }
		std::mt19937_64& GetRandom() { return m_Random; }

		// ---------------------------------------------------------- Internal (used by the compiler and library)

		String* Intern(std::string_view text);
		Function* NewFunction();
		const PhraseTable& GetPhraseTable() const { return m_Phrases; }
		bool ResolveModuleSource(const std::string& path, std::string& outName, std::string& outSource);

		void Pin(Object* object);
		void Unpin(Object* object);

		// Calls a function from native code and returns its result, throwing RuntimeError on failure.
		Value CallFromNative(const Value& callee, const std::vector<Value>& args);
		// The script meaning of 'is' (lists and maps compare their contents); see ContentsEqual.
		bool ValuesEqual(const Value& a, const Value& b) const { return ContentsEqual(a, b); }

	private:
		template<typename T, typename... Args>
		T* Allocate(size_t extraBytes, Args&&... args);

		void RegisterLibrary();
		std::string Describe(const Value& value, bool quoteText);
		// ToText for use inside the interpreter loop: a failing "to_text" becomes a script error.
		bool SafeToText(const Value& value, std::string& outText);
		void RegisterMethod(std::unordered_map<String*, NativeFunction*>& table, const std::string& name, int arity, NativeFn fn);

		// Execution
		Task* NewTask(Module* owner);
		// Calls the callee that sits on the task's stack (with its arguments above it) and runs the task.
		void StartCall(Task* task, size_t argCount);
		void RunTask(Task* task);
		void Execute(Task* task);
		void RemovePending(Task* task);
		NativeFunction* NativeMethod(const NativeType::MethodInfo& info, const std::string& name);
		bool Fail(const std::string& message);

		bool CallValue(Task* task, Value callee, size_t argCount);
		bool CallClosure(Task* task, Closure* closure, size_t argCount, uint8_t flags);
		bool CallNative(Task* task, NativeFunction* native, size_t argCount, const Value& self);
		bool Invoke(Task* task, String* name, size_t argCount);
		bool InvokeFromKind(Task* task, Kind* kind, String* name, size_t argCount);
		bool ConstructInstance(Task* task, Kind* kind, size_t argCount);
		bool BindMethod(Task* task, const Value& receiver, String* name, Value& outValue);

		Upvalue* CaptureUpvalue(Task* task, size_t slot);
		void CloseUpvalues(Task* task, size_t fromSlot);

		bool GetProperty(Task* task, const Value& object, String* name, Value& outValue);
		bool SetProperty(Task* task, const Value& object, String* name, const Value& value);
		bool GetIndex(Task* task, const Value& object, const Value& index, Value& outValue);
		bool SetIndex(Task* task, const Value& object, const Value& index, const Value& value);
		bool LookupGlobal(Module* module, String* name, Value& outValue);
		// Every name code in this function can read, for a "Did you mean ...?" hint: its variables, its
		// script's and the shared ones.
		std::vector<std::string_view> GlobalNameCandidates(const Function* fn) const;

		bool Arithmetic(Task* task, OpCode op, const Value& a, const Value& b, Value& outValue);
		bool Compare(Task* task, OpCode op, const Value& a, const Value& b, bool& outResult);
		bool ContainsValue(Task* task, const Value& container, const Value& item, bool& outResult);
		Module* UseModule(Task* task, const std::string& path);

		// Debugger
		bool DebugLineReached(Task* task, CallFrame* frame);
		void TaskStopped(Task* task);
		bool Resume(StepMode step);
		void RunStops();
		void ReleaseStep(Task* task);
		void RefreshDebugActive();
		DebugVariable DescribeForDebugger(const std::string& name, const Value& value);

		// Errors
		bool RuntimeFailure(Task* task, const std::string& message);
		bool ThrowValue(Task* task, const Value& error, const Value* where = nullptr);
		bool CallWithNames(Task* task, Closure* closure, size_t argCount, uint8_t flags, List* names);
		void ReportFailure(Task* task, const Value& error, const Value* where = nullptr);
		Value DescribeWhere(Task* task);
		std::vector<std::string> BuildTrace(Task* task, std::string& outFile, int& outLine, std::vector<StackFrame>* outFrames = nullptr);

		// Garbage collection
		void MarkValue(const Value& value);
		void MarkObject(Object* object);
		void MarkRoots();
		void TraceReferences();
		void BlackenObject(Object* object);
		void Sweep();
		void MaybeCollect();

	public:
		// Methods available on built in types ("list.add(x)").
		std::unordered_map<String*, NativeFunction*> ListMethods;
		std::unordered_map<String*, NativeFunction*> TextMethods;
		std::unordered_map<String*, NativeFunction*> MapMethods;
		std::unordered_map<String*, NativeFunction*> VectorMethods;

	private:
		VMConfig m_Config;
		PrintHandler m_PrintHandler;
		ErrorHandler m_ErrorHandler;
		ModuleLoader m_ModuleLoader;
		TriggerHandler m_TriggerHandler;

		PhraseTable m_Phrases;
		std::unordered_map<String*, Value> m_SharedGlobals;
		std::unordered_map<std::string_view, String*> m_Strings;
		std::vector<Module*> m_Modules;
		std::unordered_map<std::string, Module*> m_UsedModules;
		struct CachedSource
		{
			std::string Name;
			std::string Source;
		};
		std::unordered_map<std::string, CachedSource> m_SourceCache;
		std::vector<Task*> m_Tasks;          // Tasks that are waiting to resume.
		std::vector<Task*> m_RunningTasks;   // Tasks currently executing (can be nested).
		std::vector<Task*> m_PendingTasks;   // Tasks created but not started yet.
		std::unordered_map<Object*, int> m_Pinned;
		std::unordered_map<const NativeType::MethodInfo*, NativeFunction*> m_NativeMethodCache;
		std::string m_ErrorMessage;

		// Garbage collector state
		Object* m_Objects = nullptr;
		std::vector<Object*> m_GrayStack;
		size_t m_BytesAllocated = 0;
		size_t m_NextGC = 0;
		size_t m_ObjectCount = 0;
		int m_GCPaused = 0;
		std::set<std::string> m_YieldingPhraseWords;

		// Debugger state
		DebugHandler m_DebugHandler;
		std::unordered_map<std::string, std::vector<int>> m_Breakpoints; // Sorted lines per script.
		bool m_DebugActive = false;
		bool m_PauseRequested = false;
		size_t m_SteppingTasks = 0;
		Task* m_StoppedTask = nullptr;
		std::vector<std::pair<Task*, DebugStop>> m_QueuedStops;
		DebugStop m_PendingStop; // Set by DebugLineReached for the task that is stopping.
		DebugStop m_Stop;
		bool m_InDebugHandler = false;
		bool m_RunningStops = false;
		bool m_ResumeRequested = false;
		StepMode m_ResumeStep = StepMode::None;
		bool m_PlainText = false; // Describe without calling kinds' to_text (the debugger runs no script code).

		double m_Time = 0.0;
		uint64_t m_Frame = 0;
		std::mt19937_64 m_Random{ 0x5eed };
		int m_TextDepth = 0;

		String* m_SetupName = nullptr;
		String* m_SelfName = nullptr;

		friend class Handle;
		friend class CallContext;
		friend struct GCPause;
	};

	template<typename T, typename... Args>
	T* VM::Allocate(size_t extraBytes, Args&&... args)
	{
		// Objects are only ever freed by the garbage collector, which runs between instructions,
		// so a new object is safe until the code that made it finishes its current step.
		T* object = new T(std::forward<Args>(args)...);
		size_t size = sizeof(T) + extraBytes;
		object->m_AllocatedSize = static_cast<uint32_t>(size < 0xffffffffu ? size : 0xffffffffu);
		object->m_Next = m_Objects;
		m_Objects = object;
		m_BytesAllocated += object->m_AllocatedSize;
		m_ObjectCount++;
		return object;
	}

	// Pauses the garbage collector while it exists (used while compiling).
	struct GCPause
	{
		explicit GCPause(VM& vm) : m_VM(vm) { m_VM.m_GCPaused++; }
		~GCPause() { m_VM.m_GCPaused--; }
		GCPause(const GCPause&) = delete;
		GCPause& operator=(const GCPause&) = delete;

	private:
		VM& m_VM;
	};

}
