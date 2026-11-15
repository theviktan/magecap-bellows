#include "ForgeScript/Core/Value.h"

#include "ForgeScript/Core/Object.h"

#include <cmath>
#include <cstdio>
#include <functional>

namespace ForgeScript {

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
				return false;
			}
		}
		return false;
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
