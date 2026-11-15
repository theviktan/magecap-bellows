#pragma once

#include "Forge/Core/Chunk.h"
#include "Forge/Core/Value.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Forge {

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

	// Base class of everything that lives on the Forge heap.
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

	struct Function : public Object
	{
		Forge::Chunk Chunk;
		int Arity = 0;
		int UpvalueCount = 0;
		String* Name = nullptr;
		Module* Owner = nullptr;
		bool IsMethod = false;

		Function() : Object(ObjectType::Function) {}
	};

	struct Upvalue : public Object
	{
		Forge::Task* OwnerTask = nullptr;
		size_t Slot = 0;
		bool IsClosed = false;
		Value Closed;
		Upvalue* NextOpen = nullptr;

		Upvalue(Forge::Task* task, size_t slot) : Object(ObjectType::Upvalue), OwnerTask(task), Slot(slot) {}

		Value& Get();
	};

	struct Kind;

	struct Closure : public Object
	{
		Forge::Function* Fn = nullptr;
		std::vector<Forge::Upvalue*> Upvalues;
		Forge::Kind* OwnerKind = nullptr;

		explicit Closure(Forge::Function* fn) : Object(ObjectType::Closure), Fn(fn), Upvalues(fn->UpvalueCount, nullptr) {}
	};

	using NativeFn = std::function<Value(CallContext&)>;

	// Pass this as the arity of a native function that accepts any number of values.
	constexpr int AnyArity = -1;

	struct NativeFunction : public Object
	{
		std::string Name;
		int Arity = 0;
		NativeFn Fn;

		NativeFunction(std::string name, int arity, NativeFn fn) : Object(ObjectType::NativeFunction), Name(std::move(name)), Arity(arity), Fn(std::move(fn)) {}
	};

	struct Kind : public Object
	{
		String* Name = nullptr;
		Kind* Parent = nullptr;
		Closure* Fields = nullptr;
		std::unordered_map<String*, Closure*> Methods;

		explicit Kind(String* name) : Object(ObjectType::Kind), Name(name) {}

		Closure* FindMethod(String* name) const;
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
