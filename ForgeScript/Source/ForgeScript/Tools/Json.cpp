#include "ForgeScript/Tools/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ForgeScript {

	namespace {

		const std::string c_EmptyString;
		const Json::Array c_EmptyArray;
		const Json::Object c_EmptyObject;
		const Json c_Null;

		// Deeper input is refused, so a hostile message cannot exhaust the C++ stack.
		constexpr int c_MaxDepth = 256;

		void AppendUtf8(std::string& out, uint32_t codePoint)
		{
			if (codePoint < 0x80)
			{
				out += static_cast<char>(codePoint);
			}
			else if (codePoint < 0x800)
			{
				out += static_cast<char>(0xC0 | (codePoint >> 6));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else if (codePoint < 0x10000)
			{
				out += static_cast<char>(0xE0 | (codePoint >> 12));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (codePoint >> 18));
				out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
		}

		void DumpString(std::string& out, const std::string& text)
		{
			out += '"';
			for (unsigned char c : text)
			{
				switch (c)
				{
					case '"': out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n"; break;
					case '\r': out += "\\r"; break;
					case '\t': out += "\\t"; break;
					case '\b': out += "\\b"; break;
					case '\f': out += "\\f"; break;
					default:
						if (c < 0x20)
						{
							char escaped[8];
							std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
							out += escaped;
						}
						else
						{
							out += static_cast<char>(c);
						}
				}
			}
			out += '"';
		}

		class Parser
		{
		public:
			explicit Parser(std::string_view text) : m_Text(text) {}

			bool ParseDocument(Json& outValue, std::string& outError)
			{
				SkipSpace();
				if (!ParseValue(outValue, 0))
				{
					outError = m_Error + " (at character " + std::to_string(m_Pos + 1) + ")";
					return false;
				}
				SkipSpace();
				if (m_Pos != m_Text.size())
				{
					outError = "Unexpected text after the JSON value (at character " + std::to_string(m_Pos + 1) + ")";
					return false;
				}
				return true;
			}

		private:
			bool Fail(const std::string& message)
			{
				m_Error = message;
				return false;
			}

			void SkipSpace()
			{
				while (m_Pos < m_Text.size() && (m_Text[m_Pos] == ' ' || m_Text[m_Pos] == '\t' || m_Text[m_Pos] == '\n' || m_Text[m_Pos] == '\r'))
					m_Pos++;
			}

			bool Literal(std::string_view word)
			{
				if (m_Text.substr(m_Pos, word.size()) != word)
					return false;
				m_Pos += word.size();
				return true;
			}

			bool ParseValue(Json& out, int depth)
			{
				if (depth > c_MaxDepth)
					return Fail("JSON nested too deeply");
				if (m_Pos >= m_Text.size())
					return Fail("Expected a value, but the text ended");

				const char c = m_Text[m_Pos];
				if (c == '{')
					return ParseObject(out, depth);
				if (c == '[')
					return ParseArray(out, depth);
				if (c == '"')
				{
					std::string text;
					if (!ParseString(text))
						return false;
					out = Json(std::move(text));
					return true;
				}
				if (Literal("true")) { out = Json(true); return true; }
				if (Literal("false")) { out = Json(false); return true; }
				if (Literal("null")) { out = Json(); return true; }
				if (c == '-' || (c >= '0' && c <= '9'))
					return ParseNumber(out);
				return Fail(std::string("Unexpected character '") + c + "'");
			}

			bool ParseObject(Json& out, int depth)
			{
				m_Pos++;
				Json::Object members;
				SkipSpace();
				if (m_Pos < m_Text.size() && m_Text[m_Pos] == '}')
				{
					m_Pos++;
					out = Json(std::move(members));
					return true;
				}
				while (true)
				{
					SkipSpace();
					if (m_Pos >= m_Text.size() || m_Text[m_Pos] != '"')
						return Fail("Expected a key in quotes");
					std::string key;
					if (!ParseString(key))
						return false;
					SkipSpace();
					if (m_Pos >= m_Text.size() || m_Text[m_Pos] != ':')
						return Fail("Expected ':' after a key");
					m_Pos++;
					SkipSpace();
					Json value;
					if (!ParseValue(value, depth + 1))
						return false;
					members.emplace_back(std::move(key), std::move(value));
					SkipSpace();
					if (m_Pos < m_Text.size() && m_Text[m_Pos] == ',')
					{
						m_Pos++;
						continue;
					}
					if (m_Pos < m_Text.size() && m_Text[m_Pos] == '}')
					{
						m_Pos++;
						out = Json(std::move(members));
						return true;
					}
					return Fail("Expected ',' or '}' in an object");
				}
			}

			bool ParseArray(Json& out, int depth)
			{
				m_Pos++;
				Json::Array items;
				SkipSpace();
				if (m_Pos < m_Text.size() && m_Text[m_Pos] == ']')
				{
					m_Pos++;
					out = Json(std::move(items));
					return true;
				}
				while (true)
				{
					SkipSpace();
					Json value;
					if (!ParseValue(value, depth + 1))
						return false;
					items.push_back(std::move(value));
					SkipSpace();
					if (m_Pos < m_Text.size() && m_Text[m_Pos] == ',')
					{
						m_Pos++;
						continue;
					}
					if (m_Pos < m_Text.size() && m_Text[m_Pos] == ']')
					{
						m_Pos++;
						out = Json(std::move(items));
						return true;
					}
					return Fail("Expected ',' or ']' in an array");
				}
			}

			bool ParseHex4(uint32_t& out)
			{
				if (m_Pos + 4 > m_Text.size())
					return Fail("Expected four hex digits after \\u");
				out = 0;
				for (int i = 0; i < 4; i++)
				{
					const char c = m_Text[m_Pos++];
					out <<= 4;
					if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
					else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
					else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
					else return Fail("Expected four hex digits after \\u");
				}
				return true;
			}

			bool ParseString(std::string& out)
			{
				m_Pos++;
				while (m_Pos < m_Text.size())
				{
					const char c = m_Text[m_Pos++];
					if (c == '"')
						return true;
					if (static_cast<unsigned char>(c) < 0x20)
						return Fail("Control characters must be escaped in JSON text");
					if (c != '\\')
					{
						out += c;
						continue;
					}
					if (m_Pos >= m_Text.size())
						break;
					const char escape = m_Text[m_Pos++];
					switch (escape)
					{
						case '"': out += '"'; break;
						case '\\': out += '\\'; break;
						case '/': out += '/'; break;
						case 'b': out += '\b'; break;
						case 'f': out += '\f'; break;
						case 'n': out += '\n'; break;
						case 'r': out += '\r'; break;
						case 't': out += '\t'; break;
						case 'u':
						{
							uint32_t codePoint = 0;
							if (!ParseHex4(codePoint))
								return false;
							// Characters outside the first plane come as a pair of UTF-16 halves.
							if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
							{
								uint32_t low = 0;
								if (!Literal("\\u") || !ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
									return Fail("A \\u escape for half a character is missing its other half");
								codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
							}
							else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
							{
								return Fail("A \\u escape for half a character is missing its other half");
							}
							AppendUtf8(out, codePoint);
							break;
						}
						default:
							return Fail(std::string("Unknown escape '\\") + escape + "' in JSON text");
					}
				}
				return Fail("Text in quotes is missing its closing quote");
			}

			bool ParseNumber(Json& out)
			{
				const size_t start = m_Pos;
				if (m_Text[m_Pos] == '-')
					m_Pos++;
				auto digits = [this]
				{
					const size_t from = m_Pos;
					while (m_Pos < m_Text.size() && m_Text[m_Pos] >= '0' && m_Text[m_Pos] <= '9')
						m_Pos++;
					return m_Pos > from;
				};
				if (!digits())
					return Fail("Expected digits in a number");
				if (m_Pos < m_Text.size() && m_Text[m_Pos] == '.')
				{
					m_Pos++;
					if (!digits())
						return Fail("Expected digits after the decimal point");
				}
				if (m_Pos < m_Text.size() && (m_Text[m_Pos] == 'e' || m_Text[m_Pos] == 'E'))
				{
					m_Pos++;
					if (m_Pos < m_Text.size() && (m_Text[m_Pos] == '+' || m_Text[m_Pos] == '-'))
						m_Pos++;
					if (!digits())
						return Fail("Expected digits in a number's exponent");
				}
				const std::string number(m_Text.substr(start, m_Pos - start));
				out = Json(std::strtod(number.c_str(), nullptr));
				return true;
			}

		private:
			std::string_view m_Text;
			size_t m_Pos = 0;
			std::string m_Error;
		};

	}

	const std::string& Json::AsString() const
	{
		return IsString() ? std::get<std::string>(m_Value) : c_EmptyString;
	}

	const Json::Array& Json::AsArray() const
	{
		return IsArray() ? std::get<Array>(m_Value) : c_EmptyArray;
	}

	const Json::Object& Json::AsObject() const
	{
		return IsObject() ? std::get<Object>(m_Value) : c_EmptyObject;
	}

	const Json* Json::Find(std::string_view key) const
	{
		for (const auto& [name, value] : AsObject())
		{
			if (name == key)
				return &value;
		}
		return nullptr;
	}

	const Json& Json::Get(std::string_view key) const
	{
		const Json* value = Find(key);
		return value ? *value : c_Null;
	}

	Json& Json::Set(const std::string& key, Json value)
	{
		if (!IsObject())
			m_Value = Object();
		Object& members = std::get<Object>(m_Value);
		for (auto& [name, existing] : members)
		{
			if (name == key)
			{
				existing = std::move(value);
				return *this;
			}
		}
		members.emplace_back(key, std::move(value));
		return *this;
	}

	Json& Json::Add(Json value)
	{
		if (!IsArray())
			m_Value = Array();
		std::get<Array>(m_Value).push_back(std::move(value));
		return *this;
	}

	std::string Json::Dump() const
	{
		std::string out;
		DumpTo(out);
		return out;
	}

	void Json::DumpTo(std::string& out) const
	{
		std::visit([&out](const auto& value)
		{
			using T = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<T, std::monostate>)
			{
				out += "null";
			}
			else if constexpr (std::is_same_v<T, bool>)
			{
				out += value ? "true" : "false";
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				if (!std::isfinite(value))
				{
					out += "null";
				}
				else if (value == std::floor(value) && std::abs(value) < 1e15)
				{
					out += std::to_string(static_cast<long long>(value));
				}
				else
				{
					char buffer[32];
					std::snprintf(buffer, sizeof(buffer), "%.17g", value);
					out += buffer;
				}
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				DumpString(out, value);
			}
			else if constexpr (std::is_same_v<T, Array>)
			{
				out += '[';
				for (size_t i = 0; i < value.size(); i++)
				{
					if (i > 0)
						out += ',';
					value[i].DumpTo(out);
				}
				out += ']';
			}
			else
			{
				out += '{';
				for (size_t i = 0; i < value.size(); i++)
				{
					if (i > 0)
						out += ',';
					DumpString(out, value[i].first);
					out += ':';
					value[i].second.DumpTo(out);
				}
				out += '}';
			}
		}, m_Value);
	}

	bool Json::Parse(std::string_view text, Json& outValue, std::string& outError)
	{
		Parser parser(text);
		return parser.ParseDocument(outValue, outError);
	}

}
