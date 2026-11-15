#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ForgeScript {

	class Object;

	// The four basic shapes a ForgeScript value can take.
	// Everything that is not nothing, true/false or a number lives on the heap as an Object.
	enum class ValueType : uint8_t
	{
		Nothing = 0,
		Boolean,
		Number,
		Object
	};

	// A Value is a small, copyable "box" that holds any ForgeScript value.
	// It is 16 bytes: a type tag plus either a bool, a double or an Object pointer.
	class Value
	{
	public:
		Value() = default;

		static Value MakeNothing() { return Value(); }
		static Value FromBool(bool value) { Value v; v.m_Type = ValueType::Boolean; v.m_Bool = value; return v; }
		static Value FromNumber(double value) { Value v; v.m_Type = ValueType::Number; v.m_Number = value; return v; }
		static Value FromObject(Object* object) { Value v; v.m_Type = ValueType::Object; v.m_Object = object; return v; }

		ValueType GetType() const { return m_Type; }

		bool IsNothing() const { return m_Type == ValueType::Nothing; }
		bool IsBool() const { return m_Type == ValueType::Boolean; }
		bool IsNumber() const { return m_Type == ValueType::Number; }
		bool IsObject() const { return m_Type == ValueType::Object; }

		bool AsBool() const { return m_Bool; }
		double AsNumber() const { return m_Number; }
		Object* AsObject() const { return m_Object; }

		// ForgeScript truthiness: only nothing and false are "false". Zero and empty text are true,
		// which keeps the rules short and predictable for beginners.
		bool IsTruthy() const
		{
			if (m_Type == ValueType::Nothing)
				return false;
			if (m_Type == ValueType::Boolean)
				return m_Bool;
			return true;
		}

		bool operator==(const Value& other) const;
		bool operator!=(const Value& other) const { return !(*this == other); }

	private:
		ValueType m_Type = ValueType::Nothing;
		union
		{
			bool m_Bool;
			double m_Number = 0.0;
			Object* m_Object;
		};
	};

	struct ValueHash
	{
		size_t operator()(const Value& value) const;
	};

	// Formats a number the way ForgeScript prints it: whole numbers have no ".0".
	std::string FormatNumber(double number);

}
