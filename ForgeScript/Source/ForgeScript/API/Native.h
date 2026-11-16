#pragma once

#include "ForgeScript/Core/Object.h"
#include "ForgeScript/Core/Value.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ForgeScript {

	class VM;
	struct Task;

	// Thrown by native (C++) functions to report a script error.
	// The VM turns it into a normal ForgeScript error that scripts can "try ... catch".
	class RuntimeError : public std::runtime_error
	{
	public:
		explicit RuntimeError(const std::string& message) : std::runtime_error(message) {}
	};

	// Describes a C++ type that scripts can hold, read and call methods on.
	// Example: the engine registers "Entity" with getters for "position" and a "destroy" method.
	struct NativeType
	{
		struct MethodInfo
		{
			int Arity = 0;
			NativeFn Fn;
		};

		std::string Name;
		std::unordered_map<std::string, std::function<Value(VM&, void*)>> Getters;
		std::unordered_map<std::string, std::function<void(VM&, void*, const Value&)>> Setters;
		std::unordered_map<std::string, MethodInfo> Methods;

		// Optional: called when the script wrapper is garbage collected (not when the C++ object dies).
		std::function<void(void*)> Finalizer;
		// Optional: return false when the C++ object has been destroyed, so scripts get a clear error.
		std::function<bool(void*)> IsAlive;
		// Optional: how the object looks when printed.
		std::function<std::string(void*)> ToText;
		// Optional: when two wrappers stand for the same C++ object (for example two handles to one
		// entity), Equals makes "is" treat them as equal and Hash lets them work as map keys.
		std::function<bool(void*, void*)> Equals;
		std::function<size_t(void*)> Hash;
	};

	// Everything a native function needs: its arguments, "self" for methods, and the VM.
	class CallContext
	{
	public:
		CallContext(VM& vm, Task* task, size_t base, size_t count, Value self, const std::string& name)
			: m_VM(vm), m_Task(task), m_Base(base), m_Count(count), m_Self(self), m_Name(name) {}

		VM& GetVM() const { return m_VM; }
		const std::string& GetName() const { return m_Name; }
		size_t Count() const { return m_Count; }

		// Returns argument i (0 based), or nothing when it was not given.
		Value Arg(size_t index) const;

		double Number(size_t index) const;
		int64_t Integer(size_t index) const;
		bool Bool(size_t index) const;
		const std::string& Text(size_t index) const;
		String* TextObject(size_t index) const;
		List* ListArg(size_t index) const;
		Map* MapArg(size_t index) const;
		Vector* VectorArg(size_t index) const;
		Closure* FunctionArg(size_t index) const;
		bool IsCallable(size_t index) const;

		template<typename T>
		T* NativeArg(size_t index, const NativeType& type) const
		{
			return static_cast<T*>(NativeData(Arg(index), type, index));
		}

		// For methods: the object the method was called on.
		Value Self() const { return m_Self; }

		template<typename T>
		T* SelfAs(const NativeType& type) const
		{
			return static_cast<T*>(NativeData(m_Self, type, static_cast<size_t>(-1)));
		}

		[[noreturn]] void Error(const std::string& message) const;

	private:
		void* NativeData(const Value& value, const NativeType& type, size_t index) const;
		[[noreturn]] void TypeError(size_t index, const char* expected) const;

	private:
		VM& m_VM;
		Task* m_Task;
		size_t m_Base;
		size_t m_Count;
		Value m_Self;
		const std::string& m_Name;
	};

}
