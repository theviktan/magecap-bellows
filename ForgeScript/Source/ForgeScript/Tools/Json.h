#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ForgeScript {

	// A small JSON value for the language server and other tools, so the language needs no third-party
	// code. Objects keep their keys in the order they were added.
	class Json
	{
	public:
		using Array = std::vector<Json>;
		using Object = std::vector<std::pair<std::string, Json>>;

		Json() = default;
		Json(std::nullptr_t) {}
		Json(bool value) : m_Value(value) {}
		Json(double value) : m_Value(value) {}
		Json(int value) : m_Value(static_cast<double>(value)) {}
		Json(long long value) : m_Value(static_cast<double>(value)) {}
		Json(size_t value) : m_Value(static_cast<double>(value)) {}
		Json(const char* value) : m_Value(std::string(value)) {}
		Json(std::string value) : m_Value(std::move(value)) {}
		Json(Array value) : m_Value(std::move(value)) {}
		Json(Object value) : m_Value(std::move(value)) {}

		bool IsNull() const { return std::holds_alternative<std::monostate>(m_Value); }
		bool IsBool() const { return std::holds_alternative<bool>(m_Value); }
		bool IsNumber() const { return std::holds_alternative<double>(m_Value); }
		bool IsString() const { return std::holds_alternative<std::string>(m_Value); }
		bool IsArray() const { return std::holds_alternative<Array>(m_Value); }
		bool IsObject() const { return std::holds_alternative<Object>(m_Value); }

		// These give back the value, or the fallback when the type is different.
		bool AsBool(bool fallback = false) const { return IsBool() ? std::get<bool>(m_Value) : fallback; }
		double AsNumber(double fallback = 0.0) const { return IsNumber() ? std::get<double>(m_Value) : fallback; }
		const std::string& AsString() const;
		const Array& AsArray() const;
		const Object& AsObject() const;

		// Object member, or nullptr when this is not an object or has no such key.
		const Json* Find(std::string_view key) const;
		// Shortcuts for nested lookups that may be missing: Get("textDocument").GetString("uri").
		const Json& Get(std::string_view key) const;
		const std::string& GetString(std::string_view key) const { return Get(key).AsString(); }

		// Adds or replaces an object member (turns a null value into an object first).
		Json& Set(const std::string& key, Json value);
		// Adds an array item (turns a null value into an array first).
		Json& Add(Json value);

		bool operator==(const Json& other) const { return m_Value == other.m_Value; }

		// Compact JSON text. Whole numbers are written without a decimal point.
		std::string Dump() const;

		// Reads JSON text. Returns false with a message (and the position) when it is not valid JSON.
		static bool Parse(std::string_view text, Json& outValue, std::string& outError);

	private:
		void DumpTo(std::string& out) const;

	private:
		std::variant<std::monostate, bool, double, std::string, Array, Object> m_Value;
	};

}
