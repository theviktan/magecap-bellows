#include "ForgeScript/Core/Object.h"

#include "ForgeScript/API/Native.h"
#include "ForgeScript/VM/Task.h"

namespace ForgeScript {

	bool Map::Get(const Value& key, Value& outValue) const
	{
		auto it = Index.find(key);
		if (it == Index.end())
			return false;
		outValue = Entries[it->second].Item;
		return true;
	}

	void Map::Set(const Value& key, const Value& value)
	{
		auto it = Index.find(key);
		if (it != Index.end())
		{
			Entries[it->second].Item = value;
			return;
		}
		Index.emplace(key, Entries.size());
		Entries.push_back({ key, value });
	}

	bool Map::Remove(const Value& key)
	{
		auto it = Index.find(key);
		if (it == Index.end())
			return false;

		size_t removed = it->second;
		Index.erase(it);
		Entries.erase(Entries.begin() + static_cast<std::ptrdiff_t>(removed));

		// Shift the positions of every entry that came after the removed one.
		for (size_t i = removed; i < Entries.size(); i++)
			Index[Entries[i].Key] = i;
		return true;
	}

	Value& Upvalue::Get()
	{
		if (IsClosed)
			return Closed;
		return OwnerTask->Stack[Slot];
	}

	Closure* Kind::FindMethod(String* name) const
	{
		for (const Kind* kind = this; kind; kind = kind->Parent)
		{
			auto it = kind->Methods.find(name);
			if (it != kind->Methods.end())
				return it->second;
		}
		return nullptr;
	}

	Closure* Kind::FindFieldRule(std::unordered_map<String*, Closure*> Kind::*rules, String* name) const
	{
		for (const Kind* kind = this; kind; kind = kind->Parent)
		{
			auto it = (kind->*rules).find(name);
			if (it != (kind->*rules).end())
				return it->second;
		}
		return nullptr;
	}

	Value* Kind::FindShared(String* name, Kind** owner)
	{
		for (Kind* kind = this; kind; kind = kind->Parent)
		{
			auto it = kind->Shared.find(name);
			if (it != kind->Shared.end())
			{
				if (owner)
					*owner = kind;
				return &it->second;
			}
		}
		return nullptr;
	}

	void Instance::SetField(String* name, const Value& value)
	{
		auto it = Fields.find(name);
		if (it == Fields.end())
		{
			FieldOrder.push_back(name);
			Fields.emplace(name, value);
			return;
		}
		it->second = value;
	}

	void Module::SetGlobal(String* name, const Value& value)
	{
		auto it = Globals.find(name);
		if (it == Globals.end())
		{
			GlobalOrder.push_back(name);
			Globals.emplace(name, value);
			GlobalsVersion++;
			return;
		}
		it->second = value;
	}

	NativeObject::~NativeObject()
	{
		if (Type && Type->Finalizer)
			Type->Finalizer(Data);
	}

	const char* TypeName(const Value& value)
	{
		switch (value.GetType())
		{
			case ValueType::Nothing: return "nothing";
			case ValueType::Boolean: return "boolean";
			case ValueType::Number:  return "number";
			case ValueType::Object:  break;
		}

		switch (value.AsObject()->GetType())
		{
			case ObjectType::String:         return "text";
			case ObjectType::List:           return "list";
			case ObjectType::Map:            return "map";
			case ObjectType::Function:       return "function";
			case ObjectType::Closure:        return "function";
			case ObjectType::Upvalue:        return "upvalue";
			case ObjectType::NativeFunction: return "function";
			case ObjectType::Kind:           return "kind";
			case ObjectType::Contract:       return "contract";
			case ObjectType::Instance:       return "object";
			case ObjectType::BoundMethod:    return "function";
			case ObjectType::Vector:         return "vector";
			case ObjectType::NativeObject:   return "object";
			case ObjectType::Module:         return "module";
			case ObjectType::Task:           return "task";
		}
		return "unknown";
	}

}
