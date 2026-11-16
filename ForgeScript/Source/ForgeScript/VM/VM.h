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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ForgeScript {

	// A runtime error that was not caught by the script, reported to the host.
	struct ScriptError
	{
		std::string Message;
		std::string File;
		int Line = 0;
		std::vector<std::string> Trace; // "in function hurt (enemy.forge line 12)", innermost first.

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
	// Called by "trigger name with values". The default sends the event to the same script.
	using TriggerHandler = std::function<void(Module* source, const std::string& event, const std::vector<Value>& values)>;

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
		// Limit for built-in functions calling back into scripts (sort_by, map, to_text ...).
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
		bool RegisterPhrase(const std::string& pattern, NativeFn fn, std::string* outError = nullptr);
		// Shares a value with every script, like "player" or "gravity".
		void SetSharedGlobal(const std::string& name, Value value);
		Value GetSharedGlobal(const std::string& name);

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
		// Checks code for mistakes without running it.
		bool Check(std::string_view source, const std::string& fileName, CompileError& outError);
		// Removes a module, cancels its tasks and lets the garbage collector free it.
		void UnloadModule(Module* module);
		// Forgets every script loaded with "use", so the next "use" reads the file again (hot reload).
		void ClearModuleCache();

		Value GetGlobal(Module* module, const std::string& name);
		void SetGlobal(Module* module, const std::string& name, Value value);
		bool HasHandler(Module* module, const std::string& event);

		// Runs every "when <event>" handler in the module. Returns false if one of them failed.
		bool Emit(Module* module, const std::string& event, const std::vector<Value>& values = {});
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

		bool Arithmetic(Task* task, OpCode op, const Value& a, const Value& b, Value& outValue);
		bool Compare(Task* task, OpCode op, const Value& a, const Value& b, bool& outResult);
		bool ContainsValue(Task* task, const Value& container, const Value& item, bool& outResult);
		Module* UseModule(Task* task, const std::string& path);

		// Errors
		bool RuntimeFailure(Task* task, const std::string& message);
		bool ThrowValue(Task* task, const Value& error);
		void ReportFailure(Task* task, const Value& error);
		std::vector<std::string> BuildTrace(Task* task, std::string& outFile, int& outLine);

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
