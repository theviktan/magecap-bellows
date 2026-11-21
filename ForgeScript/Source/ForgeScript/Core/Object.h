#pragma once

#include "ForgeScript/Core/Chunk.h"
#include "ForgeScript/Core/Value.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ForgeScript {

	class VM;
	class CallContext;
	struct NativeType;
	struct Task;

	enum class ObjectType : uint8_t
	{
		String = 0,
		List,
		Map,
		Function,
		Closure,
		Upvalue,
		NativeFunction,
		Kind,
		Instance,
		BoundMethod,
		Vector,
		NativeObject,
		Module,
		Task
	};

	// Base class of everything that lives on the ForgeScript heap.
	// Objects are owned by the VM and freed by its garbage collector.
	class Object
	{
	public:
		explicit Object(ObjectType type) : m_Type(type) {}
		virtual ~Object() = default;

		Object(const Object&) = delete;
		Object& operator=(const Object&) = delete;

		ObjectType GetType() const { return m_Type; }

	private:
		ObjectType m_Type;
		bool m_Marked = false;
		uint32_t m_AllocatedSize = 0;
		Object* m_Next = nullptr;

		friend class VM;
	};

	struct String : public Object
	{
		std::string Text;
		size_t Hash = 0;

		String(std::string text, size_t hash) : Object(ObjectType::String), Text(std::move(text)), Hash(hash) {}
	};

	struct List : public Object
	{
		std::vector<Value> Items;

		List() : Object(ObjectType::List) {}
	};

	// An insertion-ordered dictionary so that "for each key in map" is always predictable.
	struct Map : public Object
	{
		struct Entry
		{
			Value Key;
			Value Item;
		};

		Map() : Object(ObjectType::Map) {}

		bool Get(const Value& key, Value& outValue) const;
		void Set(const Value& key, const Value& value);
		bool Remove(const Value& key);
		bool Has(const Value& key) const { return Index.find(key) != Index.end(); }
		size_t Size() const { return Entries.size(); }

		std::vector<Entry> Entries;
		std::unordered_map<Value, size_t, ValueHash> Index;
	};

	struct Module;

	// A named variable of a function, for the debugger: it lives in stack slot 'Slot' while the code
	// between StartIp and EndIp runs.
	struct LocalName
	{
		std::string Name;
		uint8_t Slot = 0;
		size_t StartIp = 0;
		size_t EndIp = static_cast<size_t>(-1);
	};

	struct Function : public Object
	{
		ForgeScript::Chunk Chunk;
		int Arity = 0;
		// With default values: the fewest values a call may give, and where a call that gives k values
		// starts (DefaultIps[k - MinArity]; the last entry is for a call that gives them all). Empty
		// when no parameter has a default.
		int MinArity = 0;
		std::vector<size_t> DefaultIps;
		std::vector<std::string> ParamNames; // For calls that name their values: attack(goblin, damage: 25).
		// 'function total(numbers...)': the last parameter (counted in Arity) gets the values left over, as
		// a list. MinArity and DefaultIps count only the parameters before it.
		bool HasRest = false;
		int UpvalueCount = 0;
		String* Name = nullptr;
		Module* Owner = nullptr;
		bool IsMethod = false;
		size_t BodyIp = 0; // Where the code written in the body starts, after the set-up of its variables.
		std::vector<LocalName> LocalNames;
		std::vector<std::string> UpvalueNames; // One per upvalue, in order.
		// For a handler written 'when "Start" is clicked': the name of the entity whose event it runs for
		// (compared ignoring letter case). Empty for every other function.
		std::string EventSource;

		Function() : Object(ObjectType::Function) {}
	};

	struct Upvalue : public Object
	{
		ForgeScript::Task* OwnerTask = nullptr;
		size_t Slot = 0;
		bool IsClosed = false;
		Value Closed;
		Upvalue* NextOpen = nullptr;

		Upvalue(ForgeScript::Task* task, size_t slot) : Object(ObjectType::Upvalue), OwnerTask(task), Slot(slot) {}

		Value& Get();
	};

	struct Kind;

	struct Closure : public Object
	{
		ForgeScript::Function* Fn = nullptr;
		std::vector<ForgeScript::Upvalue*> Upvalues;
		ForgeScript::Kind* OwnerKind = nullptr;

		explicit Closure(ForgeScript::Function* fn) : Object(ObjectType::Closure), Fn(fn), Upvalues(fn->UpvalueCount, nullptr) {}
	};

	using NativeFn = std::function<Value(CallContext&)>;

	// Pass this as the arity of a native function that accepts any number of values.
	constexpr int AnyArity = -1;

	struct NativeFunction : public Object
	{
		std::string Name;
		int Arity = 0;
		NativeFn Fn;
		// Values reachable as function.name, for example Material.load next to Material(...).
		std::unordered_map<std::string, Value> Members;

		NativeFunction(std::string name, int arity, NativeFn fn) : Object(ObjectType::NativeFunction), Name(std::move(name)), Arity(arity), Fn(std::move(fn)) {}
	};

	struct Kind : public Object
	{
		String* Name = nullptr;
		Kind* Parent = nullptr;
		Closure* Fields = nullptr;
		std::unordered_map<String*, Closure*> Methods;
		// 'shared count is 0': one value for the kind and every object made from it, read as Enemy.count.
		std::unordered_map<String*, Value> Shared;

		explicit Kind(String* name) : Object(ObjectType::Kind), Name(name) {}

		Closure* FindMethod(String* name) const;
		// The shared value with this name, in this kind or the nearest kind it extends; null if none.
		// 'owner' is set to the kind that has it.
		Value* FindShared(String* name, Kind** owner = nullptr);
	};

	struct Instance : public Object
	{
		Kind* Class = nullptr;
		std::unordered_map<String*, Value> Fields;
		std::vector<String*> FieldOrder;

		explicit Instance(Kind* kind) : Object(ObjectType::Instance), Class(kind) {}

		void SetField(String* name, const Value& value);
	};

	struct BoundMethod : public Object
	{
		Value Receiver;
		Object* Method = nullptr; // Closure or NativeFunction

		BoundMethod(Value receiver, Object* method) : Object(ObjectType::BoundMethod), Receiver(receiver), Method(method) {}
	};

	struct Vector : public Object
	{
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		Vector(double x, double y, double z) : Object(ObjectType::Vector), X(x), Y(y), Z(z) {}
	};

	// Wraps a pointer owned by the host program (for example an engine Entity).
	struct NativeObject : public Object
	{
		const NativeType* Type = nullptr;
		void* Data = nullptr;

		NativeObject(const NativeType* type, void* data) : Object(ObjectType::NativeObject), Type(type), Data(data) {}
		~NativeObject() override;
	};

	struct Module : public Object
	{
		std::string Name;
		std::unordered_map<String*, Value> Globals;
		std::vector<String*> GlobalOrder;
		std::unordered_set<String*> Constants;
		std::unordered_map<String*, std::vector<Closure*>> Handlers;
		// How many of the module's tasks wait for each event ('wait for hit'), so a host can tell that the
		// module wants the event without looking through every task.
		std::unordered_map<String*, int> EventWaits;
		// The script's 'test "name" ... end' blocks, in order. They run only through VM::RunTest.
		std::vector<std::pair<String*, Closure*>> Tests;
		bool HasRun = false;

		explicit Module(std::string name) : Object(ObjectType::Module), Name(std::move(name)) {}

		void SetGlobal(String* name, const Value& value);
	};

	template<typename T>
	inline T* As(const Value& value) { return static_cast<T*>(value.AsObject()); }

	inline bool IsObjectType(const Value& value, ObjectType type)
	{
		return value.IsObject() && value.AsObject()->GetType() == type;
	}

	const char* TypeName(const Value& value);

}
