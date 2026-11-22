#include "ForgeScript/Core/Value.h"

#include "ForgeScript/API/Native.h"
#include "ForgeScript/Core/Object.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace ForgeScript {

	bool ComparedByContents(const Object* object)
	{
		return object->GetType() == ObjectType::Vector || object->GetType() == ObjectType::NativeObject;
	}

	bool Value::operator==(const Value& other) const
	{
		if (m_Type != other.m_Type)
			return false;

		switch (m_Type)
		{
			case ValueType::Nothing: return true;
			case ValueType::Boolean: return m_Bool == other.m_Bool;
			case ValueType::Number:  return m_Number == other.m_Number;
			case ValueType::Object:
			{
				if (m_Object == other.m_Object)
					return true;

				// Text is interned, so two equal texts are always the same object.
				// Vectors are compared by their components.
				if (m_Object->GetType() == ObjectType::Vector && other.m_Object->GetType() == ObjectType::Vector)
				{
					const Vector* a = static_cast<const Vector*>(m_Object);
					const Vector* b = static_cast<const Vector*>(other.m_Object);
					return a->X == b->X && a->Y == b->Y && a->Z == b->Z;
				}
				// Two wrappers of the same C++ object are equal when the type says so.
				if (m_Object->GetType() == ObjectType::NativeObject && other.m_Object->GetType() == ObjectType::NativeObject)
				{
					const NativeObject* a = static_cast<const NativeObject*>(m_Object);
					const NativeObject* b = static_cast<const NativeObject*>(other.m_Object);
					return a->Type == b->Type && a->Type->Equals && a->Type->Equals(a->Data, b->Data);
				}
				return false;
			}
		}
		return false;
	}

	bool ContentsEqual(const Value& a, const Value& b)
	{
		// Compare with a work list instead of recursion, so deeply nested data cannot overflow the C++
		// stack. A pair of containers already being compared counts as equal, which ends cycles.
		std::vector<std::pair<Value, Value>> work = { { a, b } };
		std::set<std::pair<const Object*, const Object*>> seen;
		while (!work.empty())
		{
			auto [x, y] = work.back();
			work.pop_back();
			if (x == y)
				continue;

			const bool lists = IsObjectType(x, ObjectType::List) && IsObjectType(y, ObjectType::List);
			const bool maps = IsObjectType(x, ObjectType::Map) && IsObjectType(y, ObjectType::Map);
			if (!lists && !maps)
				return false;
			if (!seen.insert({ x.AsObject(), y.AsObject() }).second)
				continue;

			if (lists)
			{
				const std::vector<Value>& left = static_cast<const List*>(x.AsObject())->Items;
				const std::vector<Value>& right = static_cast<const List*>(y.AsObject())->Items;
				if (left.size() != right.size())
					return false;
				for (size_t i = 0; i < left.size(); i++)
					work.push_back({ left[i], right[i] });
			}
			else
			{
				const Map* left = static_cast<const Map*>(x.AsObject());
				const Map* right = static_cast<const Map*>(y.AsObject());
				if (left->Size() != right->Size())
					return false;
				for (const Map::Entry& entry : left->Entries)
				{
					Value other;
					if (!right->Get(entry.Key, other))
						return false;
					work.push_back({ entry.Item, other });
				}
			}
		}
		return true;
	}

	static size_t HashDouble(double number)
	{
		// Make 0 and -0 hash the same because they compare equal.
		if (number == 0.0)
			number = 0.0;
		return std::hash<double>{}(number);
	}

	size_t ValueHash::operator()(const Value& value) const
	{
		switch (value.GetType())
		{
			case ValueType::Nothing: return 0x9e3779b9u;
			case ValueType::Boolean: return value.AsBool() ? 1231u : 1237u;
			case ValueType::Number:  return HashDouble(value.AsNumber());
			case ValueType::Object:
			{
				Object* object = value.AsObject();
				if (object->GetType() == ObjectType::String)
					return static_cast<String*>(object)->Hash;
				if (object->GetType() == ObjectType::Vector)
				{
					const Vector* v = static_cast<const Vector*>(object);
					size_t hash = HashDouble(v->X);
					hash ^= HashDouble(v->Y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
					hash ^= HashDouble(v->Z) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
					return hash;
				}
				if (object->GetType() == ObjectType::NativeObject)
				{
					const NativeObject* native = static_cast<const NativeObject*>(object);
					if (native->Type->Equals && native->Type->Hash)
						return native->Type->Hash(native->Data);
				}
				return std::hash<const void*>{}(object);
			}
		}
		return 0;
	}

	std::string FormatNumber(double number)
	{
		if (std::isnan(number))
			return "not a number";
		if (std::isinf(number))
			return number > 0 ? "infinity" : "-infinity";

		char buffer[64];
		if (number == std::floor(number) && std::fabs(number) < 1e15)
		{
			std::snprintf(buffer, sizeof(buffer), "%.0f", number);
			// Avoid printing "-0".
			if (buffer[0] == '-' && buffer[1] == '0' && buffer[2] == '\0')
				return "0";
		}
		else
		{
			std::snprintf(buffer, sizeof(buffer), "%.14g", number);
		}
		return buffer;
	}

}
