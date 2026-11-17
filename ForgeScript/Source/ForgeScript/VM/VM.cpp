#include "ForgeScript/VM/VM.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace ForgeScript {

	// ======================================================================= Small helpers

	std::string ScriptError::ToString() const
	{
		std::string text = "Error: " + Message;
		for (const std::string& line : Trace)
			text += "\n    " + line;
		return text;
	}

	namespace {

		// Converts a 1 based ForgeScript position (negative counts from the end) into a 0 based index.
		bool ResolvePosition(const Value& position, size_t size, size_t& outIndex, std::string& outError)
		{
			if (!position.IsNumber())
			{
				outError = std::string("A position must be a number, not ") + TypeName(position) + ".";
				return false;
			}
			double number = position.AsNumber();
			if (number != std::floor(number))
			{
				outError = "A position must be a whole number, not " + FormatNumber(number) + ".";
				return false;
			}
			if (number == 0)
			{
				outError = "Positions start at 1 in ForgeScript (use 1 for the first item, -1 for the last).";
				return false;
			}
			long long index = static_cast<long long>(number);
			if (index < 0)
				index = static_cast<long long>(size) + index + 1;
			if (index < 1 || index > static_cast<long long>(size))
			{
				outError = "Position " + FormatNumber(number) + " is outside the list, which has " +
					std::to_string(size) + (size == 1 ? " item." : " items.");
				return false;
			}
			outIndex = static_cast<size_t>(index - 1);
			return true;
		}

		bool IsCallableValue(const Value& value)
		{
			if (!value.IsObject())
				return false;
			switch (value.AsObject()->GetType())
			{
				case ObjectType::Closure: case ObjectType::NativeFunction:
				case ObjectType::BoundMethod: case ObjectType::Kind:
					return true;
				default:
					return false;
			}
		}

	}

	// ======================================================================= Handle

	Handle::Handle(VM& vm, Value value)
		: m_VM(&vm), m_Value(value)
	{
		if (m_Value.IsObject())
			m_VM->Pin(m_Value.AsObject());
	}

	Handle::~Handle()
	{
		Reset();
	}

	Handle::Handle(const Handle& other)
		: m_VM(other.m_VM), m_Value(other.m_Value)
	{
		if (m_VM && m_Value.IsObject())
			m_VM->Pin(m_Value.AsObject());
	}

	Handle& Handle::operator=(const Handle& other)
	{
		if (this != &other)
		{
			Reset();
			m_VM = other.m_VM;
			m_Value = other.m_Value;
			if (m_VM && m_Value.IsObject())
				m_VM->Pin(m_Value.AsObject());
		}
		return *this;
	}

	Handle::Handle(Handle&& other) noexcept
		: m_VM(other.m_VM), m_Value(other.m_Value)
	{
		other.m_VM = nullptr;
		other.m_Value = Value();
	}

	Handle& Handle::operator=(Handle&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_VM = other.m_VM;
			m_Value = other.m_Value;
			other.m_VM = nullptr;
			other.m_Value = Value();
		}
		return *this;
	}

	void Handle::Reset()
	{
		if (m_VM && m_Value.IsObject())
			m_VM->Unpin(m_Value.AsObject());
		m_VM = nullptr;
		m_Value = Value();
	}

	// ======================================================================= CallContext

	Value CallContext::Arg(size_t index) const
	{
		if (index >= m_Count)
			return Value();
		return m_Task->Stack[m_Base + index];
	}

	void CallContext::Error(const std::string& message) const
	{
		throw RuntimeError(message);
	}

	void CallContext::TypeError(size_t index, const char* expected) const
	{
		Value value = Arg(index);
		std::string which = index == static_cast<size_t>(-1) ? std::string("its object") : "value " + std::to_string(index + 1);
		throw RuntimeError(m_Name + " needs " + expected + " for " + which + ", but got " + m_VM.TypeOf(value) + ".");
	}

	double CallContext::Number(size_t index) const
	{
		Value value = Arg(index);
		if (!value.IsNumber())
			TypeError(index, "a number");
		return value.AsNumber();
	}

	int64_t CallContext::Integer(size_t index) const
	{
		double number = Number(index);
		if (number != std::floor(number))
			throw RuntimeError(m_Name + " needs a whole number for value " + std::to_string(index + 1) + ", but got " + FormatNumber(number) + ".");
		return static_cast<int64_t>(number);
	}

	bool CallContext::Bool(size_t index) const
	{
		return Arg(index).IsTruthy();
	}

	String* CallContext::TextObject(size_t index) const
	{
		Value value = Arg(index);
		if (!IsObjectType(value, ObjectType::String))
			TypeError(index, "text");
		return As<String>(value);
	}

	const std::string& CallContext::Text(size_t index) const
	{
		return TextObject(index)->Text;
	}

	List* CallContext::ListArg(size_t index) const
	{
		Value value = Arg(index);
		if (!IsObjectType(value, ObjectType::List))
			TypeError(index, "a list");
		return As<List>(value);
	}

	Map* CallContext::MapArg(size_t index) const
	{
		Value value = Arg(index);
		if (!IsObjectType(value, ObjectType::Map))
			TypeError(index, "a map");
		return As<Map>(value);
	}

	Vector* CallContext::VectorArg(size_t index) const
	{
		Value value = Arg(index);
		if (!IsObjectType(value, ObjectType::Vector))
			TypeError(index, "a vector");
		return As<Vector>(value);
	}

	Closure* CallContext::FunctionArg(size_t index) const
	{
		Value value = Arg(index);
		if (!IsObjectType(value, ObjectType::Closure))
			TypeError(index, "a function");
		return As<Closure>(value);
	}

	bool CallContext::IsCallable(size_t index) const
	{
		return IsCallableValue(Arg(index));
	}

	void* CallContext::NativeData(const Value& value, const NativeType& type, size_t index) const
	{
		if (!IsObjectType(value, ObjectType::NativeObject) || As<NativeObject>(value)->Type != &type)
		{
			if (index == static_cast<size_t>(-1))
				throw RuntimeError(m_Name + " can only be used on a " + type.Name + ".");
			TypeError(index, ("a " + type.Name).c_str());
		}
		NativeObject* object = As<NativeObject>(value);
		if (type.IsAlive && !type.IsAlive(object->Data))
			throw RuntimeError("This " + type.Name + " no longer exists (it was destroyed).");
		return object->Data;
	}

	// ======================================================================= Construction

	VM::VM(const VMConfig& config)
		: m_Config(config)
	{
		m_NextGC = m_Config.InitialGCThreshold;
		m_PrintHandler = [](const std::string& text) { std::cout << text << std::endl; };
		m_ErrorHandler = [](const ScriptError& error) { std::cerr << error.ToString() << std::endl; };

		GCPause pause(*this);
		m_SetupName = Intern("setup");
		m_SelfName = Intern("self");
		RegisterLibrary();
	}

	VM::~VM()
	{
		Object* object = m_Objects;
		while (object)
		{
			Object* next = object->m_Next;
			delete object;
			object = next;
		}
		m_Objects = nullptr;
	}

	String* VM::Intern(std::string_view text)
	{
		auto it = m_Strings.find(text);
		if (it != m_Strings.end())
			return it->second;

		std::string owned(text);
		size_t hash = std::hash<std::string>{}(owned);
		String* string = Allocate<String>(owned.size(), std::move(owned), hash);
		m_Strings.emplace(std::string_view(string->Text), string);
		return string;
	}

	Function* VM::NewFunction()
	{
		return Allocate<Function>(0);
	}

	void VM::Pin(Object* object)
	{
		m_Pinned[object]++;
	}

	void VM::Unpin(Object* object)
	{
		auto it = m_Pinned.find(object);
		if (it == m_Pinned.end())
			return;
		if (--it->second <= 0)
			m_Pinned.erase(it);
	}

	void VM::Print(const std::string& text)
	{
		if (m_PrintHandler)
			m_PrintHandler(text);
	}

	// ======================================================================= Host API

	void VM::RegisterFunction(const std::string& name, int arity, NativeFn fn)
	{
		GCPause pause(*this);
		NativeFunction* native = Allocate<NativeFunction>(name.size(), name, arity, std::move(fn));
		m_SharedGlobals[Intern(name)] = Value::FromObject(native);
	}

	bool VM::RegisterPhrase(const std::string& pattern, NativeFn fn, std::string* outError)
	{
		PhrasePattern parsed;
		std::string error;
		if (!ParsePhrasePattern(pattern, parsed, error))
		{
			if (outError)
				*outError = error;
			return false;
		}
		static const char* s_Blocked[] = { "if", "then", "else", "otherwise", "end", "while", "until", "do",
			"repeat", "for", "function", "phrase", "kind", "when", "try", "catch" };
		for (const std::string& part : parsed.Parts)
		{
			for (const char* word : s_Blocked)
			{
				if (part == word)
				{
					if (outError)
						*outError = "The word '" + part + "' cannot be used inside a phrase.";
					return false;
				}
			}
		}

		Lexer headLexer(parsed.Parts.front());
		if (headLexer.Tokenize().front().Type != TokenType::Identifier)
		{
			if (outError)
				*outError = "A phrase cannot start with the ForgeScript keyword '" + parsed.Parts.front() + "'.";
			return false;
		}

		std::string name = parsed.MangledName();
		if (!m_Phrases.Add(parsed))
		{
			if (outError)
				*outError = "The phrase '" + pattern + "' already exists.";
			return false;
		}
		RegisterFunction(name, static_cast<int>(parsed.SlotCount()), std::move(fn));
		return true;
	}

	void VM::SetSharedGlobal(const std::string& name, Value value)
	{
		GCPause pause(*this);
		m_SharedGlobals[Intern(name)] = value;
	}

	std::vector<std::string> VM::GetSharedGlobalNames() const
	{
		std::vector<std::string> names;
		for (const auto& [name, value] : m_SharedGlobals)
			names.push_back(name->Text);
		std::sort(names.begin(), names.end());
		return names;
	}

	Value VM::GetSharedGlobal(const std::string& name)
	{
		auto it = m_SharedGlobals.find(Intern(name));
		return it == m_SharedGlobals.end() ? Value() : it->second;
	}

	Value VM::MakeText(std::string_view text) { return Value::FromObject(Intern(text)); }

	Value VM::MakeList(std::vector<Value> items)
	{
		List* list = Allocate<List>(items.size() * sizeof(Value));
		list->Items = std::move(items);
		return Value::FromObject(list);
	}

	Value VM::MakeMap() { return Value::FromObject(Allocate<Map>(0)); }

	Value VM::MakeVector(double x, double y, double z) { return Value::FromObject(Allocate<Vector>(0, x, y, z)); }

	Value VM::MakeNative(const NativeType& type, void* data) { return Value::FromObject(Allocate<NativeObject>(0, &type, data)); }

	Value VM::MakeFunction(const std::string& name, int arity, NativeFn fn)
	{
		return Value::FromObject(Allocate<NativeFunction>(name.size(), name, arity, std::move(fn)));
	}

	bool VM::SetFunctionMember(const Value& function, const std::string& name, Value member)
	{
		if (!IsObjectType(function, ObjectType::NativeFunction))
			return false;
		As<NativeFunction>(function)->Members[name] = member;
		return true;
	}

	std::string VM::TypeOf(const Value& value) const
	{
		if (IsObjectType(value, ObjectType::Instance))
			return As<Instance>(value)->Class->Name->Text;
		if (IsObjectType(value, ObjectType::NativeObject))
			return As<NativeObject>(value)->Type->Name;
		return TypeName(value);
	}

	Module* VM::CreateModule(const std::string& name)
	{
		Module* module = Allocate<Module>(name.size(), name);
		m_Modules.push_back(module);
		return module;
	}

	void VM::UnloadModule(Module* module)
	{
		CancelTasks(module);
		m_Modules.erase(std::remove(m_Modules.begin(), m_Modules.end(), module), m_Modules.end());
	}

	Value VM::GetGlobal(Module* module, const std::string& name)
	{
		auto it = module->Globals.find(Intern(name));
		return it == module->Globals.end() ? Value() : it->second;
	}

	void VM::SetGlobal(Module* module, const std::string& name, Value value)
	{
		GCPause pause(*this);
		module->SetGlobal(Intern(name), value);
	}

	bool VM::HasHandler(Module* module, const std::string& event)
	{
		auto it = module->Handlers.find(Intern(event));
		return it != module->Handlers.end() && !it->second.empty();
	}

	bool VM::Check(std::string_view source, const std::string& fileName, CompileError& outError)
	{
		GCPause pause(*this);
		Module* scratch = Allocate<Module>(0, fileName);
		CompileOptions options;
		options.ReportMissingScripts = true;
		return CompileScript(*this, scratch, source, fileName, options, outError) != nullptr;
	}

	std::vector<CompileError> VM::FindMistakes(std::string_view source, const std::string& fileName)
	{
		GCPause pause(*this);
		Module* scratch = Allocate<Module>(0, fileName);
		CompileOptions options;
		options.ReportMissingScripts = true;
		return FindCompileErrors(*this, scratch, source, fileName, options);
	}

	LoadResult VM::RunInModule(Module* module, std::string_view source, const std::string& fileName, const CompileOptions& options)
	{
		LoadResult result;
		result.LoadedModule = module;

		CompileError error;
		Function* fn = nullptr;
		{
			GCPause pause(*this);
			fn = CompileScript(*this, module, source, fileName, options, error);
		}
		if (!fn)
		{
			result.ErrorMessage = error.ToString();
			if (m_ErrorHandler)
			{
				ScriptError report;
				report.Message = error.Message;
				report.File = error.File;
				report.Line = error.Line;
				report.Trace.push_back("in " + (error.File.empty() ? std::string("script") : error.File) + " line " + std::to_string(error.Line) +
					(error.Column > 0 ? ", column " + std::to_string(error.Column) : std::string()));
				m_ErrorHandler(report);
			}
			return result;
		}

		Task* task = NewTask(module);
		task->Stack.push_back(Value::FromObject(Allocate<Closure>(0, fn)));
		StartCall(task, 0);
		module->HasRun = true;

		if (task->State == TaskState::Failed)
		{
			result.ErrorMessage = task->FailureMessage;
			return result;
		}
		result.Success = true;
		if (task->State == TaskState::Done)
			result.ReturnValue = task->Result;
		return result;
	}

	LoadResult VM::Load(std::string_view source, const std::string& fileName)
	{
		Module* module = CreateModule(fileName);
		return RunInModule(module, source, fileName);
	}

	bool VM::Emit(Module* module, const std::string& event, const std::vector<Value>& values)
	{
		auto it = module->Handlers.find(Intern(event));
		if (it == module->Handlers.end() || it->second.empty())
			return true;

		// Create every task before running any, so all of them hold the values safely.
		std::vector<Closure*> handlers = it->second;
		std::vector<Task*> tasks;
		for (Closure* handler : handlers)
		{
			Task* task = NewTask(module);
			task->Stack.push_back(Value::FromObject(handler));
			int arity = handler->Fn->Arity;
			for (int i = 0; i < arity; i++)
				task->Stack.push_back(static_cast<size_t>(i) < values.size() ? values[static_cast<size_t>(i)] : Value());
			tasks.push_back(task);
			m_PendingTasks.push_back(task);
		}

		bool success = true;
		for (Task* task : tasks)
		{
			StartCall(task, task->Stack.size() - 1);
			RemovePending(task);
			if (task->State == TaskState::Failed)
				success = false;
		}
		return success;
	}

	void VM::EmitAll(const std::string& event, const std::vector<Value>& values)
	{
		std::vector<Value> held = values;
		std::vector<Module*> modules = m_Modules;
		for (Module* module : modules)
		{
			// The module may have been unloaded by an earlier handler.
			if (std::find(m_Modules.begin(), m_Modules.end(), module) != m_Modules.end())
				Emit(module, event, held);
		}
	}

	bool VM::Call(const Value& callee, const std::vector<Value>& args, Value* outResult)
	{
		Module* owner = nullptr;
		if (IsObjectType(callee, ObjectType::Closure))
			owner = As<Closure>(callee)->Fn->Owner;

		Task* task = NewTask(owner);
		task->CanYield = false;
		task->Stack.push_back(callee);
		for (const Value& arg : args)
			task->Stack.push_back(arg);
		m_PendingTasks.push_back(task);
		StartCall(task, args.size());
		RemovePending(task);

		if (task->State == TaskState::Failed)
			return false;
		if (outResult)
			*outResult = task->Result;
		return true;
	}

	Value VM::CallFromNative(const Value& callee, const std::vector<Value>& args)
	{
		// Each nested call uses C++ stack space, so stop runaway nesting with a clear error.
		if (m_RunningTasks.size() >= m_Config.MaxNestedCalls)
			throw RuntimeError("Too many functions calling each other through built-in functions.");

		Module* owner = nullptr;
		if (IsObjectType(callee, ObjectType::Closure))
			owner = As<Closure>(callee)->Fn->Owner;

		Task* task = NewTask(owner);
		task->CanYield = false;
		task->IsNested = true;
		task->Stack.push_back(callee);
		for (const Value& arg : args)
			task->Stack.push_back(arg);
		m_PendingTasks.push_back(task);
		StartCall(task, args.size());
		RemovePending(task);

		if (task->State == TaskState::Failed)
			throw RuntimeError(task->FailureMessage);
		return task->Result;
	}

	void VM::Update(double deltaSeconds)
	{
		if (deltaSeconds > 0)
			m_Time += deltaSeconds;
		m_Frame++;

		std::vector<Task*> ready;
		std::vector<Task*> keep;
		for (Task* task : m_Tasks)
		{
			if (task->State != TaskState::Waiting)
				continue;
			bool wake = task->Mode == WaitMode::Seconds ? m_Time >= task->WakeTime : m_Frame >= task->WakeFrame;
			(wake ? ready : keep).push_back(task);
		}
		m_Tasks = std::move(keep);

		m_PendingTasks.insert(m_PendingTasks.end(), ready.begin(), ready.end());
		for (Task* task : ready)
		{
			RemovePending(task);
			if (task->State == TaskState::Waiting)
				RunTask(task);
		}
	}

	void VM::CancelTasks(Module* module)
	{
		for (Task* task : m_Tasks)
		{
			if (!module || task->Owner == module)
			{
				task->State = TaskState::Cancelled;
				CloseUpvalues(task, 0);
			}
		}
		m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(),
			[](Task* task) { return task->State == TaskState::Cancelled; }), m_Tasks.end());
	}

	// ======================================================================= Tasks

	Task* VM::NewTask(Module* owner)
	{
		Task* task = Allocate<Task>(256 * sizeof(Value));
		task->Owner = owner;
		task->Stack.reserve(256);
		return task;
	}

	void VM::RemovePending(Task* task)
	{
		auto it = std::find(m_PendingTasks.begin(), m_PendingTasks.end(), task);
		if (it != m_PendingTasks.end())
			m_PendingTasks.erase(it);
	}

	void VM::StartCall(Task* task, size_t argCount)
	{
		m_RunningTasks.push_back(task);
		Value callee = task->Stack[task->Stack.size() - 1 - argCount];
		bool ok = CallValue(task, callee, argCount);
		m_RunningTasks.pop_back();

		if (!ok)
		{
			// The call failed before any code ran (for example the wrong number of values).
			ReportFailure(task, MakeText(m_ErrorMessage));
			return;
		}
		if (task->Frames.empty())
		{
			// A native function finished immediately.
			task->Result = task->Stack.empty() ? Value() : task->Stack.back();
			task->Stack.clear();
			task->State = TaskState::Done;
			return;
		}
		RunTask(task);
	}

	void VM::RunTask(Task* task)
	{
		m_RunningTasks.push_back(task);
		Execute(task);
		m_RunningTasks.pop_back();

		if (task->State == TaskState::Waiting)
		{
			if (std::find(m_Tasks.begin(), m_Tasks.end(), task) == m_Tasks.end())
				m_Tasks.push_back(task);
		}
	}

	// ======================================================================= Calls

	bool VM::Fail(const std::string& message)
	{
		m_ErrorMessage = message;
		return false;
	}

	static std::string FunctionLabel(Function* fn)
	{
		return fn->Name ? fn->Name->Text : std::string("function");
	}

	static std::string ValueCount(int count)
	{
		return std::to_string(count) + (count == 1 ? " value" : " values");
	}

	bool VM::CallClosure(Task* task, Closure* closure, size_t argCount, uint8_t flags)
	{
		Function* fn = closure->Fn;
		if (static_cast<int>(argCount) != fn->Arity)
		{
			return Fail("'" + FunctionLabel(fn) + "' needs " + ValueCount(fn->Arity) + ", but was given " +
				ValueCount(static_cast<int>(argCount)) + ".");
		}
		if (task->Frames.size() >= m_Config.MaxCallDepth)
			return Fail("Too many functions calling each other (is a function calling itself forever?).");

		CallFrame frame;
		frame.Function = closure;
		frame.Ip = 0;
		frame.Base = task->Stack.size() - argCount - 1;
		frame.Flags = flags;
		task->Frames.push_back(frame);
		return true;
	}

	bool VM::CallNative(Task* task, NativeFunction* native, size_t argCount, const Value& self)
	{
		if (native->Arity != AnyArity && static_cast<int>(argCount) != native->Arity)
		{
			return Fail("'" + native->Name + "' needs " + ValueCount(native->Arity) + ", but was given " +
				ValueCount(static_cast<int>(argCount)) + ".");
		}

		size_t base = task->Stack.size() - argCount;
		Value result;
		try
		{
			CallContext context(*this, task, base, argCount, self, native->Name);
			result = native->Fn(context);
		}
		catch (const RuntimeError& error)
		{
			return Fail(error.what());
		}
		catch (const std::exception& error)
		{
			return Fail("Internal error in '" + native->Name + "': " + error.what());
		}

		task->Stack.resize(base - 1);
		task->Stack.push_back(result);
		return true;
	}

	bool VM::CallValue(Task* task, Value callee, size_t argCount)
	{
		if (callee.IsObject())
		{
			switch (callee.AsObject()->GetType())
			{
				case ObjectType::Closure:
					return CallClosure(task, As<Closure>(callee), argCount, FrameNone);
				case ObjectType::NativeFunction:
					return CallNative(task, As<NativeFunction>(callee), argCount, Value());
				case ObjectType::BoundMethod:
				{
					BoundMethod* bound = As<BoundMethod>(callee);
					task->Stack[task->Stack.size() - argCount - 1] = bound->Receiver;
					if (bound->Method->GetType() == ObjectType::Closure)
						return CallClosure(task, static_cast<Closure*>(bound->Method), argCount, FrameNone);
					return CallNative(task, static_cast<NativeFunction*>(bound->Method), argCount, bound->Receiver);
				}
				case ObjectType::Kind:
					return ConstructInstance(task, As<Kind>(callee), argCount);
				default:
					break;
			}
		}
		return Fail("This is " + std::string(callee.IsNothing() ? "nothing" : "a " + TypeOf(callee)) +
			", not a function, so it cannot be called.");
	}

	bool VM::ConstructInstance(Task* task, Kind* kind, size_t argCount)
	{
		Instance* instance = Allocate<Instance>(0, kind);
		size_t slot = task->Stack.size() - argCount - 1;
		task->Stack[slot] = Value::FromObject(instance);

		Closure* setup = kind->FindMethod(m_SetupName);
		if (setup)
		{
			if (!CallClosure(task, setup, argCount, FrameInitializer))
				return false;
		}
		else if (argCount != 0)
		{
			return Fail("The kind '" + kind->Name->Text + "' has no 'setup' function, so 'new " + kind->Name->Text +
				"' cannot be given values.");
		}

		// Field defaults run first, parents before children. Frames run last-pushed first,
		// so push the child's fields first and the oldest parent's fields last.
		for (Kind* current = kind; current; current = current->Parent)
		{
			if (!current->Fields)
				continue;
			task->Stack.push_back(Value::FromObject(instance));
			if (!CallClosure(task, current->Fields, 0, FrameDiscard))
				return false;
		}
		return true;
	}

	NativeFunction* VM::NativeMethod(const NativeType::MethodInfo& info, const std::string& name)
	{
		auto it = m_NativeMethodCache.find(&info);
		if (it != m_NativeMethodCache.end())
			return it->second;
		NativeFunction* native = Allocate<NativeFunction>(name.size(), name, info.Arity, info.Fn);
		m_NativeMethodCache.emplace(&info, native);
		return native;
	}

	bool VM::Invoke(Task* task, String* name, size_t argCount)
	{
		size_t receiverSlot = task->Stack.size() - argCount - 1;
		Value receiver = task->Stack[receiverSlot];

		if (!receiver.IsObject())
		{
			return Fail("Cannot use '" + name->Text + "' on " + (receiver.IsNothing() ? std::string("nothing") : "a " + TypeOf(receiver)) +
				(receiver.IsNothing() ? ". The value has not been set yet." : "."));
		}

		auto callBuiltin = [&](std::unordered_map<String*, NativeFunction*>& table, const char* typeName) -> bool
		{
			auto it = table.find(name);
			if (it == table.end())
				return Fail(std::string("A ") + typeName + " has no function called '" + name->Text + "'.");
			return CallNative(task, it->second, argCount, receiver);
		};

		switch (receiver.AsObject()->GetType())
		{
			case ObjectType::Instance:
			{
				Instance* instance = As<Instance>(receiver);
				auto field = instance->Fields.find(name);
				if (field != instance->Fields.end())
				{
					task->Stack[receiverSlot] = field->second;
					return CallValue(task, field->second, argCount);
				}
				Closure* method = instance->Class->FindMethod(name);
				if (!method)
					return Fail("'" + instance->Class->Name->Text + "' has no function called '" + name->Text + "'.");
				return CallClosure(task, method, argCount, FrameNone);
			}
			case ObjectType::NativeObject:
			{
				NativeObject* object = As<NativeObject>(receiver);
				if (object->Type->IsAlive && !object->Type->IsAlive(object->Data))
					return Fail("This " + object->Type->Name + " no longer exists (it was destroyed).");
				auto method = object->Type->Methods.find(name->Text);
				if (method != object->Type->Methods.end())
					return CallNative(task, NativeMethod(method->second, name->Text), argCount, receiver);
				Value property;
				if (GetProperty(task, receiver, name, property) && IsCallableValue(property))
				{
					task->Stack[receiverSlot] = property;
					return CallValue(task, property, argCount);
				}
				return Fail("A " + object->Type->Name + " has no function called '" + name->Text + "'.");
			}
			case ObjectType::Map:
			{
				Map* map = As<Map>(receiver);
				Value stored;
				if (map->Get(Value::FromObject(name), stored) && IsCallableValue(stored))
				{
					task->Stack[receiverSlot] = stored;
					return CallValue(task, stored, argCount);
				}
				return callBuiltin(MapMethods, "map");
			}
			case ObjectType::List:   return callBuiltin(ListMethods, "list");
			case ObjectType::String: return callBuiltin(TextMethods, "text");
			case ObjectType::Vector: return callBuiltin(VectorMethods, "vector");
			case ObjectType::Module:
			{
				Module* module = As<Module>(receiver);
				auto global = module->Globals.find(name);
				if (global == module->Globals.end())
					return Fail("The script '" + module->Name + "' has nothing called '" + name->Text + "'.");
				task->Stack[receiverSlot] = global->second;
				return CallValue(task, global->second, argCount);
			}
			case ObjectType::NativeFunction:
			{
				NativeFunction* function = As<NativeFunction>(receiver);
				auto member = function->Members.find(name->Text);
				if (member == function->Members.end())
					return Fail("'" + function->Name + "' has nothing called '" + name->Text + "'.");
				task->Stack[receiverSlot] = member->second;
				return CallValue(task, member->second, argCount);
			}
			default:
				break;
		}
		return Fail("Cannot use '" + name->Text + "' on a " + TypeOf(receiver) + ".");
	}

	bool VM::BindMethod(Task* task, const Value& receiver, String* name, Value& outValue)
	{
		(void)task;
		if (IsObjectType(receiver, ObjectType::Instance))
		{
			Closure* method = As<Instance>(receiver)->Class->FindMethod(name);
			if (!method)
				return false;
			outValue = Value::FromObject(Allocate<BoundMethod>(0, receiver, method));
			return true;
		}

		std::unordered_map<String*, NativeFunction*>* table = nullptr;
		switch (receiver.IsObject() ? receiver.AsObject()->GetType() : ObjectType::Upvalue)
		{
			case ObjectType::List:   table = &ListMethods; break;
			case ObjectType::String: table = &TextMethods; break;
			case ObjectType::Map:    table = &MapMethods; break;
			case ObjectType::Vector: table = &VectorMethods; break;
			case ObjectType::NativeObject:
			{
				NativeObject* object = As<NativeObject>(receiver);
				auto method = object->Type->Methods.find(name->Text);
				if (method == object->Type->Methods.end())
					return false;
				outValue = Value::FromObject(Allocate<BoundMethod>(0, receiver, NativeMethod(method->second, name->Text)));
				return true;
			}
			default:
				return false;
		}
		auto it = table->find(name);
		if (it == table->end())
			return false;
		outValue = Value::FromObject(Allocate<BoundMethod>(0, receiver, it->second));
		return true;
	}

	// ======================================================================= Upvalues

	Upvalue* VM::CaptureUpvalue(Task* task, size_t slot)
	{
		Upvalue* previous = nullptr;
		Upvalue* upvalue = task->OpenUpvalues;
		while (upvalue && upvalue->Slot > slot)
		{
			previous = upvalue;
			upvalue = upvalue->NextOpen;
		}
		if (upvalue && upvalue->Slot == slot)
			return upvalue;

		Upvalue* created = Allocate<Upvalue>(0, task, slot);
		created->NextOpen = upvalue;
		if (previous)
			previous->NextOpen = created;
		else
			task->OpenUpvalues = created;
		return created;
	}

	void VM::CloseUpvalues(Task* task, size_t fromSlot)
	{
		while (task->OpenUpvalues && task->OpenUpvalues->Slot >= fromSlot)
		{
			Upvalue* upvalue = task->OpenUpvalues;
			upvalue->Closed = upvalue->Slot < task->Stack.size() ? task->Stack[upvalue->Slot] : Value();
			upvalue->IsClosed = true;
			upvalue->OwnerTask = nullptr;
			task->OpenUpvalues = upvalue->NextOpen;
		}
	}

	// ======================================================================= Properties and indexing

	bool VM::LookupGlobal(Module* module, String* name, Value& outValue)
	{
		if (module)
		{
			auto it = module->Globals.find(name);
			if (it != module->Globals.end())
			{
				outValue = it->second;
				return true;
			}
		}
		auto shared = m_SharedGlobals.find(name);
		if (shared != m_SharedGlobals.end())
		{
			outValue = shared->second;
			return true;
		}
		return false;
	}

	bool VM::GetProperty(Task* task, const Value& object, String* name, Value& outValue)
	{
		const std::string& key = name->Text;
		if (!object.IsObject())
		{
			if (object.IsNothing())
				return Fail("Cannot read '" + key + "' because the value is nothing (it has not been set yet).");
			return Fail("A " + TypeOf(object) + " has no property called '" + key + "'.");
		}

		switch (object.AsObject()->GetType())
		{
			case ObjectType::Instance:
			{
				Instance* instance = As<Instance>(object);
				auto field = instance->Fields.find(name);
				if (field != instance->Fields.end())
				{
					outValue = field->second;
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				return Fail("'" + instance->Class->Name->Text + "' has no field or function called '" + key + "'.");
			}
			case ObjectType::NativeObject:
			{
				NativeObject* native = As<NativeObject>(object);
				if (native->Type->IsAlive && !native->Type->IsAlive(native->Data))
					return Fail("This " + native->Type->Name + " no longer exists (it was destroyed).");
				auto getter = native->Type->Getters.find(key);
				if (getter != native->Type->Getters.end())
				{
					try
					{
						outValue = getter->second(*this, native->Data);
					}
					catch (const RuntimeError& error)
					{
						return Fail(error.what());
					}
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				return Fail("A " + native->Type->Name + " has no property called '" + key + "'.");
			}
			case ObjectType::Map:
			{
				Map* map = As<Map>(object);
				if (map->Get(Value::FromObject(name), outValue))
					return true;
				if (key == "length" || key == "size" || key == "count")
				{
					outValue = Value::FromNumber(static_cast<double>(map->Size()));
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				outValue = Value();
				return true;
			}
			case ObjectType::List:
			{
				List* list = As<List>(object);
				if (key == "length" || key == "size" || key == "count")
				{
					outValue = Value::FromNumber(static_cast<double>(list->Items.size()));
					return true;
				}
				if (key == "first" || key == "last")
				{
					outValue = list->Items.empty() ? Value() : (key == "first" ? list->Items.front() : list->Items.back());
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				return Fail("A list has no property called '" + key + "'.");
			}
			case ObjectType::String:
			{
				String* text = As<String>(object);
				if (key == "length" || key == "size" || key == "count")
				{
					outValue = Value::FromNumber(static_cast<double>(Utf8::Length(text->Text)));
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				return Fail("Text has no property called '" + key + "'.");
			}
			case ObjectType::Vector:
			{
				Vector* vector = As<Vector>(object);
				if (key == "x") { outValue = Value::FromNumber(vector->X); return true; }
				if (key == "y") { outValue = Value::FromNumber(vector->Y); return true; }
				if (key == "z") { outValue = Value::FromNumber(vector->Z); return true; }
				if (key == "length")
				{
					outValue = Value::FromNumber(std::sqrt(vector->X * vector->X + vector->Y * vector->Y + vector->Z * vector->Z));
					return true;
				}
				if (BindMethod(task, object, name, outValue))
					return true;
				return Fail("A vector has no property called '" + key + "'. It has x, y, z and length.");
			}
			case ObjectType::Module:
			{
				Module* module = As<Module>(object);
				auto global = module->Globals.find(name);
				if (global == module->Globals.end())
					return Fail("The script '" + module->Name + "' has nothing called '" + key + "'.");
				outValue = global->second;
				return true;
			}
			case ObjectType::NativeFunction:
			{
				NativeFunction* function = As<NativeFunction>(object);
				auto member = function->Members.find(key);
				if (member == function->Members.end())
					return Fail("'" + function->Name + "' has nothing called '" + key + "'.");
				outValue = member->second;
				return true;
			}
			case ObjectType::Kind:
			{
				Kind* kind = As<Kind>(object);
				if (key == "name")
				{
					outValue = Value::FromObject(kind->Name);
					return true;
				}
				return Fail("The kind '" + kind->Name->Text + "' has no property called '" + key + "'.");
			}
			default:
				break;
		}
		return Fail("A " + TypeOf(object) + " has no property called '" + key + "'.");
	}

	bool VM::SetProperty(Task* task, const Value& object, String* name, const Value& value)
	{
		(void)task;
		const std::string& key = name->Text;
		if (!object.IsObject())
		{
			if (object.IsNothing())
				return Fail("Cannot change '" + key + "' because the value is nothing (it has not been set yet).");
			return Fail("Cannot change '" + key + "' on a " + TypeOf(object) + ".");
		}

		switch (object.AsObject()->GetType())
		{
			case ObjectType::Instance:
				As<Instance>(object)->SetField(name, value);
				return true;
			case ObjectType::Map:
				As<Map>(object)->Set(Value::FromObject(name), value);
				return true;
			case ObjectType::NativeObject:
			{
				NativeObject* native = As<NativeObject>(object);
				if (native->Type->IsAlive && !native->Type->IsAlive(native->Data))
					return Fail("This " + native->Type->Name + " no longer exists (it was destroyed).");
				auto setter = native->Type->Setters.find(key);
				if (setter == native->Type->Setters.end())
				{
					if (native->Type->Getters.count(key))
						return Fail("'" + key + "' of a " + native->Type->Name + " can be read but not changed.");
					return Fail("A " + native->Type->Name + " has no property called '" + key + "'.");
				}
				try
				{
					setter->second(*this, native->Data, value);
				}
				catch (const RuntimeError& error)
				{
					return Fail(error.what());
				}
				return true;
			}
			case ObjectType::Vector:
			{
				if (!value.IsNumber())
					return Fail("The parts of a vector must be numbers, not " + TypeOf(value) + ".");
				Vector* vector = As<Vector>(object);
				if (key == "x") { vector->X = value.AsNumber(); return true; }
				if (key == "y") { vector->Y = value.AsNumber(); return true; }
				if (key == "z") { vector->Z = value.AsNumber(); return true; }
				return Fail("A vector only has x, y and z.");
			}
			case ObjectType::Module:
			{
				Module* module = As<Module>(object);
				if (module->Constants.count(name))
					return Fail("'" + key + "' is a constant and cannot be changed.");
				module->SetGlobal(name, value);
				return true;
			}
			default:
				break;
		}
		return Fail("Cannot change '" + key + "' on a " + TypeOf(object) + ".");
	}

	bool VM::GetIndex(Task* task, const Value& object, const Value& index, Value& outValue)
	{
		std::string error;
		if (IsObjectType(object, ObjectType::List))
		{
			List* list = As<List>(object);
			size_t i = 0;
			if (!ResolvePosition(index, list->Items.size(), i, error))
				return Fail(error);
			outValue = list->Items[i];
			return true;
		}
		if (IsObjectType(object, ObjectType::Map))
		{
			if (!As<Map>(object)->Get(index, outValue))
				outValue = Value();
			return true;
		}
		if (IsObjectType(object, ObjectType::String))
		{
			String* text = As<String>(object);
			size_t i = 0;
			if (!ResolvePosition(index, Utf8::Length(text->Text), i, error))
				return Fail(error);
			outValue = MakeText(Utf8::CharAt(text->Text, i));
			return true;
		}
		if (IsObjectType(index, ObjectType::String))
			return GetProperty(task, object, As<String>(index), outValue);
		if (object.IsNothing())
			return Fail("Cannot look inside nothing (the value has not been set yet).");
		return Fail("Cannot use [ ] on a " + TypeOf(object) + ".");
	}

	bool VM::SetIndex(Task* task, const Value& object, const Value& index, const Value& value)
	{
		std::string error;
		if (IsObjectType(object, ObjectType::List))
		{
			List* list = As<List>(object);
			size_t i = 0;
			if (!ResolvePosition(index, list->Items.size(), i, error))
				return Fail(error + " Use 'add item to list' to grow a list.");
			list->Items[i] = value;
			return true;
		}
		if (IsObjectType(object, ObjectType::Map))
		{
			if (index.IsNothing())
				return Fail("A map key cannot be nothing.");
			As<Map>(object)->Set(index, value);
			return true;
		}
		if (IsObjectType(index, ObjectType::String))
			return SetProperty(task, object, As<String>(index), value);
		if (IsObjectType(object, ObjectType::String))
			return Fail("Text cannot be changed letter by letter. Build new text instead.");
		if (object.IsNothing())
			return Fail("Cannot change an item inside nothing (the value has not been set yet).");
		return Fail("Cannot use [ ] on a " + TypeOf(object) + ".");
	}

	// ======================================================================= Operators

	bool VM::Arithmetic(Task* task, OpCode op, const Value& a, const Value& b, Value& outValue)
	{
		(void)task;
		if (a.IsNumber() && b.IsNumber())
		{
			double x = a.AsNumber();
			double y = b.AsNumber();
			switch (op)
			{
				case OpCode::Add:      outValue = Value::FromNumber(x + y); return true;
				case OpCode::Subtract: outValue = Value::FromNumber(x - y); return true;
				case OpCode::Multiply: outValue = Value::FromNumber(x * y); return true;
				case OpCode::Divide:
					if (y == 0)
						return Fail("Cannot divide by zero.");
					outValue = Value::FromNumber(x / y);
					return true;
				case OpCode::Modulo:
				{
					if (y == 0)
						return Fail("Cannot take the remainder of a division by zero.");
					double r = std::fmod(x, y);
					if (r != 0 && ((r < 0) != (y < 0)))
						r += y;
					outValue = Value::FromNumber(r);
					return true;
				}
				case OpCode::Power: outValue = Value::FromNumber(std::pow(x, y)); return true;
				default: break;
			}
		}

		if (op == OpCode::Add)
		{
			if (IsObjectType(a, ObjectType::String) || IsObjectType(b, ObjectType::String))
			{
				std::string left;
				std::string right;
				if (!SafeToText(a, left) || !SafeToText(b, right))
					return false;
				outValue = MakeText(left + right);
				return true;
			}
			if (IsObjectType(a, ObjectType::List) && IsObjectType(b, ObjectType::List))
			{
				std::vector<Value> items = As<List>(a)->Items;
				const std::vector<Value>& more = As<List>(b)->Items;
				items.insert(items.end(), more.begin(), more.end());
				outValue = MakeList(std::move(items));
				return true;
			}
		}

		bool aVector = IsObjectType(a, ObjectType::Vector);
		bool bVector = IsObjectType(b, ObjectType::Vector);
		if (aVector && bVector && (op == OpCode::Add || op == OpCode::Subtract || op == OpCode::Multiply))
		{
			Vector* u = As<Vector>(a);
			Vector* v = As<Vector>(b);
			if (op == OpCode::Add)
				outValue = MakeVector(u->X + v->X, u->Y + v->Y, u->Z + v->Z);
			else if (op == OpCode::Subtract)
				outValue = MakeVector(u->X - v->X, u->Y - v->Y, u->Z - v->Z);
			else
				outValue = MakeVector(u->X * v->X, u->Y * v->Y, u->Z * v->Z);
			return true;
		}
		if ((aVector && b.IsNumber()) || (a.IsNumber() && bVector))
		{
			Vector* v = aVector ? As<Vector>(a) : As<Vector>(b);
			double s = aVector ? b.AsNumber() : a.AsNumber();
			if (op == OpCode::Multiply)
			{
				outValue = MakeVector(v->X * s, v->Y * s, v->Z * s);
				return true;
			}
			if (op == OpCode::Divide && aVector)
			{
				if (s == 0)
					return Fail("Cannot divide a vector by zero.");
				outValue = MakeVector(v->X / s, v->Y / s, v->Z / s);
				return true;
			}
		}
		if (op == OpCode::Multiply && IsObjectType(a, ObjectType::String) && b.IsNumber())
		{
			double count = b.AsNumber();
			if (count < 0 || count != std::floor(count))
				return Fail("Text can only be repeated a whole number of times.");
			std::string result;
			for (double i = 0; i < count; i++)
				result += As<String>(a)->Text;
			outValue = MakeText(result);
			return true;
		}

		const char* verb = "combine";
		switch (op)
		{
			case OpCode::Add:      verb = "add"; break;
			case OpCode::Subtract: verb = "subtract"; break;
			case OpCode::Multiply: verb = "multiply"; break;
			case OpCode::Divide:   verb = "divide"; break;
			case OpCode::Modulo:   verb = "take the remainder of"; break;
			case OpCode::Power:    verb = "raise"; break;
			default: break;
		}
		auto describe = [this](const Value& v) { return v.IsNothing() ? std::string("nothing") : TypeOf(v); };
		return Fail(std::string("Cannot ") + verb + " " + describe(a) + " and " + describe(b) + ".");
	}

	bool VM::Compare(Task* task, OpCode op, const Value& a, const Value& b, bool& outResult)
	{
		(void)task;
		int order = 0;
		if (a.IsNumber() && b.IsNumber())
		{
			double x = a.AsNumber();
			double y = b.AsNumber();
			order = x < y ? -1 : (x > y ? 1 : 0);
			if (std::isnan(x) || std::isnan(y))
			{
				outResult = false;
				return true;
			}
		}
		else if (IsObjectType(a, ObjectType::String) && IsObjectType(b, ObjectType::String))
		{
			int c = As<String>(a)->Text.compare(As<String>(b)->Text);
			order = c < 0 ? -1 : (c > 0 ? 1 : 0);
		}
		else
		{
			auto describe = [this](const Value& v) { return v.IsNothing() ? std::string("nothing") : TypeOf(v); };
			return Fail("Cannot compare " + describe(a) + " with " + describe(b) + ". Only numbers with numbers, or text with text.");
		}

		switch (op)
		{
			case OpCode::Greater:      outResult = order > 0; break;
			case OpCode::GreaterEqual: outResult = order >= 0; break;
			case OpCode::Less:         outResult = order < 0; break;
			case OpCode::LessEqual:    outResult = order <= 0; break;
			default:                   outResult = false; break;
		}
		return true;
	}

	bool VM::ContainsValue(Task* task, const Value& container, const Value& item, bool& outResult)
	{
		(void)task;
		if (IsObjectType(container, ObjectType::List))
		{
			const std::vector<Value>& items = As<List>(container)->Items;
			outResult = std::any_of(items.begin(), items.end(), [&item](const Value& v) { return ContentsEqual(v, item); });
			return true;
		}
		if (IsObjectType(container, ObjectType::Map))
		{
			outResult = As<Map>(container)->Has(item);
			return true;
		}
		if (IsObjectType(container, ObjectType::String))
		{
			if (!IsObjectType(item, ObjectType::String))
				return Fail("Text can only contain other text.");
			outResult = As<String>(container)->Text.find(As<String>(item)->Text) != std::string::npos;
			return true;
		}
		return Fail("Only lists, maps and text can contain things, not a " + TypeOf(container) + ".");
	}

	bool VM::ResolveModuleSource(const std::string& path, std::string& outName, std::string& outSource)
	{
		auto cached = m_SourceCache.find(path);
		if (cached != m_SourceCache.end())
		{
			outName = cached->second.Name;
			outSource = cached->second.Source;
			return true;
		}
		if (!m_ModuleLoader || !m_ModuleLoader(path, outName, outSource))
			return false;
		m_SourceCache[path] = { outName, outSource };
		return true;
	}

	void VM::ClearModuleCache()
	{
		m_SourceCache.clear();
		m_UsedModules.clear();
	}

	std::vector<std::string> VM::GetGlobalNames(Module* module) const
	{
		std::vector<std::string> names;
		names.reserve(module->GlobalOrder.size());
		for (String* name : module->GlobalOrder)
			names.push_back(name->Text);
		return names;
	}

	std::vector<std::string> VM::CarryOverVariables(Module* from, Module* to, const std::vector<std::string>& skip)
	{
		// Code and imported scripts always come from the new version.
		auto isCode = [](const Value& value)
		{
			return IsObjectType(value, ObjectType::Function) || IsObjectType(value, ObjectType::Closure) ||
				IsObjectType(value, ObjectType::NativeFunction) || IsObjectType(value, ObjectType::Kind) ||
				IsObjectType(value, ObjectType::BoundMethod) || IsObjectType(value, ObjectType::Module);
		};

		GCPause pause(*this);
		std::vector<std::string> carried;
		for (String* name : to->GlobalOrder)
		{
			if (std::find(skip.begin(), skip.end(), name->Text) != skip.end() || to->Constants.count(name))
				continue;
			auto old = from->Globals.find(name);
			if (old == from->Globals.end())
				continue;
			const Value& current = to->Globals[name];
			if (isCode(old->second) || isCode(current) || TypeOf(old->second) != TypeOf(current))
				continue;
			to->SetGlobal(name, old->second);
			carried.push_back(name->Text);
		}
		return carried;
	}

	Module* VM::UseModule(Task* task, const std::string& path)
	{
		(void)task;
		std::string name;
		std::string source;
		if (!ResolveModuleSource(path, name, source))
		{
			Fail("Could not find the script \"" + path + "\" to use.");
			return nullptr;
		}

		auto cached = m_UsedModules.find(name);
		if (cached != m_UsedModules.end())
			return cached->second;

		Module* module = Allocate<Module>(name.size(), name);
		m_UsedModules.emplace(name, module);

		CompileError error;
		Function* fn = nullptr;
		{
			GCPause pause(*this);
			fn = CompileScript(*this, module, source, name, CompileOptions(), error);
		}
		if (!fn)
		{
			m_UsedModules.erase(name);
			Fail(error.ToString());
			return nullptr;
		}

		Task* loader = NewTask(module);
		loader->CanYield = false;
		loader->IsNested = true;
		loader->Stack.push_back(Value::FromObject(Allocate<Closure>(0, fn)));
		m_PendingTasks.push_back(loader);
		StartCall(loader, 0);
		RemovePending(loader);
		module->HasRun = true;

		if (loader->State == TaskState::Failed)
		{
			m_UsedModules.erase(name);
			Fail(loader->FailureMessage);
			return nullptr;
		}
		return module;
	}

	// ======================================================================= Errors

	std::vector<std::string> VM::BuildTrace(Task* task, std::string& outFile, int& outLine)
	{
		std::vector<std::string> trace;
		outFile.clear();
		outLine = 0;
		for (auto it = task->Frames.rbegin(); it != task->Frames.rend(); ++it)
		{
			Function* fn = it->Function->Fn;
			int line = fn->Chunk.GetLine(it->Ip > 0 ? it->Ip - 1 : 0);
			std::string file = fn->Owner ? fn->Owner->Name : std::string("script");
			if (trace.empty())
			{
				outFile = file;
				outLine = line;
			}
			bool isScript = it + 1 == task->Frames.rend() && fn->Arity == 0 && fn->Name && fn->Owner && fn->Name->Text == fn->Owner->Name;
			std::string label = isScript ? std::string("in ") + file : "in '" + FunctionLabel(fn) + "' (" + file;
			label += isScript ? " line " + std::to_string(line) : " line " + std::to_string(line) + ")";
			trace.push_back(label);
		}

		// Collapse long runs of the same line (deep recursion) so the error stays readable.
		std::vector<std::string> collapsed;
		for (size_t i = 0; i < trace.size();)
		{
			size_t run = 1;
			while (i + run < trace.size() && trace[i + run] == trace[i])
				run++;
			collapsed.push_back(trace[i]);
			if (run > 2)
				collapsed.push_back("... the line above repeats " + std::to_string(run - 1) + " more times");
			else if (run == 2)
				collapsed.push_back(trace[i]);
			i += run;
		}
		return collapsed;
	}

	bool VM::SafeToText(const Value& value, std::string& outText)
	{
		try
		{
			outText = ToText(value);
			return true;
		}
		catch (const RuntimeError& error)
		{
			return Fail(error.what());
		}
	}

	void VM::ReportFailure(Task* task, const Value& error)
	{
		std::string message;
		if (IsObjectType(error, ObjectType::String))
			message = As<String>(error)->Text;
		else if (!SafeToText(error, message))
			message = "A " + TypeOf(error) + " was raised as an error.";
		std::string file;
		int line = 0;
		std::vector<std::string> trace = BuildTrace(task, file, line);

		task->State = TaskState::Failed;
		CloseUpvalues(task, 0);
		task->Frames.clear();
		task->Handlers.clear();
		task->Stack.clear();

		if (task->IsNested)
		{
			task->FailureMessage = message;
			if (line > 0)
				task->FailureMessage += " (" + file + " line " + std::to_string(line) + ")";
			return;
		}

		ScriptError report;
		report.Message = message;
		report.File = file;
		report.Line = line;
		report.Trace = std::move(trace);
		task->FailureMessage = report.ToString();
		if (m_ErrorHandler)
			m_ErrorHandler(report);
	}

	bool VM::ThrowValue(Task* task, const Value& error)
	{
		if (!task->Handlers.empty())
		{
			TryHandler handler = task->Handlers.back();
			task->Handlers.pop_back();
			CloseUpvalues(task, handler.StackSize);
			task->Frames.resize(handler.FrameCount);
			task->Stack.resize(handler.StackSize);
			task->Stack.push_back(error);
			task->Frames.back().Ip = handler.CatchIp;
			return true;
		}
		ReportFailure(task, error);
		return false;
	}

	bool VM::RuntimeFailure(Task* task, const std::string& message)
	{
		return ThrowValue(task, MakeText(message));
	}

	// ======================================================================= The interpreter loop

	void VM::Execute(Task* task)
	{
		std::vector<Value>& stack = task->Stack;
		CallFrame* frame = &task->Frames.back();
		const uint8_t* code = frame->Function->Fn->Chunk.GetCode().data();
		const Value* constants = frame->Function->Fn->Chunk.GetConstants().data();
		uint64_t instructions = 0;
		task->State = TaskState::Running;

#define FORGESCRIPT_REFRESH()                                                         \
		do                                                                      \
		{                                                                       \
			frame = &task->Frames.back();                                       \
			code = frame->Function->Fn->Chunk.GetCode().data();                 \
			constants = frame->Function->Fn->Chunk.GetConstants().data();       \
		} while (0)
#define FORGESCRIPT_READ_BYTE() (code[frame->Ip++])
#define FORGESCRIPT_READ_U16() (frame->Ip += 2, static_cast<uint16_t>((code[frame->Ip - 2] << 8) | code[frame->Ip - 1]))
#define FORGESCRIPT_READ_CONSTANT() (constants[FORGESCRIPT_READ_U16()])
#define FORGESCRIPT_READ_STRING() As<String>(FORGESCRIPT_READ_CONSTANT())
#define FORGESCRIPT_FAIL(message)                                                     \
		{                                                                       \
			if (!RuntimeFailure(task, message))                                 \
				return;                                                         \
			FORGESCRIPT_REFRESH();                                                    \
			goto dispatch;                                                      \
		}
#define FORGESCRIPT_CHECK(expression)                                                 \
		if (!(expression))                                                      \
			FORGESCRIPT_FAIL(m_ErrorMessage)

		for (;;)
		{
		dispatch:
			if (m_BytesAllocated > m_NextGC && m_GCPaused == 0)
				CollectGarbage();
			if (m_Config.InstructionLimit && ++instructions > m_Config.InstructionLimit)
			{
				instructions = 0;
				FORGESCRIPT_FAIL("This script ran too long without waiting. Add a 'wait' inside long loops.");
			}

			OpCode op = static_cast<OpCode>(FORGESCRIPT_READ_BYTE());
			switch (op)
			{
				case OpCode::Constant: stack.push_back(FORGESCRIPT_READ_CONSTANT()); break;
				case OpCode::Nothing:  stack.push_back(Value()); break;
				case OpCode::True:     stack.push_back(Value::FromBool(true)); break;
				case OpCode::False:    stack.push_back(Value::FromBool(false)); break;
				case OpCode::Pop:      stack.pop_back(); break;
				case OpCode::Dup:      stack.push_back(stack.back()); break;
				case OpCode::Dup2:
				{
					Value a = stack[stack.size() - 2];
					Value b = stack.back();
					stack.push_back(a);
					stack.push_back(b);
					break;
				}

				case OpCode::GetLocal:
					stack.push_back(stack[frame->Base + FORGESCRIPT_READ_BYTE()]);
					break;
				case OpCode::SetLocal:
					stack[frame->Base + FORGESCRIPT_READ_BYTE()] = stack.back();
					break;
				case OpCode::GetUpvalue:
					stack.push_back(frame->Function->Upvalues[FORGESCRIPT_READ_BYTE()]->Get());
					break;
				case OpCode::SetUpvalue:
					frame->Function->Upvalues[FORGESCRIPT_READ_BYTE()]->Get() = stack.back();
					break;
				case OpCode::CloseUpvalue:
					CloseUpvalues(task, stack.size() - 1);
					stack.pop_back();
					break;

				case OpCode::GetGlobal:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Value value;
					if (!LookupGlobal(frame->Function->Fn->Owner, name, value))
					{
						FORGESCRIPT_FAIL("'" + name->Text + "' has not been given a value yet. Use: set " + name->Text + " to ...");
					}
					stack.push_back(value);
					break;
				}
				case OpCode::SetGlobal:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Module* module = frame->Function->Fn->Owner;
					if (module->Constants.count(name))
						FORGESCRIPT_FAIL("'" + name->Text + "' is a constant and cannot be changed.");
					module->SetGlobal(name, stack.back());
					break;
				}
				case OpCode::DefineConstant:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Module* module = frame->Function->Fn->Owner;
					if (module->Constants.count(name))
						FORGESCRIPT_FAIL("The constant '" + name->Text + "' was already made.");
					module->SetGlobal(name, stack.back());
					module->Constants.insert(name);
					stack.pop_back();
					break;
				}

				case OpCode::GetProperty:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Value result;
					FORGESCRIPT_CHECK(GetProperty(task, stack.back(), name, result));
					stack.back() = result;
					break;
				}
				case OpCode::SetProperty:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Value value = stack.back();
					FORGESCRIPT_CHECK(SetProperty(task, stack[stack.size() - 2], name, value));
					stack.pop_back();
					stack.back() = value;
					break;
				}
				case OpCode::GetIndex:
				{
					Value result;
					FORGESCRIPT_CHECK(GetIndex(task, stack[stack.size() - 2], stack.back(), result));
					stack.pop_back();
					stack.back() = result;
					break;
				}
				case OpCode::SetIndex:
				{
					Value value = stack.back();
					FORGESCRIPT_CHECK(SetIndex(task, stack[stack.size() - 3], stack[stack.size() - 2], value));
					stack.resize(stack.size() - 2);
					stack.back() = value;
					break;
				}
				case OpCode::WriteBackProperty:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Value value = stack.back();
					stack.pop_back();
					if (IsObjectType(value, ObjectType::Vector) && !IsObjectType(stack.back(), ObjectType::Vector))
						FORGESCRIPT_CHECK(SetProperty(task, stack.back(), name, value));
					break;
				}
				case OpCode::WriteBackIndex:
				{
					Value value = stack.back();
					Value index = stack[stack.size() - 2];
					stack.resize(stack.size() - 2);
					if (IsObjectType(value, ObjectType::Vector))
						FORGESCRIPT_CHECK(SetIndex(task, stack.back(), index, value));
					break;
				}

				case OpCode::Equal:
				{
					Value b = stack.back();
					stack.pop_back();
					stack.back() = Value::FromBool(ContentsEqual(stack.back(), b));
					break;
				}
				case OpCode::NotEqual:
				{
					Value b = stack.back();
					stack.pop_back();
					stack.back() = Value::FromBool(!ContentsEqual(stack.back(), b));
					break;
				}
				case OpCode::Greater: case OpCode::GreaterEqual: case OpCode::Less: case OpCode::LessEqual:
				{
					bool result = false;
					FORGESCRIPT_CHECK(Compare(task, op, stack[stack.size() - 2], stack.back(), result));
					stack.pop_back();
					stack.back() = Value::FromBool(result);
					break;
				}
				case OpCode::Add: case OpCode::Subtract: case OpCode::Multiply:
				case OpCode::Divide: case OpCode::Modulo: case OpCode::Power:
				{
					Value result;
					FORGESCRIPT_CHECK(Arithmetic(task, op, stack[stack.size() - 2], stack.back(), result));
					stack.pop_back();
					stack.back() = result;
					break;
				}
				case OpCode::Negate:
				{
					Value value = stack.back();
					if (value.IsNumber())
						stack.back() = Value::FromNumber(-value.AsNumber());
					else if (IsObjectType(value, ObjectType::Vector))
					{
						Vector* v = As<Vector>(value);
						stack.back() = MakeVector(-v->X, -v->Y, -v->Z);
					}
					else
						FORGESCRIPT_FAIL("Only numbers and vectors can be made negative, not " + TypeOf(value) + ".");
					break;
				}
				case OpCode::Not:
					stack.back() = Value::FromBool(!stack.back().IsTruthy());
					break;
				case OpCode::Contains:
				case OpCode::In:
				{
					Value container = op == OpCode::Contains ? stack[stack.size() - 2] : stack.back();
					Value item = op == OpCode::Contains ? stack.back() : stack[stack.size() - 2];
					bool result = false;
					FORGESCRIPT_CHECK(ContainsValue(task, container, item, result));
					stack.pop_back();
					stack.back() = Value::FromBool(result);
					break;
				}
				case OpCode::IsType:
				{
					TypeCheck check = static_cast<TypeCheck>(FORGESCRIPT_READ_BYTE());
					Value value = stack.back();
					bool result = false;
					switch (check)
					{
						case TypeCheck::Number:   result = value.IsNumber(); break;
						case TypeCheck::Text:     result = IsObjectType(value, ObjectType::String); break;
						case TypeCheck::Boolean:  result = value.IsBool(); break;
						case TypeCheck::List:     result = IsObjectType(value, ObjectType::List); break;
						case TypeCheck::Map:      result = IsObjectType(value, ObjectType::Map); break;
						case TypeCheck::Function: result = IsCallableValue(value) && !IsObjectType(value, ObjectType::Kind); break;
						case TypeCheck::Vector:   result = IsObjectType(value, ObjectType::Vector); break;
						case TypeCheck::Nothing:  result = value.IsNothing(); break;
						case TypeCheck::Kind:     result = IsObjectType(value, ObjectType::Kind); break;
						case TypeCheck::Object:   result = IsObjectType(value, ObjectType::Instance) || IsObjectType(value, ObjectType::NativeObject); break;
					}
					stack.back() = Value::FromBool(result);
					break;
				}
				case OpCode::IsKind:
				{
					Value kindValue = stack.back();
					stack.pop_back();
					if (!IsObjectType(kindValue, ObjectType::Kind))
						FORGESCRIPT_FAIL("'is a' needs a kind (like Enemy) or a type (number, text, list, map, function, vector).");
					Kind* kind = As<Kind>(kindValue);
					bool result = false;
					if (IsObjectType(stack.back(), ObjectType::Instance))
					{
						for (Kind* current = As<Instance>(stack.back())->Class; current; current = current->Parent)
						{
							if (current == kind)
							{
								result = true;
								break;
							}
						}
					}
					stack.back() = Value::FromBool(result);
					break;
				}
				case OpCode::IsEmpty:
				{
					Value value = stack.back();
					bool result = false;
					if (value.IsNothing())
						result = true;
					else if (IsObjectType(value, ObjectType::String))
						result = As<String>(value)->Text.empty();
					else if (IsObjectType(value, ObjectType::List))
						result = As<List>(value)->Items.empty();
					else if (IsObjectType(value, ObjectType::Map))
						result = As<Map>(value)->Size() == 0;
					stack.back() = Value::FromBool(result);
					break;
				}

				case OpCode::AddTo:
				case OpCode::TakeFrom:
				{
					Value amount = stack.back();
					Value current = stack[stack.size() - 2];
					Value result;
					if (IsObjectType(current, ObjectType::List))
					{
						std::vector<Value>& items = As<List>(current)->Items;
						if (op == OpCode::AddTo)
							items.push_back(amount);
						else
						{
							auto found = std::find_if(items.begin(), items.end(), [&amount](const Value& v) { return ContentsEqual(v, amount); });
							if (found != items.end())
								items.erase(found);
						}
						result = current;
					}
					else if (current.IsNothing())
					{
						FORGESCRIPT_FAIL(op == OpCode::AddTo
							? std::string("Cannot add to something that has no value yet. Set it first, like: set score to 0")
							: std::string("Cannot take from something that has no value yet. Set it first, like: set health to 100"));
					}
					else if (IsObjectType(current, ObjectType::Map))
					{
						if (op == OpCode::AddTo)
							FORGESCRIPT_FAIL("Use 'set my_map[key] to value' to put things in a map.");
						As<Map>(current)->Remove(amount);
						result = current;
					}
					else
					{
						FORGESCRIPT_CHECK(Arithmetic(task, op == OpCode::AddTo ? OpCode::Add : OpCode::Subtract, current, amount, result));
					}
					stack.pop_back();
					stack.back() = result;
					break;
				}

				case OpCode::Jump:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					frame->Ip += offset;
					break;
				}
				case OpCode::JumpIfFalse:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					if (!stack.back().IsTruthy())
						frame->Ip += offset;
					break;
				}
				case OpCode::JumpIfTrue:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					if (stack.back().IsTruthy())
						frame->Ip += offset;
					break;
				}
				case OpCode::Loop:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					frame->Ip -= offset;
					break;
				}

				case OpCode::ForPrepare:
				{
					size_t slot = frame->Base + FORGESCRIPT_READ_BYTE();
					Value& start = stack[slot];
					Value& end = stack[slot + 1];
					Value& step = stack[slot + 2];
					if (!start.IsNumber() || !end.IsNumber())
						FORGESCRIPT_FAIL("A 'for ... from ... to ...' loop needs numbers, like: for i from 1 to 10");
					if (step.IsNothing())
						step = Value::FromNumber(start.AsNumber() <= end.AsNumber() ? 1.0 : -1.0);
					if (!step.IsNumber() || step.AsNumber() == 0)
						FORGESCRIPT_FAIL("The 'by' step of a for loop must be a number that is not zero.");
					break;
				}
				case OpCode::ForCheck:
				{
					size_t slot = frame->Base + FORGESCRIPT_READ_BYTE();
					uint16_t exit = FORGESCRIPT_READ_U16();
					double counter = stack[slot].AsNumber();
					double end = stack[slot + 1].AsNumber();
					double step = stack[slot + 2].AsNumber();
					bool keepGoing = step > 0 ? counter <= end : counter >= end;
					if (!keepGoing)
						frame->Ip += exit;
					break;
				}
				case OpCode::ForStep:
				{
					size_t slot = frame->Base + FORGESCRIPT_READ_BYTE();
					stack[slot] = Value::FromNumber(stack[slot].AsNumber() + stack[slot + 2].AsNumber());
					break;
				}
				case OpCode::RepeatCheck:
				{
					size_t slot = frame->Base + FORGESCRIPT_READ_BYTE();
					uint16_t exit = FORGESCRIPT_READ_U16();
					if (!stack[slot].IsNumber())
						FORGESCRIPT_FAIL("'repeat ... times' needs a number, like: repeat 3 times");
					double done = stack[slot + 1].AsNumber();
					if (done >= stack[slot].AsNumber())
					{
						frame->Ip += exit;
						break;
					}
					stack[slot + 1] = Value::FromNumber(done + 1);
					break;
				}
				case OpCode::IterNext:
				{
					size_t slot = frame->Base + FORGESCRIPT_READ_BYTE();
					uint16_t exit = FORGESCRIPT_READ_U16();
					Value iterable = stack[slot];
					size_t position = static_cast<size_t>(stack[slot + 1].AsNumber());

					if (IsObjectType(iterable, ObjectType::List))
					{
						List* list = As<List>(iterable);
						if (position >= list->Items.size())
						{
							frame->Ip += exit;
							break;
						}
						stack[slot + 1] = Value::FromNumber(static_cast<double>(position + 1));
						stack.push_back(list->Items[position]);
						stack.push_back(Value::FromNumber(static_cast<double>(position + 1)));
					}
					else if (IsObjectType(iterable, ObjectType::Map))
					{
						Map* map = As<Map>(iterable);
						if (position >= map->Entries.size())
						{
							frame->Ip += exit;
							break;
						}
						stack[slot + 1] = Value::FromNumber(static_cast<double>(position + 1));
						stack.push_back(map->Entries[position].Key);
						stack.push_back(map->Entries[position].Item);
					}
					else if (IsObjectType(iterable, ObjectType::String))
					{
						// For text the position counts bytes, so each step moves one whole character.
						const std::string& text = As<String>(iterable)->Text;
						if (position >= text.size())
						{
							frame->Ip += exit;
							break;
						}
						size_t length = Utf8::CharLength(static_cast<unsigned char>(text[position]));
						stack[slot + 1] = Value::FromNumber(static_cast<double>(position + length));
						stack.push_back(MakeText(text.substr(position, length)));
						stack.push_back(Value::FromNumber(static_cast<double>(Utf8::Length(std::string_view(text).substr(0, position)) + 1)));
					}
					else
					{
						FORGESCRIPT_FAIL("'for each' needs a list, a map or text, but got " +
							std::string(iterable.IsNothing() ? "nothing" : TypeOf(iterable)) + ".");
					}
					break;
				}

				case OpCode::Call:
				{
					uint8_t count = FORGESCRIPT_READ_BYTE();
					FORGESCRIPT_CHECK(CallValue(task, stack[stack.size() - 1 - count], count));
					FORGESCRIPT_REFRESH();
					break;
				}
				case OpCode::Invoke:
				{
					String* name = FORGESCRIPT_READ_STRING();
					uint8_t count = FORGESCRIPT_READ_BYTE();
					FORGESCRIPT_CHECK(Invoke(task, name, count));
					FORGESCRIPT_REFRESH();
					break;
				}
				case OpCode::InvokeParent:
				{
					String* name = FORGESCRIPT_READ_STRING();
					uint8_t count = FORGESCRIPT_READ_BYTE();
					Kind* owner = frame->Function->OwnerKind;
					if (!owner || !owner->Parent)
						FORGESCRIPT_FAIL("'parent' was used, but this kind does not extend another kind.");
					Closure* method = owner->Parent->FindMethod(name);
					if (!method)
						FORGESCRIPT_FAIL("The parent kind '" + owner->Parent->Name->Text + "' has no function called '" + name->Text + "'.");
					FORGESCRIPT_CHECK(CallClosure(task, method, count, FrameNone));
					FORGESCRIPT_REFRESH();
					break;
				}
				case OpCode::GetParent:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Kind* owner = frame->Function->OwnerKind;
					if (!owner || !owner->Parent)
						FORGESCRIPT_FAIL("'parent' was used, but this kind does not extend another kind.");
					Closure* method = owner->Parent->FindMethod(name);
					if (!method)
						FORGESCRIPT_FAIL("The parent kind '" + owner->Parent->Name->Text + "' has no function called '" + name->Text + "'.");
					stack.back() = Value::FromObject(Allocate<BoundMethod>(0, stack.back(), method));
					break;
				}
				case OpCode::Start:
				{
					uint8_t count = FORGESCRIPT_READ_BYTE();
					size_t calleeIndex = stack.size() - 1 - count;
					Task* spawned = NewTask(frame->Function->Fn->Owner);
					spawned->Stack.assign(stack.begin() + static_cast<std::ptrdiff_t>(calleeIndex), stack.end());
					stack.resize(calleeIndex);
					m_PendingTasks.push_back(spawned);
					StartCall(spawned, count);
					RemovePending(spawned);
					FORGESCRIPT_REFRESH();
					break;
				}
				case OpCode::Closure:
				{
					Function* fn = As<Function>(FORGESCRIPT_READ_CONSTANT());
					Closure* closure = Allocate<Closure>(fn->UpvalueCount * sizeof(Upvalue*), fn);
					stack.push_back(Value::FromObject(closure));
					for (int i = 0; i < fn->UpvalueCount; i++)
					{
						uint8_t isLocal = FORGESCRIPT_READ_BYTE();
						uint8_t index = FORGESCRIPT_READ_BYTE();
						closure->Upvalues[static_cast<size_t>(i)] = isLocal ? CaptureUpvalue(task, frame->Base + index) : frame->Function->Upvalues[index];
					}
					break;
				}
				case OpCode::Return:
				{
					Value result = stack.back();
					stack.pop_back();
					CallFrame finished = *frame;
					CloseUpvalues(task, finished.Base);
					task->Frames.pop_back();
					while (!task->Handlers.empty() && task->Handlers.back().FrameCount > task->Frames.size())
						task->Handlers.pop_back();
					if (finished.Flags & FrameInitializer)
						result = stack[finished.Base];
					stack.resize(finished.Base);

					if (task->Frames.empty())
					{
						task->Result = result;
						task->State = TaskState::Done;
						return;
					}
					if (!(finished.Flags & FrameDiscard))
						stack.push_back(result);
					FORGESCRIPT_REFRESH();
					break;
				}

				case OpCode::Kind:
				{
					String* name = FORGESCRIPT_READ_STRING();
					stack.push_back(Value::FromObject(Allocate<Kind>(0, name)));
					break;
				}
				case OpCode::Inherit:
				{
					Value parent = stack.back();
					stack.pop_back();
					if (!IsObjectType(parent, ObjectType::Kind))
						FORGESCRIPT_FAIL("A kind can only extend another kind, not " + std::string(parent.IsNothing() ? "nothing" : TypeOf(parent)) + ".");
					Kind* kind = As<Kind>(stack.back());
					if (As<Kind>(parent) == kind)
						FORGESCRIPT_FAIL("A kind cannot extend itself.");
					kind->Parent = As<Kind>(parent);
					break;
				}
				case OpCode::Method:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Closure* method = As<Closure>(stack.back());
					stack.pop_back();
					Kind* kind = As<Kind>(stack.back());
					method->OwnerKind = kind;
					kind->Methods[name] = method;
					break;
				}
				case OpCode::FieldInit:
				{
					Closure* fields = As<Closure>(stack.back());
					stack.pop_back();
					Kind* kind = As<Kind>(stack.back());
					fields->OwnerKind = kind;
					kind->Fields = fields;
					break;
				}
				case OpCode::New:
				{
					uint8_t count = FORGESCRIPT_READ_BYTE();
					Value kind = stack[stack.size() - 1 - count];
					if (!IsObjectType(kind, ObjectType::Kind))
						FORGESCRIPT_FAIL("'new' needs a kind, like: new Enemy(). This is " + std::string(kind.IsNothing() ? "nothing" : "a " + TypeOf(kind)) + ".");
					FORGESCRIPT_CHECK(ConstructInstance(task, As<Kind>(kind), count));
					FORGESCRIPT_REFRESH();
					break;
				}

				case OpCode::NewList:
					stack.push_back(MakeList());
					break;
				case OpCode::ListAppend:
				{
					Value item = stack.back();
					stack.pop_back();
					As<List>(stack.back())->Items.push_back(item);
					break;
				}
				case OpCode::NewMap:
					stack.push_back(MakeMap());
					break;
				case OpCode::MapSet:
				{
					Value value = stack.back();
					Value key = stack[stack.size() - 2];
					if (key.IsNothing())
						FORGESCRIPT_FAIL("A map key cannot be nothing.");
					As<Map>(stack[stack.size() - 3])->Set(key, value);
					stack.resize(stack.size() - 2);
					break;
				}
				case OpCode::Interpolate:
				{
					uint8_t count = FORGESCRIPT_READ_BYTE();
					std::string text;
					size_t first = stack.size() - count;
					bool ok = true;
					for (size_t i = first; i < stack.size() && ok; i++)
					{
						std::string part;
						ok = SafeToText(stack[i], part);
						text += part;
					}
					FORGESCRIPT_CHECK(ok);
					stack.resize(first);
					stack.push_back(MakeText(text));
					break;
				}

				case OpCode::TryBegin:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					TryHandler handler;
					handler.FrameCount = task->Frames.size();
					handler.StackSize = stack.size();
					handler.CatchIp = frame->Ip + offset;
					task->Handlers.push_back(handler);
					break;
				}
				case OpCode::TryEnd:
					if (!task->Handlers.empty())
						task->Handlers.pop_back();
					break;
				case OpCode::Fail:
				{
					Value error = stack.back();
					stack.pop_back();
					if (!ThrowValue(task, error))
						return;
					FORGESCRIPT_REFRESH();
					break;
				}

				case OpCode::Wait:
				{
					WaitMode mode = static_cast<WaitMode>(FORGESCRIPT_READ_BYTE());
					Value amount = stack.back();
					stack.pop_back();
					if (!amount.IsNumber())
						FORGESCRIPT_FAIL("'wait' needs a number, like: wait 2 seconds");
					if (!task->CanYield)
						FORGESCRIPT_FAIL("'wait' cannot be used here, because this code must finish right away. Use 'start' to run it as its own task.");
					task->Mode = mode;
					if (mode == WaitMode::Seconds)
						task->WakeTime = m_Time + std::max(0.0, amount.AsNumber());
					else
						task->WakeFrame = m_Frame + static_cast<uint64_t>(std::max(1.0, std::floor(amount.AsNumber())));
					task->State = TaskState::Waiting;
					return;
				}
				case OpCode::OnEvent:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Closure* handler = As<Closure>(stack.back());
					stack.pop_back();
					Module* module = frame->Function->Fn->Owner;
					module->Handlers[name].push_back(handler);
					break;
				}
				case OpCode::Trigger:
				{
					String* name = FORGESCRIPT_READ_STRING();
					uint8_t count = FORGESCRIPT_READ_BYTE();
					// Handlers run inside this call, so a handler that triggers its own event (or two that
					// trigger each other) would use up the C++ stack: stop with an error the script can catch.
					if (m_RunningTasks.size() >= m_Config.MaxNestedCalls)
					{
						FORGESCRIPT_FAIL("The event '" + name->Text + "' was triggered from inside " + std::to_string(m_Config.MaxNestedCalls) +
							" other events; when handlers that trigger each other need a way to stop.");
					}
					std::vector<Value> values(stack.end() - count, stack.end());
					Module* module = frame->Function->Fn->Owner;
					// The values stay on this task's stack while the event runs, which keeps them alive.
					if (m_TriggerHandler)
					{
						// A host handler reports problems (like events nested too deep) as RuntimeError: they
						// become an error on this 'trigger' line, which scripts can catch.
						std::string failure;
						try
						{
							m_TriggerHandler(module, name->Text, values);
						}
						catch (const RuntimeError& error)
						{
							failure = error.what();
						}
						if (!failure.empty())
							FORGESCRIPT_FAIL(failure);
					}
					else
					{
						Emit(module, name->Text, values);
					}
					stack.resize(stack.size() - count);
					FORGESCRIPT_REFRESH();
					break;
				}
				case OpCode::Use:
				{
					String* path = FORGESCRIPT_READ_STRING();
					uint8_t bindAll = FORGESCRIPT_READ_BYTE();
					Module* used = UseModule(task, path->Text);
					if (!used)
						FORGESCRIPT_FAIL(m_ErrorMessage);
					if (bindAll)
					{
						Module* module = frame->Function->Fn->Owner;
						for (String* global : used->GlobalOrder)
						{
							if (global == m_SelfName)
								continue;
							module->SetGlobal(global, used->Globals[global]);
						}
					}
					stack.push_back(Value::FromObject(used));
					break;
				}
			}
		}

