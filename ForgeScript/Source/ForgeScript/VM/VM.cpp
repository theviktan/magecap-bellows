#include "ForgeScript/VM/VM.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Core/Suggest.h"
#include "ForgeScript/Core/Utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>

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

		// The names in a table, as candidates for a "Did you mean ...?" hint (see Core/Suggest.h).
		std::string_view NameText(const String* name) { return name->Text; }
		std::string_view NameText(const std::string& name) { return name; }

		template<typename Table>
		void AddNames(std::vector<std::string_view>& names, const Table& table)
		{
			for (const auto& entry : table)
				names.push_back(NameText(entry.first));
		}

		// Compares names the way people type them: "start" matches "Start". Letters outside A-Z are
		// compared exactly.
		bool SameTextIgnoringCase(std::string_view a, std::string_view b)
		{
			auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
		}

		// A kind's own functions and those it inherits.
		void AddKindMethods(std::vector<std::string_view>& names, const Kind* kind)
		{
			for (; kind; kind = kind->Parent)
				AddNames(names, kind->Methods);
		}

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
		// Past about 9.2 quintillion (and at infinity) the number has no int64_t, and converting it
		// anyway is undefined behavior in C++.
		if (std::fabs(number) >= 9.2e18)
			throw RuntimeError(m_Name + " needs a whole number between -9.2e18 and 9.2e18 for value " + std::to_string(index + 1) +
				", but got " + FormatNumber(number) + ".");
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

	bool VM::RegisterPhrase(const std::string& pattern, NativeFn fn, std::string* outError, bool yieldToCalls)
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
		if (yieldToCalls)
			m_YieldingPhraseWords.insert(parsed.Parts.front());
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

	bool VM::HasSharedGlobal(const std::string& name) const
	{
		return std::any_of(m_SharedGlobals.begin(), m_SharedGlobals.end(), [&name](const auto& entry) { return entry.first->Text == name; });
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
		String* name = Intern(event);
		auto it = module->Handlers.find(name);
		if (it != module->Handlers.end() && !it->second.empty())
			return true;
		auto waits = module->EventWaits.find(name);
		return waits != module->EventWaits.end() && waits->second > 0;
	}

	void VM::EndEventWait(Task* task)
	{
		if (task->Mode != WaitMode::Event || !task->WaitEvent || !task->Owner)
			return;
		auto it = task->Owner->EventWaits.find(task->WaitEvent);
		if (it != task->Owner->EventWaits.end() && --it->second <= 0)
			task->Owner->EventWaits.erase(it);
		task->WaitEvent = nullptr;
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

	std::vector<NameUse> VM::FindNameUses(std::string_view source, const std::string& fileName, std::vector<CompileError>* outMistakes)
	{
		GCPause pause(*this);
		Module* scratch = Allocate<Module>(0, fileName);
		std::vector<NameUse> uses;
		CompileOptions options;
		options.ReportMissingScripts = true;
		options.NameUses = &uses;
		std::vector<CompileError> mistakes = FindCompileErrors(*this, scratch, source, fileName, options);
		if (outMistakes)
			*outMistakes = std::move(mistakes);
		std::sort(uses.begin(), uses.end(), [](const NameUse& a, const NameUse& b) { return a.Token < b.Token; });
		return uses;
	}

	std::vector<CompileError> VM::FindWarnings(std::string_view source, const std::string& fileName,
		const std::vector<std::string>& moreBuiltinFunctions)
	{
		GCPause pause(*this);
		Module* scratch = Allocate<Module>(0, fileName);
		std::vector<CompileError> warnings;
		CompileOptions options;
		options.ReportMissingScripts = true;
		options.Warnings = &warnings;
		options.BuiltinFunctions = &moreBuiltinFunctions;
		FindCompileErrors(*this, scratch, source, fileName, options);
		// Only this script's own: a script it uses is checked on its own.
		warnings.erase(std::remove_if(warnings.begin(), warnings.end(), [&fileName](const CompileError& warning) { return warning.File != fileName; }),
			warnings.end());
		std::stable_sort(warnings.begin(), warnings.end(), [](const CompileError& a, const CompileError& b)
		{
			return a.Line != b.Line ? a.Line < b.Line : a.Column < b.Column;
		});
		return warnings;
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
				report.Frames.push_back({ error.File.empty() ? std::string("script") : error.File, error.Line, std::string(), 1 });
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

	bool VM::Emit(Module* module, const std::string& event, const std::vector<Value>& values, const std::string& source)
	{
		String* name = Intern(event);
		auto fits = [&source](const std::string& wanted) { return wanted.empty() || (!source.empty() && SameTextIgnoringCase(wanted, source)); };

		// The tasks waiting for this event are found before any handler runs, so a handler (or a woken task)
		// that starts waiting for the same event waits for the next one.
		std::vector<Task*> waiting;
		if (auto waits = module->EventWaits.find(name); waits != module->EventWaits.end() && waits->second > 0)
		{
			for (Task* task : m_Tasks)
			{
				if (task->Owner == module && task->State == TaskState::Waiting && task->Mode == WaitMode::Event &&
					task->WaitEvent == name && fits(task->WaitSource))
				{
					waiting.push_back(task);
				}
			}
		}

		auto it = module->Handlers.find(name);
		if ((it == module->Handlers.end() || it->second.empty()) && waiting.empty())
			return true;

		// Create every task before running any, so all of them hold the values safely.
		std::vector<Closure*> handlers;
		if (it != module->Handlers.end())
		{
			for (Closure* handler : it->second)
			{
				if (fits(handler->Fn->EventSource))
					handlers.push_back(handler);
			}
		}
		std::vector<Task*> tasks;
		const size_t depth = m_RunningTasks.empty() ? 0 : m_RunningTasks.back()->EventDepth + 1;
		for (Closure* handler : handlers)
		{
			Task* task = NewTask(module);
			task->EventDepth = depth;
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

		// Then the waiting tasks, unless a handler ended them (cancelled, or the module unloaded).
		for (Task* task : waiting)
		{
			auto found = std::find(m_Tasks.begin(), m_Tasks.end(), task);
			if (found == m_Tasks.end() || task->State != TaskState::Waiting || task->Mode != WaitMode::Event || task->WaitEvent != name)
				continue;
			m_PendingTasks.push_back(task);
			m_Tasks.erase(found);
			EndEventWait(task);
			for (size_t i = 0; i < task->WaitCount; i++)
				task->Stack.push_back(i < values.size() ? values[i] : Value());
			task->EventDepth = depth;
			RunTask(task);
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

	std::vector<std::string> VM::GetTestNames(Module* module) const
	{
		std::vector<std::string> names;
		if (module)
		{
			for (const auto& [name, test] : module->Tests)
				names.push_back(name->Text);
		}
		return names;
	}

	bool VM::RunTest(Module* module, size_t index, std::string& outFailure, double step, double maxSeconds)
	{
		if (!module || index >= module->Tests.size())
		{
			outFailure = "There is no test " + std::to_string(index + 1) + " in this script.";
			return false;
		}
		Task* task = NewTask(module);
		// The task is kept while it waits between updates, and read after it ends.
		Handle keep(*this, Value::FromObject(task));
		task->Stack.push_back(Value::FromObject(module->Tests[index].second));
		m_PendingTasks.push_back(task);
		StartCall(task, 0);
		RemovePending(task);

		const double start = m_Time;
		auto running = [task] { return task->State == TaskState::Ready || task->State == TaskState::Waiting || task->State == TaskState::Running; };
		while (running() && m_Time - start < maxSeconds)
			Update(step);

		switch (task->State)
		{
			case TaskState::Done:
				return true;
			case TaskState::Failed:
				outFailure = task->FailureMessage;
				return false;
			case TaskState::Cancelled:
				outFailure = "The test was stopped before it finished.";
				return false;
			default:
				outFailure = "The test was still waiting after " + Describe(Value::FromNumber(maxSeconds), false) + " seconds of game time.";
				return false;
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

		// A task that finished one of these calls is used again for the next, so map, filter, sort_by
		// and the like don't make a new task (and its stack) for every item, which also kept the garbage
		// collector running.
		Task* task = nullptr;
		if (!m_SpareTasks.empty())
		{
			task = m_SpareTasks.back();
			m_SpareTasks.pop_back();
			task->Owner = owner;
		}
		else
			task = NewTask(owner);
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
		const Value result = task->Result;
		// Only a task that ended cleanly, holding nothing, is kept: one that failed or stopped in the
		// debugger may still be looked at.
		if (task->State == TaskState::Done && !task->OpenUpvalues && task->Frames.empty() && m_SpareTasks.size() < c_MaxSpareTasks)
		{
			task->Stack.clear();
			task->Handlers.clear();
			task->PendingNames = nullptr;
			task->State = TaskState::Ready;
			task->EventDepth = 0;
			task->ChangeInstance = nullptr;
			task->ChangeField = nullptr;
			task->Step = StepMode::None;
			task->StepDepth = 0;
			task->Result = Value();
			task->Owner = nullptr;
			m_SpareTasks.push_back(task);
		}
		return result;
	}

	void VM::Update(double deltaSeconds)
	{
		// The debugger holds a task: everything waits with it, and time stands still.
		if (m_StoppedTask)
			return;
		if (deltaSeconds > 0)
			m_Time += deltaSeconds;
		m_Frame++;
		ResumeFinishedWaits(nullptr);
	}

	void VM::UpdatePaused(double deltaSeconds, const std::function<bool(const Module*)>& keepsRunning)
	{
		if (m_StoppedTask)
			return;
		for (Task* task : m_Tasks)
		{
			if (task->State != TaskState::Waiting || task->Mode == WaitMode::Event || !keepsRunning(task->Owner))
				continue;
			if (task->Mode == WaitMode::Seconds)
				task->WakeTime -= std::max(0.0, deltaSeconds);
			else if (task->WakeFrame > 0)
				task->WakeFrame--;
		}
		ResumeFinishedWaits(&keepsRunning);
	}

	void VM::ResumeFinishedWaits(const std::function<bool(const Module*)>* only)
	{
		std::vector<Task*> ready;
		std::vector<Task*> keep;
		for (Task* task : m_Tasks)
		{
			if (task->State != TaskState::Waiting)
				continue;
			// A 'wait for' ends only when its event comes (in Emit).
			const bool due = task->Mode == WaitMode::Seconds ? m_Time >= task->WakeTime :
				task->Mode == WaitMode::Frames && m_Frame >= task->WakeFrame;
			const bool wake = due && (!only || (*only)(task->Owner));
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
		auto cancel = [this](Task* task)
		{
			EndEventWait(task);
			task->State = TaskState::Cancelled;
			CloseUpvalues(task, 0);
			ReleaseStep(task);
		};
		for (Task* task : m_Tasks)
		{
			if (!module || task->Owner == module)
				cancel(task);
		}
		m_Tasks.erase(std::remove_if(m_Tasks.begin(), m_Tasks.end(),
			[](Task* task) { return task->State == TaskState::Cancelled; }), m_Tasks.end());

		// Stopped tasks of the module go too; the next waiting stop, if any, takes the place of the current one.
		for (auto& [task, stop] : m_QueuedStops)
		{
			if (!module || task->Owner == module)
				cancel(task);
		}
		m_QueuedStops.erase(std::remove_if(m_QueuedStops.begin(), m_QueuedStops.end(),
			[](const std::pair<Task*, DebugStop>& queued) { return queued.first->State == TaskState::Cancelled; }), m_QueuedStops.end());
		if (m_StoppedTask && (!module || m_StoppedTask->Owner == module))
		{
			cancel(m_StoppedTask);
			m_StoppedTask = nullptr;
			if (!m_QueuedStops.empty())
			{
				m_StoppedTask = m_QueuedStops.front().first;
				m_Stop = m_QueuedStops.front().second;
				m_QueuedStops.erase(m_QueuedStops.begin());
				if (!m_RunningStops)
					RunStops();
			}
		}
	}

	// ======================================================================= Debugger

	static std::string FunctionLabel(Function* fn)
	{
		return fn->Name ? fn->Name->Text : std::string("function");
	}


	void VM::SetBreakpoints(const std::string& file, const std::vector<int>& lines)
	{
		std::vector<int> sorted;
		for (int line : lines)
		{
			if (line > 0)
				sorted.push_back(line);
		}
		std::sort(sorted.begin(), sorted.end());
		sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
		if (sorted.empty())
			m_Breakpoints.erase(file);
		else
			m_Breakpoints[file] = std::move(sorted);
		RefreshDebugActive();
	}

	void VM::ClearBreakpoints()
	{
		m_Breakpoints.clear();
		RefreshDebugActive();
	}

	void VM::Pause()
	{
		m_PauseRequested = true;
		RefreshDebugActive();
	}

	void VM::StopDebugging()
	{
		m_Breakpoints.clear();
		m_PauseRequested = false;
		for (Task* task : m_Tasks)
			ReleaseStep(task);
		auto cancel = [this](Task* task)
		{
			EndEventWait(task);
			task->State = TaskState::Cancelled;
			CloseUpvalues(task, 0);
			ReleaseStep(task);
		};
		for (auto& [task, stop] : m_QueuedStops)
			cancel(task);
		m_QueuedStops.clear();
		if (m_StoppedTask)
		{
			cancel(m_StoppedTask);
			m_StoppedTask = nullptr;
		}
		m_ResumeRequested = false;
		m_ResumeStep = StepMode::None;
		RefreshDebugActive();
	}

	bool VM::Continue() { return Resume(StepMode::None); }
	bool VM::StepOver() { return Resume(StepMode::Over); }
	bool VM::StepIn() { return Resume(StepMode::In); }
	bool VM::StepOut() { return Resume(StepMode::Out); }

	void VM::RefreshDebugActive()
	{
		m_DebugActive = !m_Breakpoints.empty() || m_PauseRequested || m_SteppingTasks > 0;
	}

	void VM::ReleaseStep(Task* task)
	{
		if (task->Step == StepMode::None)
			return;
		task->Step = StepMode::None;
		m_SteppingTasks--;
		RefreshDebugActive();
	}

	bool VM::DebugLineReached(Task* task, CallFrame* frame)
	{
		// A line is reached when a frame moves to a new line, so a line that calls a function is not
		// reached again when the call returns.
		Function* fn = frame->Function->Fn;
		if (frame->Ip < fn->BodyIp)
			return false;
		const int line = fn->Chunk.GetLine(frame->Ip);
		if (line <= 0 || line == frame->DebugLine)
			return false;
		frame->DebugLine = line;
		// Code that must finish right away (called back by native code or by VM::Call) cannot be held.
		if (!task->CanYield || task->IsNested || m_InDebugHandler)
			return false;

		const size_t depth = task->Frames.size();
		bool stop = false;
		DebugStopReason reason = DebugStopReason::Step;
		switch (task->Step)
		{
			case StepMode::None: break;
			case StepMode::In:   stop = true; break;
			case StepMode::Over: stop = depth <= task->StepDepth; break;
			case StepMode::Out:  stop = depth < task->StepDepth; break;
		}
		if (!stop && m_PauseRequested)
		{
			stop = true;
			reason = DebugStopReason::Pause;
		}
		const std::string& file = fn->Owner ? fn->Owner->Name : std::string();
		auto breakpoints = m_Breakpoints.find(file);
		if (breakpoints != m_Breakpoints.end() && std::binary_search(breakpoints->second.begin(), breakpoints->second.end(), line))
		{
			stop = true;
			reason = DebugStopReason::Breakpoint;
		}
		if (!stop)
			return false;

		m_PauseRequested = false;
		ReleaseStep(task);
		m_PendingStop.Reason = reason;
		m_PendingStop.File = file;
		m_PendingStop.Line = line;
		return true;
	}

	void VM::TaskStopped(Task* task)
	{
		if (m_StoppedTask)
		{
			m_QueuedStops.push_back({ task, m_PendingStop });
			return;
		}
		m_StoppedTask = task;
		m_Stop = m_PendingStop;
		if (!m_RunningStops)
			RunStops();
	}

	bool VM::Resume(StepMode step)
	{
		if (!m_StoppedTask)
			return false;
		m_ResumeRequested = true;
		m_ResumeStep = step;
		// Inside the handler (or while a resumed task runs) the loop below picks the request up.
		if (!m_RunningStops)
			RunStops();
		return true;
	}

	void VM::RunStops()
	{
		// One loop tells the handler about each stop and runs each resumed task, so a handler that
		// keeps stepping does not grow the C++ stack.
		struct Running
		{
			bool& Flag;
			explicit Running(bool& flag) : Flag(flag) { Flag = true; }
			~Running() { Flag = false; }
		} running(m_RunningStops);

		while (m_StoppedTask)
		{
			if (!m_ResumeRequested)
			{
				if (m_DebugHandler)
				{
					Running inHandler(m_InDebugHandler);
					m_DebugHandler(m_Stop);
				}
				if (!m_ResumeRequested)
					return;
			}

			Task* task = m_StoppedTask;
			m_StoppedTask = nullptr;
			const StepMode step = m_ResumeStep;
			m_ResumeRequested = false;
			m_ResumeStep = StepMode::None;
			if (step != StepMode::None)
			{
				task->Step = step;
				task->StepDepth = task->Frames.size();
				m_SteppingTasks++;
				RefreshDebugActive();
			}
			RunTask(task);

			if (!m_StoppedTask && !m_QueuedStops.empty())
			{
				m_StoppedTask = m_QueuedStops.front().first;
				m_Stop = m_QueuedStops.front().second;
				m_QueuedStops.erase(m_QueuedStops.begin());
			}
		}
	}

	std::vector<DebugFrame> VM::GetCallStack() const
	{
		std::vector<DebugFrame> frames;
		if (!m_StoppedTask)
			return frames;
		const std::vector<CallFrame>& calls = m_StoppedTask->Frames;
		for (size_t i = calls.size(); i-- > 0;)
		{
			const CallFrame& call = calls[i];
			Function* fn = call.Function->Fn;
			DebugFrame frame;
			// The innermost frame is about to run its instruction; the others are inside a call.
			const bool innermost = i + 1 == calls.size();
			frame.Line = fn->Chunk.GetLine(innermost || call.Ip == 0 ? call.Ip : call.Ip - 1);
			frame.File = fn->Owner ? fn->Owner->Name : std::string("script");
			frame.Owner = fn->Owner;
			const bool topLevel = i == 0 && fn->Arity == 0 && fn->Name && fn->Owner && fn->Name->Text == fn->Owner->Name;
			if (!topLevel)
				frame.Function = FunctionLabel(fn);
			frames.push_back(std::move(frame));
		}
		return frames;
	}

	DebugVariable VM::DescribeForDebugger(const std::string& name, const Value& value)
	{
		DebugVariable variable;
		variable.Name = name;
		variable.Type = TypeOf(value);
		struct PlainText
		{
			bool& Flag;
			explicit PlainText(bool& flag) : Flag(flag) { Flag = true; }
			~PlainText() { Flag = false; }
		} plain(m_PlainText);
		try
		{
			variable.Text = Describe(value, true);
		}
		catch (const std::exception&)
		{
			// A host type's text function failed; the debugger still shows the rest.
			variable.Text = "<" + variable.Type + ">";
		}
		constexpr size_t c_MaxText = 500;
		if (variable.Text.size() > c_MaxText)
		{
			size_t cut = c_MaxText;
			while (cut > 0 && (static_cast<unsigned char>(variable.Text[cut]) & 0xC0) == 0x80)
				cut--;
			variable.Text = variable.Text.substr(0, cut) + "...";
		}
		return variable;
	}

	std::vector<DebugVariable> VM::GetVariables(size_t frameIndex)
	{
		std::vector<DebugVariable> variables;
		if (!m_StoppedTask || frameIndex >= m_StoppedTask->Frames.size())
			return variables;
		GCPause pause(*this);
		const CallFrame& frame = m_StoppedTask->Frames[m_StoppedTask->Frames.size() - 1 - frameIndex];
		const std::vector<Value>& stack = m_StoppedTask->Stack;
		Function* fn = frame.Function->Fn;

		// The names in use at this point; an inner variable with the same name as an outer one wins.
		std::vector<const LocalName*> visible;
		for (const LocalName& local : fn->LocalNames)
		{
			if (local.StartIp <= frame.Ip && frame.Ip < local.EndIp && frame.Base + local.Slot < stack.size())
				visible.push_back(&local);
		}
		std::sort(visible.begin(), visible.end(), [](const LocalName* a, const LocalName* b) { return a->Slot < b->Slot; });
		std::vector<std::string> names;
		for (size_t i = 0; i < visible.size(); i++)
		{
			bool shadowed = false;
			for (size_t j = i + 1; j < visible.size(); j++)
				shadowed = shadowed || visible[j]->Name == visible[i]->Name;
			if (shadowed)
				continue;
			variables.push_back(DescribeForDebugger(visible[i]->Name, stack[frame.Base + visible[i]->Slot]));
			names.push_back(visible[i]->Name);
		}
		for (size_t i = 0; i < fn->UpvalueNames.size() && i < frame.Function->Upvalues.size(); i++)
		{
			Upvalue* upvalue = frame.Function->Upvalues[i];
			const std::string& name = fn->UpvalueNames[i];
			if (!upvalue || std::find(names.begin(), names.end(), name) != names.end())
				continue;
			variables.push_back(DescribeForDebugger(name, upvalue->Get()));
			names.push_back(name);
		}
		return variables;
	}

	std::vector<DebugVariable> VM::GetGlobals(Module* module)
	{
		std::vector<DebugVariable> variables;
		if (!module)
			return variables;
		GCPause pause(*this);
		for (String* name : module->GlobalOrder)
		{
			auto it = module->Globals.find(name);
			if (it == module->Globals.end())
				continue;
			const Value& value = it->second;
			if (IsObjectType(value, ObjectType::Closure) || IsObjectType(value, ObjectType::NativeFunction) ||
				IsObjectType(value, ObjectType::BoundMethod) || IsObjectType(value, ObjectType::Kind) ||
				IsObjectType(value, ObjectType::Module) || IsObjectType(value, ObjectType::Function))
				continue;
			variables.push_back(DescribeForDebugger(name->Text, value));
		}
		return variables;
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
		else if (task->State == TaskState::Stopped)
		{
			TaskStopped(task);
		}
		else if (task->State == TaskState::Done || task->State == TaskState::Failed)
		{
			ReleaseStep(task);
		}
	}

	// ======================================================================= Calls

	bool VM::Fail(const std::string& message)
	{
		m_ErrorMessage = message;
		return false;
	}

	static std::string ValueCount(int count)
	{
		return std::to_string(count) + (count == 1 ? " value" : " values");
	}

	bool VM::CallClosure(Task* task, Closure* closure, size_t argCount, uint8_t flags)
	{
		Function* fn = closure->Fn;
		List* names = std::exchange(task->PendingNames, nullptr);
		int given = static_cast<int>(argCount);
		const int fixed = fn->HasRest ? fn->Arity - 1 : fn->Arity;
		const int fewest = fn->DefaultIps.empty() ? fixed : fn->MinArity;
		if (!names && (given < fewest || (!fn->HasRest && given > fixed)))
		{
			const std::string needs = fn->HasRest ? "at least " + ValueCount(fewest)
				: fewest == fixed ? ValueCount(fixed) : std::to_string(fewest) + " to " + ValueCount(fixed);
			return Fail("'" + FunctionLabel(fn) + "' needs " + needs + ", but was given " + ValueCount(given) + ".");
		}
		if (task->Frames.size() >= m_Config.MaxCallDepth)
			return Fail("Too many functions calling each other (is a function calling itself forever?).");
		if (names)
			return CallWithNames(task, closure, argCount, flags, names);

		// The values after the fixed ones become one list. They stay on the stack (where the collector
		// sees them) until the list holds them.
		Value rest;
		if (fn->HasRest)
		{
			const int extra = given > fixed ? given - fixed : 0;
			const auto first = task->Stack.end() - extra;
			rest = MakeList(std::vector<Value>(first, task->Stack.end()));
			task->Stack.resize(task->Stack.size() - static_cast<size_t>(extra));
			given -= extra;
		}

		// Values left out get their defaults: their places are made here, and the call starts at the
		// code that fills in the first one left out.
		size_t start = 0;
		if (!fn->DefaultIps.empty())
		{
			for (int i = given; i < fixed; i++)
				task->Stack.push_back(Value());
			start = fn->DefaultIps[static_cast<size_t>(given - fewest)];
		}
		if (fn->HasRest)
			task->Stack.push_back(rest);

		CallFrame frame;
		frame.Function = closure;
		frame.Ip = start;
		// The values with defaults that this call gave are the first (given - fewest) of them.
		if (!fn->DefaultIps.empty())
		{
			const int count = given - fewest;
			frame.Given = count >= 64 ? ~uint64_t(0) : (uint64_t(1) << count) - 1;
		}
		frame.Base = task->Stack.size() - static_cast<size_t>(fn->Arity) - 1;
		frame.Flags = flags;
		task->Frames.push_back(frame);
		return true;
	}

	// A call whose last values are named: they go to the parameters with those names. A parameter left
	// out gets its default (its default code runs; the others' is skipped), or the call fails.
	bool VM::CallWithNames(Task* task, Closure* closure, size_t argCount, uint8_t flags, List* names)
	{
		Function* fn = closure->Fn;
		const size_t named = names->Items.size();
		const size_t positional = argCount - named;
		const size_t arity = static_cast<size_t>(fn->Arity);
		// With a last parameter that collects the rest, the positional values after the fixed ones go
		// to it; it can't be named.
		const size_t fixed = fn->HasRest ? arity - 1 : arity;
		const size_t fewest = fn->DefaultIps.empty() ? fixed : static_cast<size_t>(fn->MinArity);
		const std::string label = "'" + FunctionLabel(fn) + "'";
		if (!fn->HasRest && positional > arity)
			return Fail(label + " needs at most " + ValueCount(fn->Arity) + ", but was given " + ValueCount(static_cast<int>(positional)) + " before the named ones.");

		std::vector<Value> values(arity);
		std::vector<bool> given(arity, false);
		const size_t base = task->Stack.size() - argCount;
		for (size_t i = 0; i < positional && i < fixed; i++)
		{
			values[i] = task->Stack[base + i];
			given[i] = true;
		}
		if (fn->HasRest)
		{
			const size_t first = std::min(positional, fixed);
			values[fixed] = MakeList(std::vector<Value>(task->Stack.begin() + static_cast<std::ptrdiff_t>(base + first),
				task->Stack.begin() + static_cast<std::ptrdiff_t>(base + positional)));
			given[fixed] = true;
		}
		for (size_t i = 0; i < named; i++)
		{
			const std::string& name = As<String>(names->Items[i])->Text;
			if (fn->HasRest && name == fn->ParamNames.back())
				return Fail(label + " collects the values left over in '" + name + "', so it can't be named. Give them in order, after the others.");
			auto found = std::find(fn->ParamNames.begin(), fn->ParamNames.end(), name);
			if (found == fn->ParamNames.end())
			{
				std::vector<std::string_view> candidates(fn->ParamNames.begin(), fn->ParamNames.end());
				return Fail(label + " has no value called '" + name + "'." + DidYouMean(name, candidates));
			}
			const size_t index = static_cast<size_t>(found - fn->ParamNames.begin());
			if (given[index])
				return Fail(label + " was given '" + name + "' twice.");
			values[index] = task->Stack[base + positional + i];
			given[index] = true;
		}
		size_t firstMissing = fixed;
		for (size_t i = 0; i < fixed; i++)
		{
			if (given[i])
				continue;
			if (i < fewest)
				return Fail(label + " needs a value for '" + fn->ParamNames[i] + "'.");
			if (firstMissing == fixed)
				firstMissing = i;
		}

		task->Stack.resize(base);
		task->Stack.insert(task->Stack.end(), values.begin(), values.end());
		CallFrame frame;
		frame.Function = closure;
		frame.Ip = fn->DefaultIps.empty() ? 0 : fn->DefaultIps[firstMissing - fewest];
		frame.Given = 0;
		for (size_t i = fewest; i < fixed; i++)
		{
			if (given[i])
				frame.Given |= uint64_t(1) << (i - fewest);
		}
		frame.Base = task->Stack.size() - arity - 1;
		frame.Flags = flags;
		task->Frames.push_back(frame);
		return true;
	}

	bool VM::CallNative(Task* task, NativeFunction* native, size_t argCount, const Value& self)
	{
		if (std::exchange(task->PendingNames, nullptr))
			return Fail("'" + native->Name + "' is built in, so its values can't be named. Give them in order.");
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
				(receiver.IsNothing() ? ". The value has not been set yet." : "." + OtherLanguageMethodHint(TypeOf(receiver), name->Text)));
		}

		// 'properties' are the type's values read without brackets, like a list's length.
		auto callBuiltin = [&](std::unordered_map<String*, NativeFunction*>& table, const char* typeName,
			std::vector<std::string_view> properties) -> bool
		{
			auto it = table.find(name);
			if (it == table.end())
			{
				if (std::find(properties.begin(), properties.end(), name->Text) != properties.end())
				{
					return Fail("'" + name->Text + "' of a " + typeName + " is a value, not a function. Write it without brackets: ." +
						name->Text);
				}
				AddNames(properties, table);
				std::string hint = OtherLanguageMethodHint(typeName, name->Text);
				if (hint.empty())
					hint = DidYouMean(name->Text, properties);
				return Fail(std::string("A ") + typeName + " has no function called '" + name->Text + "'." + hint);
			}
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
				{
					std::vector<std::string_view> names;
					AddNames(names, instance->Fields);
					AddKindMethods(names, instance->Class);
					return Fail("'" + instance->Class->Name->Text + "' has no function called '" + name->Text + "'." + DidYouMean(name->Text, names));
				}
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
				std::vector<std::string_view> names;
				AddNames(names, object->Type->Methods);
				return Fail("A " + object->Type->Name + " has no function called '" + name->Text + "'." + DidYouMean(name->Text, names));
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
				return callBuiltin(MapMethods, "map", { "length", "size", "count" });
			}
			case ObjectType::List:   return callBuiltin(ListMethods, "list", { "length", "size", "count", "first", "last" });
			case ObjectType::String: return callBuiltin(TextMethods, "text", { "length", "size", "count" });
			case ObjectType::Vector: return callBuiltin(VectorMethods, "vector", { "x", "y", "z", "length" });
			case ObjectType::Module:
			{
				Module* module = As<Module>(receiver);
				auto global = module->Globals.find(name);
				if (global == module->Globals.end())
				{
					std::vector<std::string_view> names;
					AddNames(names, module->Globals);
					return Fail("The script '" + module->Name + "' has nothing called '" + name->Text + "'." + DidYouMean(name->Text, names));
				}
				task->Stack[receiverSlot] = global->second;
				return CallValue(task, global->second, argCount);
			}
			case ObjectType::NativeFunction:
			{
				NativeFunction* function = As<NativeFunction>(receiver);
				auto member = function->Members.find(name->Text);
				if (member == function->Members.end())
				{
					std::vector<std::string_view> names;
					AddNames(names, function->Members);
					return Fail("'" + function->Name + "' has nothing called '" + name->Text + "'." + DidYouMean(name->Text, names));
				}
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

	std::vector<std::string_view> VM::GlobalNameCandidates(const Function* fn) const
	{
		std::vector<std::string_view> names;
		for (const LocalName& local : fn->LocalNames)
			names.push_back(local.Name);
		for (const std::string& upvalue : fn->UpvalueNames)
			names.push_back(upvalue);
		if (fn->Owner)
			AddNames(names, fn->Owner->Globals);
		AddNames(names, m_SharedGlobals);
		return names;
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
				std::vector<std::string_view> names;
				AddNames(names, instance->Fields);
				AddKindMethods(names, instance->Class);
				Kind* owner = nullptr;
				if (instance->Class->FindShared(name, &owner))
				{
					return Fail("'" + instance->Class->Name->Text + "' has no field or function called '" + key + "'. '" + key +
						"' is shared by every " + owner->Name->Text + ": write " + owner->Name->Text + "." + key);
				}
				return Fail("'" + instance->Class->Name->Text + "' has no field or function called '" + key + "'." + DidYouMean(key, names));
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
				std::vector<std::string_view> names;
				AddNames(names, native->Type->Getters);
				AddNames(names, native->Type->Methods);
				return Fail("A " + native->Type->Name + " has no property called '" + key + "'." + DidYouMean(key, names));
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
				std::vector<std::string_view> names = { "length", "size", "count", "first", "last" };
				AddNames(names, ListMethods);
				return Fail("A list has no property called '" + key + "'." + DidYouMean(key, names));
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
				std::vector<std::string_view> names = { "length", "size", "count" };
				AddNames(names, TextMethods);
				return Fail("Text has no property called '" + key + "'." + DidYouMean(key, names));
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
				{
					std::vector<std::string_view> names;
					AddNames(names, module->Globals);
					return Fail("The script '" + module->Name + "' has nothing called '" + key + "'." + DidYouMean(key, names));
				}
				outValue = global->second;
				return true;
			}
			case ObjectType::NativeFunction:
			{
				NativeFunction* function = As<NativeFunction>(object);
				auto member = function->Members.find(key);
				if (member == function->Members.end())
				{
					std::vector<std::string_view> names;
					AddNames(names, function->Members);
					return Fail("'" + function->Name + "' has nothing called '" + key + "'." + DidYouMean(key, names));
				}
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
				if (Value* shared = kind->FindShared(name))
				{
					outValue = *shared;
					return true;
				}
				std::vector<std::string_view> names;
				for (Kind* k = kind; k; k = k->Parent)
					AddNames(names, k->Shared);
				return Fail("The kind '" + kind->Name->Text + "' has no property called '" + key + "'." + DidYouMean(key, names));
			}
			default:
				break;
		}
		return Fail("A " + TypeOf(object) + " has no property called '" + key + "'.");
	}

	bool VM::SetWatchedField(Task* task, Instance* instance, String* name, const Value& value)
	{
		if (!instance->Class->WatchedFields.count(name))
		{
			instance->SetField(name, value);
			return true;
		}
		auto old = instance->Fields.find(name);
		const bool had = old != instance->Fields.end();
		const Value before = had ? old->second : Value();
		instance->SetField(name, value);

		// A first value (the field is new, or the kind's 'set' lines are giving it) or an equal one
		// isn't a change; nor is a write inside this field's own handler.
		if (!had || ValuesEqual(before, value))
			return true;
		if (task && !task->Frames.empty() && task->Frames.back().Function->Fn->IsFieldInit)
			return true;
		if (task && task->ChangeInstance == instance && task->ChangeField == name)
			return true;

		// The handlers for this field, a kind it extends first.
		std::vector<Kind*> kinds;
		for (Kind* kind = instance->Class; kind; kind = kind->Parent)
			kinds.push_back(kind);
		std::vector<Closure*> handlers;
		for (auto kind = kinds.rbegin(); kind != kinds.rend(); ++kind)
		{
			auto found = (*kind)->ChangeHandlers.find(name);
			if (found != (*kind)->ChangeHandlers.end())
				handlers.insert(handlers.end(), found->second.begin(), found->second.end());
		}
		if (handlers.empty())
			return true;

		const size_t depth = task ? task->EventDepth + 1 : 0;
		if (m_RunningTasks.size() >= m_Config.MaxNestedCalls || depth >= m_Config.MaxNestedCalls)
		{
			return Fail("'" + name->Text + "' was changed from inside " + std::to_string(m_Config.MaxNestedCalls) +
				" other changes and events; 'when ... changes' handlers that change each other's fields need a way to stop.");
		}

		// Like 'trigger': every task is made before any runs, each one a method call on the object.
		std::vector<Task*> tasks;
		for (Closure* handler : handlers)
		{
			Task* handlerTask = NewTask(handler->Fn->Owner);
			handlerTask->EventDepth = depth;
			handlerTask->ChangeInstance = instance;
			handlerTask->ChangeField = name;
			m_PendingTasks.push_back(handlerTask);
			// The pending task keeps itself, and the kind its handler, while the call is made.
			handlerTask->Stack.push_back(Value::FromObject(Allocate<BoundMethod>(0, Value::FromObject(instance), handler)));
			const Value values[2] = { value, before };
			for (int i = 0; i < handler->Fn->Arity && i < 2; i++)
				handlerTask->Stack.push_back(values[i]);
			tasks.push_back(handlerTask);
		}
		for (Task* handlerTask : tasks)
		{
			StartCall(handlerTask, handlerTask->Stack.size() - 1);
			RemovePending(handlerTask);
		}
		return true;
	}

	bool VM::SetProperty(Task* task, const Value& object, String* name, const Value& value)
	{
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
			{
				Instance* instance = As<Instance>(object);
				if (instance->Class && instance->Class->WatchesFields)
					return SetWatchedField(task, instance, name, value);
				instance->SetField(name, value);
				return true;
			}
			case ObjectType::Map:
				As<Map>(object)->Set(Value::FromObject(name), value);
				return true;
			case ObjectType::Kind:
			{
				Kind* kind = As<Kind>(object);
				if (Value* shared = kind->FindShared(name))
				{
					*shared = value;
					return true;
				}
				return Fail("Cannot change '" + key + "' on a kind. To give every " + kind->Name->Text +
					" one value they share, write inside the kind: shared " + key + " is 0");
			}
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
					std::vector<std::string_view> names;
					AddNames(names, native->Type->Setters);
					return Fail("A " + native->Type->Name + " has no property called '" + key + "'." + DidYouMean(key, names));
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
			if (count < 0 || count != std::floor(count) || !std::isfinite(count))
				return Fail("Text can only be repeated a whole number of times.");
			const std::string& text = As<String>(a)->Text;
			// One step must not build gigabytes (or loop forever on infinity) where the instruction limit
			// can't stop it.
			if (!text.empty() && count > static_cast<double>(c_MaxRepeatedTextBytes / text.size()))
				return Fail("Repeating this text " + FormatNumber(count) + " times would make more than " +
					FormatNumber(static_cast<double>(c_MaxRepeatedTextBytes)) + " bytes of text.");
			std::string result;
			result.reserve(text.size() * static_cast<size_t>(count));
			for (double i = 0; i < count && !text.empty(); i++)
				result += text;
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
		// The compiler refuses such a 'use' too; refusing here also covers the phrase prescan and hosts.
		if (!CheckUsePath(path).empty() || !m_ModuleLoader || !m_ModuleLoader(path, outName, outSource))
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

	std::vector<std::string> VM::BuildTrace(Task* task, std::string& outFile, int& outLine, std::vector<StackFrame>* outFrames)
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
			if (outFrames)
			{
				const std::string function = isScript ? std::string() : FunctionLabel(fn);
				if (!outFrames->empty() && outFrames->back().File == file && outFrames->back().Line == line && outFrames->back().Function == function)
					outFrames->back().Repeats++;
				else
					outFrames->push_back({ file, line, function, 1 });
			}
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

	// Where an error happened, as a list: the file, the line, the trace lines, then each frame as
	// [file, line, function, repeats]. A 'finally' handler keeps it with the error, so an error that goes
	// on after the finally code is reported where it happened. Only the VM reads it.
	Value VM::DescribeWhere(Task* task)
	{
		std::string file;
		int line = 0;
		std::vector<StackFrame> frames;
		const std::vector<std::string> trace = BuildTrace(task, file, line, &frames);
		Value where = MakeList();
		Value traceList = MakeList();
		Value frameList = MakeList();
		As<List>(where)->Items = { MakeText(file), Value::FromNumber(line), traceList, frameList };
		for (const std::string& entry : trace)
			As<List>(traceList)->Items.push_back(MakeText(entry));
		for (const StackFrame& frame : frames)
		{
			Value item = MakeList();
			As<List>(item)->Items = { MakeText(frame.File), Value::FromNumber(frame.Line), MakeText(frame.Function), Value::FromNumber(frame.Repeats) };
			As<List>(frameList)->Items.push_back(item);
		}
		return where;
	}

	void VM::ReportFailure(Task* task, const Value& error, const Value* where)
	{
		std::string message;
		if (IsObjectType(error, ObjectType::String))
			message = As<String>(error)->Text;
		else if (!SafeToText(error, message))
			message = "A " + TypeOf(error) + " was raised as an error.";
		std::string file;
		int line = 0;
		std::vector<std::string> trace;
		std::vector<StackFrame> frames;
		auto text = [](const Value& value) { return IsObjectType(value, ObjectType::String) ? As<String>(value)->Text : std::string(); };
		auto number = [](const Value& value) { return value.IsNumber() ? static_cast<int>(value.AsNumber()) : 0; };
		if (where && IsObjectType(*where, ObjectType::List) && As<List>(*where)->Items.size() == 4)
		{
			const std::vector<Value>& items = As<List>(*where)->Items;
			file = text(items[0]);
			line = number(items[1]);
			if (IsObjectType(items[2], ObjectType::List))
			{
				for (const Value& entry : As<List>(items[2])->Items)
					trace.push_back(text(entry));
			}
			if (IsObjectType(items[3], ObjectType::List))
			{
				for (const Value& entry : As<List>(items[3])->Items)
				{
					if (!IsObjectType(entry, ObjectType::List) || As<List>(entry)->Items.size() != 4)
						continue;
					const std::vector<Value>& parts = As<List>(entry)->Items;
					frames.push_back({ text(parts[0]), number(parts[1]), text(parts[2]), number(parts[3]) });
				}
			}
		}
		else
		{
			trace = BuildTrace(task, file, line, &frames);
		}

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
		report.Frames = std::move(frames);
		task->FailureMessage = report.ToString();
		if (m_ErrorHandler)
			m_ErrorHandler(report);
	}

	// 'where' is given when the error is raised again after finally code, to keep where it first happened.
	bool VM::ThrowValue(Task* task, const Value& error, const Value* where)
	{
		task->PendingNames = nullptr;
		if (!task->Handlers.empty())
		{
			TryHandler handler = task->Handlers.back();
			task->Handlers.pop_back();
			// Worked out before the frames are unwound; allocating here is safe, as the collector only runs
			// between instructions.
			const Value place = handler.Finally ? (where ? *where : DescribeWhere(task)) : Value();
			CloseUpvalues(task, handler.StackSize);
			task->Frames.resize(handler.FrameCount);
			task->Stack.resize(handler.StackSize);
			task->Stack.push_back(error);
			if (handler.Finally)
				task->Stack.push_back(place);
			task->Frames.back().Ip = handler.CatchIp;
			return true;
		}
		ReportFailure(task, error, where);
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
		// Every instruction this run executes when VMConfig::CountInstructions (or a limit) is on, added
		// to the VM's total however the run ends.
		struct CountOnExit
		{
			uint64_t& Total;
			uint64_t Executed = 0;
			~CountOnExit() { Total += Executed; }
		} executed { m_InstructionsRun };
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
			// One test for both, and none of their work in the usual run, which keeps the loop fast.
			if (m_Config.InstructionLimit || m_Config.CountInstructions)
			{
				executed.Executed++;
				if (m_Config.InstructionLimit && ++instructions > m_Config.InstructionLimit)
				{
					instructions = 0;
					FORGESCRIPT_FAIL("This script ran too long without waiting. Add a 'wait' inside long loops.");
				}
			}
			if (m_DebugActive && DebugLineReached(task, frame))
			{
				task->State = TaskState::Stopped;
				return;
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
						// A name from another language ('True', 'len') gets the ForgeScript for it; otherwise a typo of
						// a known name is far more likely than a forgotten 'set', so name the match.
						std::string hint = OtherLanguageNameHint(name->Text);
						if (hint.empty())
							hint = DidYouMean(name->Text, GlobalNameCandidates(frame->Function->Fn));
						FORGESCRIPT_FAIL("'" + name->Text + "' has not been given a value yet." +
							(hint.empty() ? " Use: set " + name->Text + " to ..." : hint));
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
					// "1 < x < 10" compares the first answer (true or false) with 10.
					if (stack[stack.size() - 2].IsBool() && stack.back().IsNumber())
						FORGESCRIPT_FAIL(std::string("Cannot compare boolean with number. Only numbers with numbers, or text with text. To check that a value is between two others, write: if x is between 1 and 10"));
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
				case OpCode::Between:
				{
					// Both ends count, and they may come in either order: 5 is between 10 and 1.
					const Value high = stack.back();
					const Value low = stack[stack.size() - 2];
					const Value value = stack[stack.size() - 3];
					bool swapped = false;
					FORGESCRIPT_CHECK(Compare(task, OpCode::Greater, low, high, swapped));
					bool aboveLow = false;
					bool belowHigh = false;
					FORGESCRIPT_CHECK(Compare(task, OpCode::GreaterEqual, value, swapped ? high : low, aboveLow));
					FORGESCRIPT_CHECK(Compare(task, OpCode::LessEqual, value, swapped ? low : high, belowHigh));
					stack.resize(stack.size() - 2);
					stack.back() = Value::FromBool(aboveLow && belowHigh);
					break;
				}
				case OpCode::NumberTest:
				{
					const NumberTest test = static_cast<NumberTest>(FORGESCRIPT_READ_BYTE());
					const Value value = stack.back();
					if (test == NumberTest::Whole)
					{
						stack.back() = Value::FromBool(value.IsNumber() && std::isfinite(value.AsNumber()) && std::floor(value.AsNumber()) == value.AsNumber());
						break;
					}
					if (!value.IsNumber())
					{
						FORGESCRIPT_FAIL(std::string("'is ") + (test == NumberTest::Even ? "even" : "odd") + "' needs a number, but this is " +
							(value.IsNothing() ? std::string("nothing") : "a " + TypeOf(value)) + ".");
					}
					const double number = value.AsNumber();
					const bool whole = std::isfinite(number) && std::floor(number) == number;
					const bool even = whole && std::fmod(number, 2.0) == 0.0;
					stack.back() = Value::FromBool(whole && (test == NumberTest::Even ? even : !even));
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
					else if (IsObjectType(iterable, ObjectType::Instance))
					{
						// An object made from a kind: its fields in the order they were first set, like a map's
						// keys. Its functions belong to the kind, so they aren't among them.
						Instance* instance = As<Instance>(iterable);
						if (position >= instance->FieldOrder.size())
						{
							frame->Ip += exit;
							break;
						}
						String* field = instance->FieldOrder[position];
						stack[slot + 1] = Value::FromNumber(static_cast<double>(position + 1));
						stack.push_back(Value::FromObject(field));
						stack.push_back(instance->Fields[field]);
					}
					else
					{
						FORGESCRIPT_FAIL("'for each' needs a list, a map, text or an object made from a kind, but got " +
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
					{
						std::vector<std::string_view> names;
						AddKindMethods(names, owner->Parent);
						FORGESCRIPT_FAIL("The parent kind '" + owner->Parent->Name->Text + "' has no function called '" + name->Text + "'." +
							DidYouMean(name->Text, names));
					}
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
					{
						std::vector<std::string_view> names;
						AddKindMethods(names, owner->Parent);
						FORGESCRIPT_FAIL("The parent kind '" + owner->Parent->Name->Text + "' has no function called '" + name->Text + "'." +
							DidYouMean(name->Text, names));
					}
					stack.back() = Value::FromObject(Allocate<BoundMethod>(0, stack.back(), method));
					break;
				}
				case OpCode::Start:
				{
					uint8_t count = FORGESCRIPT_READ_BYTE();
					size_t calleeIndex = stack.size() - 1 - count;
					Task* spawned = NewTask(frame->Function->Fn->Owner);
					spawned->PendingNames = std::exchange(task->PendingNames, nullptr);
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
					kind->WatchesFields = kind->WatchesFields || kind->Parent->WatchesFields;
					kind->WatchedFields.insert(kind->Parent->WatchedFields.begin(), kind->Parent->WatchedFields.end());
					break;
				}
				case OpCode::ChangeHandler:
				{
					String* field = FORGESCRIPT_READ_STRING();
					Closure* handler = As<Closure>(stack.back());
					stack.pop_back();
					Kind* kind = As<Kind>(stack.back());
					kind->ChangeHandlers[field].push_back(handler);
					kind->WatchesFields = true;
					kind->WatchedFields.insert(field);
					break;
				}
				case OpCode::AddTest:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Closure* test = As<Closure>(stack.back());
					stack.pop_back();
					frame->Function->Fn->Owner->Tests.emplace_back(name, test);
					break;
				}
				case OpCode::Expect:
				{
					String* text = FORGESCRIPT_READ_STRING();
					String* leftText = FORGESCRIPT_READ_STRING();
					const bool hasLeft = FORGESCRIPT_READ_BYTE() != 0;
					const Value condition = stack.back();
					stack.pop_back();
					if (condition.IsTruthy())
						break;
					if (hasLeft)
						FORGESCRIPT_FAIL("Expected " + text->Text + ", but " + leftText->Text + " is " + Describe(stack.back(), true) + ".");
					FORGESCRIPT_FAIL("Expected " + text->Text + ", but it is " + Describe(condition, true) + ".");
				}
				case OpCode::SharedValue:
				{
					String* name = FORGESCRIPT_READ_STRING();
					Value value = stack.back();
					stack.pop_back();
					As<Kind>(stack.back())->Shared[name] = value;
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

				case OpCode::TryBegin: case OpCode::TryFinally:
				{
					uint16_t offset = FORGESCRIPT_READ_U16();
					TryHandler handler;
					handler.FrameCount = task->Frames.size();
					handler.StackSize = stack.size();
					handler.CatchIp = frame->Ip + offset;
					handler.Finally = op == OpCode::TryFinally;
					task->Handlers.push_back(handler);
					break;
				}
				case OpCode::Unpack:
				{
					const size_t count = FORGESCRIPT_READ_BYTE();
					const Value value = stack.back();
					const bool isList = IsObjectType(value, ObjectType::List);
					const bool isVector = IsObjectType(value, ObjectType::Vector);
					if (!isList && !isVector)
					{
						FORGESCRIPT_FAIL("Only a list or a vector can be taken apart, like: set x, y to position. This is " +
							std::string(value.IsNothing() ? "nothing" : "a " + TypeOf(value)) + ".");
					}
					if (isVector && count != 2 && count != 3)
						FORGESCRIPT_FAIL("A vector can be taken apart into 2 names (x, y) or 3 (x, y, z), not " + std::to_string(count) + ".");
					std::vector<Value> items;
					if (isList)
					{
						items = As<List>(value)->Items;
					}
					else
					{
						const Vector* vector = As<Vector>(value);
						items = { Value::FromNumber(vector->X), Value::FromNumber(vector->Y), Value::FromNumber(vector->Z) };
						items.resize(count);
					}
					if (items.size() != count)
						FORGESCRIPT_FAIL("There are " + std::to_string(count) + " names but the list has " + std::to_string(items.size()) +
							(items.size() == 1 ? " item." : " items.") + " Give one name for each item.");
					stack.pop_back();
					stack.insert(stack.end(), items.begin(), items.end());
					break;
				}
				case OpCode::NamedArgs:
					task->PendingNames = As<List>(FORGESCRIPT_READ_CONSTANT());
					break;
				case OpCode::SkipIfGiven:
				{
					const uint8_t index = FORGESCRIPT_READ_BYTE();
					const uint16_t offset = FORGESCRIPT_READ_U16();
					if ((frame->Given >> index) & 1)
						frame->Ip += offset;
					break;
				}
				case OpCode::Rethrow:
				{
					const Value where = stack.back();
					stack.pop_back();
					const Value error = stack.back();
					stack.pop_back();
					if (!ThrowValue(task, error, &where))
						return;
					FORGESCRIPT_REFRESH();
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
					task->EventDepth = 0;
					return;
				}
				case OpCode::WaitEvent:
				{
					String* event = FORGESCRIPT_READ_STRING();
					String* source = FORGESCRIPT_READ_STRING();
					uint8_t count = FORGESCRIPT_READ_BYTE();
					if (!task->CanYield)
						FORGESCRIPT_FAIL("'wait for' cannot be used here, because this code must finish right away. Use 'start' to run it as its own task.");
					if (!task->Owner)
						FORGESCRIPT_FAIL("'wait for' needs a script to wait in.");
					task->Mode = WaitMode::Event;
					task->WaitEvent = event;
					task->WaitSource = source->Text;
					task->WaitCount = count;
					task->Owner->EventWaits[event]++;
					task->State = TaskState::Waiting;
					task->EventDepth = 0;
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
					// The task's own count matters too: a handler the debugger held and let go runs outside
					// the call it is still part of.
					if (m_RunningTasks.size() >= m_Config.MaxNestedCalls || task->EventDepth + 1 >= m_Config.MaxNestedCalls)
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
		for (Task* task : m_SpareTasks)
			MarkObject(task);
		for (Task* task : m_PendingTasks)
			MarkObject(task);
		if (m_StoppedTask)
			MarkObject(m_StoppedTask);
		for (auto& [task, stop] : m_QueuedStops)
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
				for (auto& [name, value] : kind->Shared)
				{
					MarkObject(name);
					MarkValue(value);
				}
				for (auto& [name, handlers] : kind->ChangeHandlers)
				{
					MarkObject(name);
					for (Closure* handler : handlers)
						MarkObject(handler);
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
				for (auto& [name, test] : module->Tests)
				{
					MarkObject(name);
					MarkObject(test);
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
				if (task->WaitEvent)
					MarkObject(task->WaitEvent);
				if (task->ChangeInstance)
					MarkObject(task->ChangeInstance);
				if (task->ChangeField)
					MarkObject(task->ChangeField);
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
