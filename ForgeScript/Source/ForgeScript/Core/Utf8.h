#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ForgeScript::Utf8 {

	// ForgeScript text is UTF-8. These helpers count and index by characters, not bytes,
	// so "café" has a length of 4.

	inline size_t CharLength(unsigned char lead)
	{
		if (lead < 0x80) return 1;
		if ((lead >> 5) == 0x6) return 2;
		if ((lead >> 4) == 0xe) return 3;
		if ((lead >> 3) == 0x1e) return 4;
		return 1; // Invalid byte: treat it as one character so we never get stuck.
	}

	// The byte offset of the first byte that isn't part of a valid UTF-8 character, or npos when all of
	// text is valid. Overlong forms, surrogates and values past U+10FFFF count as invalid.
	inline size_t FirstInvalid(std::string_view text)
	{
		size_t i = 0;
		while (i < text.size())
		{
			const unsigned char lead = static_cast<unsigned char>(text[i]);
			if (lead < 0x80)
			{
				i++;
				continue;
			}
			const size_t length = CharLength(lead);
			if (length == 1 || i + length > text.size())
				return i;
			unsigned int code = lead & (0xFF >> (length + 1));
			for (size_t k = 1; k < length; k++)
			{
				const unsigned char next = static_cast<unsigned char>(text[i + k]);
				if ((next & 0xC0) != 0x80)
					return i;
				code = (code << 6) | (next & 0x3F);
			}
			static constexpr unsigned int c_Smallest[] = { 0, 0, 0x80, 0x800, 0x10000 };
			if (code < c_Smallest[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
				return i;
			i += length;
		}
		return std::string_view::npos;
	}

	inline size_t Length(std::string_view text)
	{
		size_t count = 0;
		for (size_t i = 0; i < text.size(); count++)
			i += CharLength(static_cast<unsigned char>(text[i]));
		return count;
	}

	// Byte offset of the character at (0 based) index, or text.size() when past the end.
	inline size_t Offset(std::string_view text, size_t index)
	{
		size_t i = 0;
		while (i < text.size() && index > 0)
		{
			i += CharLength(static_cast<unsigned char>(text[i]));
			index--;
		}
		return i < text.size() ? i : text.size();
	}

	// The character (as text) at a 0 based index.
	inline std::string CharAt(std::string_view text, size_t index)
	{
		size_t offset = Offset(text, index);
		if (offset >= text.size())
			return std::string();
		size_t length = CharLength(static_cast<unsigned char>(text[offset]));
		return std::string(text.substr(offset, length));
	}

	// Characters [start, start + count) as text (0 based).
	inline std::string Slice(std::string_view text, size_t start, size_t count)
	{
		size_t begin = Offset(text, start);
		size_t end = begin;
		while (end < text.size() && count > 0)
		{
			end += CharLength(static_cast<unsigned char>(text[end]));
			count--;
		}
		if (end > text.size())
			end = text.size();
		return std::string(text.substr(begin, end - begin));
	}

}