#undef FORGESCRIPT_REFRESH
#undef FORGESCRIPT_READ_BYTE
#undef FORGESCRIPT_READ_U16
#undef FORGESCRIPT_READ_CONSTANT
#undef FORGESCRIPT_READ_STRING
#undef FORGESCRIPT_FAIL
#undef FORGESCRIPT_CHECK
	}

	// ======================================================================= Garbage collector

	void VM::MarkValue(const Value& value)
	{
		if (value.IsObject())
			MarkObject(value.AsObject());
	}

	void VM::MarkObject(Object* object)
	{
		if (!object || object->m_Marked)
			return;
		object->m_Marked = true;
		m_GrayStack.push_back(object);
	}

	void VM::MarkRoots()
	{
		for (Module* module : m_Modules)
			MarkObject(module);
		for (auto& [name, module] : m_UsedModules)
			MarkObject(module);
		for (auto& [name, value] : m_SharedGlobals)
		{
			MarkObject(name);
			MarkValue(value);
		}
		auto markTable = [this](std::unordered_map<String*, NativeFunction*>& table)
		{
			for (auto& [name, fn] : table)
			{
				MarkObject(name);
				MarkObject(fn);
			}
		};
		markTable(ListMethods);
		markTable(TextMethods);
		markTable(MapMethods);
		markTable(VectorMethods);
		for (auto& [info, fn] : m_NativeMethodCache)
			MarkObject(fn);
		for (Task* task : m_Tasks)
			MarkObject(task);
		for (Task* task : m_RunningTasks)
			MarkObject(task);
		for (Task* task : m_PendingTasks)
			MarkObject(task);
		for (auto& [object, count] : m_Pinned)
			MarkObject(object);
		MarkObject(m_SetupName);
		MarkObject(m_SelfName);
	}

	void VM::BlackenObject(Object* object)
	{
		switch (object->GetType())
		{
			case ObjectType::String:
			case ObjectType::Vector:
			case ObjectType::NativeObject:
				break;
			case ObjectType::NativeFunction:
				for (const auto& [name, member] : static_cast<NativeFunction*>(object)->Members)
					MarkValue(member);
				break;
			case ObjectType::List:
				for (const Value& item : static_cast<List*>(object)->Items)
					MarkValue(item);
				break;
			case ObjectType::Map:
				for (const Map::Entry& entry : static_cast<Map*>(object)->Entries)
				{
					MarkValue(entry.Key);
					MarkValue(entry.Item);
				}
				break;
			case ObjectType::Function:
			{
				Function* fn = static_cast<Function*>(object);
				MarkObject(fn->Name);
				MarkObject(fn->Owner);
				for (const Value& constant : fn->Chunk.GetConstants())
					MarkValue(constant);
				break;
			}
			case ObjectType::Closure:
			{
				Closure* closure = static_cast<Closure*>(object);
				MarkObject(closure->Fn);
				MarkObject(closure->OwnerKind);
				for (Upvalue* upvalue : closure->Upvalues)
					MarkObject(upvalue);
				break;
			}
			case ObjectType::Upvalue:
			{
				Upvalue* upvalue = static_cast<Upvalue*>(object);
				MarkValue(upvalue->Closed);
				MarkObject(upvalue->OwnerTask);
				break;
			}
			case ObjectType::Kind:
			{
				Kind* kind = static_cast<Kind*>(object);
				MarkObject(kind->Name);
				MarkObject(kind->Parent);
				MarkObject(kind->Fields);
				for (auto& [name, method] : kind->Methods)
				{
					MarkObject(name);
					MarkObject(method);
				}
				break;
			}
			case ObjectType::Instance:
			{
				Instance* instance = static_cast<Instance*>(object);
				MarkObject(instance->Class);
				for (auto& [name, value] : instance->Fields)
				{
					MarkObject(name);
					MarkValue(value);
				}
				break;
			}
			case ObjectType::BoundMethod:
			{
				BoundMethod* bound = static_cast<BoundMethod*>(object);
				MarkValue(bound->Receiver);
				MarkObject(bound->Method);
				break;
			}
			case ObjectType::Module:
			{
				Module* module = static_cast<Module*>(object);
				for (auto& [name, value] : module->Globals)
				{
					MarkObject(name);
					MarkValue(value);
				}
				for (auto& [name, handlers] : module->Handlers)
				{
					MarkObject(name);
					for (Closure* handler : handlers)
						MarkObject(handler);
				}
				break;
			}
			case ObjectType::Task:
			{
				Task* task = static_cast<Task*>(object);
				for (const Value& value : task->Stack)
					MarkValue(value);
				for (const CallFrame& frame : task->Frames)
					MarkObject(frame.Function);
				for (Upvalue* upvalue = task->OpenUpvalues; upvalue; upvalue = upvalue->NextOpen)
					MarkObject(upvalue);
				MarkValue(task->Result);
				MarkObject(task->Owner);
				break;
			}
		}
	}

	void VM::TraceReferences()
	{
		while (!m_GrayStack.empty())
		{
			Object* object = m_GrayStack.back();
			m_GrayStack.pop_back();
			BlackenObject(object);
		}
	}

	void VM::Sweep()
	{
		// Text is interned weakly: forget texts that nothing uses any more.
		for (auto it = m_Strings.begin(); it != m_Strings.end();)
		{
			if (!it->second->m_Marked)
				it = m_Strings.erase(it);
			else
				++it;
		}

		Object** link = &m_Objects;
		while (*link)
		{
			Object* object = *link;
			if (object->m_Marked)
			{
				object->m_Marked = false;
				link = &object->m_Next;
				continue;
			}
			*link = object->m_Next;
			m_BytesAllocated -= std::min<size_t>(m_BytesAllocated, object->m_AllocatedSize);
			m_ObjectCount--;
			delete object;
		}
	}

	void VM::CollectGarbage()
	{
		if (m_GCPaused > 0)
			return;
		MarkRoots();
		TraceReferences();
		Sweep();
		m_NextGC = std::max(m_Config.InitialGCThreshold, m_BytesAllocated * 2);
	}

	void VM::MaybeCollect()
	{
		if (m_BytesAllocated > m_NextGC)
			CollectGarbage();
	}

}
