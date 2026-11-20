#include "bwpch.h"
#include "Bellows/UI/UITextEdit.h"

#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"

namespace Bellows {

	namespace {

		bool IsContinuationByte(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

		// Word characters are letters, digits, '_' and anything beyond ASCII (accented letters).
		bool IsWordCharacter(std::string_view text, size_t offset)
		{
			const auto c = static_cast<uint8_t>(text[offset]);
			return c >= 0x80 || std::isalnum(c) || c == '_';
		}

		// Ctrl+arrows and Ctrl+Backspace treat a run of punctuation like a word, and skip spaces.
		bool IsSpaceCharacter(std::string_view text, size_t offset)
		{
			const char c = text[offset];
			return c == ' ' || c == '\t' || c == '\n';
		}

	}

	size_t ClampUITextOffset(std::string_view text, size_t offset)
	{
		offset = std::min(offset, text.size());
		while (offset > 0 && offset < text.size() && IsContinuationByte(text[offset]))
			offset--;
		return offset;
	}

	size_t NextUICharacter(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		if (offset >= text.size())
			return text.size();
		offset++;
		while (offset < text.size() && IsContinuationByte(text[offset]))
			offset++;
		return offset;
	}

	size_t PreviousUICharacter(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		if (offset == 0)
			return 0;
		offset--;
		while (offset > 0 && IsContinuationByte(text[offset]))
			offset--;
		return offset;
	}

	size_t NextUIWord(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		while (offset < text.size() && IsSpaceCharacter(text, offset))
			offset = NextUICharacter(text, offset);
		if (offset >= text.size())
			return text.size();
		const bool word = IsWordCharacter(text, offset);
		while (offset < text.size() && !IsSpaceCharacter(text, offset) && IsWordCharacter(text, offset) == word)
			offset = NextUICharacter(text, offset);
		return offset;
	}

	size_t PreviousUIWord(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		while (offset > 0 && IsSpaceCharacter(text, PreviousUICharacter(text, offset)))
			offset = PreviousUICharacter(text, offset);
		if (offset == 0)
			return 0;
		const bool word = IsWordCharacter(text, PreviousUICharacter(text, offset));
		while (offset > 0)
		{
			const size_t previous = PreviousUICharacter(text, offset);
			if (IsSpaceCharacter(text, previous) || IsWordCharacter(text, previous) != word)
				break;
			offset = previous;
		}
		return offset;
	}

	size_t UILineStart(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		const size_t newline = offset == 0 ? std::string_view::npos : text.rfind('\n', offset - 1);
		return newline == std::string_view::npos ? 0 : newline + 1;
	}

	size_t UILineEnd(std::string_view text, size_t offset)
	{
		offset = ClampUITextOffset(text, offset);
		const size_t newline = text.find('\n', offset);
		return newline == std::string_view::npos ? text.size() : newline;
	}

	size_t CountUICharacters(std::string_view text)
	{
		return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return !IsContinuationByte(c); }));
	}

	size_t UICharacterOffset(std::string_view text, size_t index)
	{
		size_t offset = 0;
		for (size_t i = 0; i < index && offset < text.size(); i++)
			offset = NextUICharacter(text, offset);
		return offset;
	}

	size_t UICharacterIndex(std::string_view text, size_t offset)
	{
		return CountUICharacters(text.substr(0, ClampUITextOffset(text, offset)));
	}

	bool InsertUIText(std::string& text, UITextSelection& selection, std::string_view insert, bool multiLine, int maxLength)
	{
		// Keep what may be typed into this field, decoding so invalid bytes become U+FFFD.
		std::string clean;
		for (size_t offset = 0; offset < insert.size(); )
		{
			const uint32_t codepoint = DecodeUtf8(insert, offset);
			if (codepoint == '\r')
				continue;
			if (codepoint == '\n' ? !multiLine : (codepoint < 0x20 || codepoint == 0x7F))
				continue;
			AppendUtf8(clean, codepoint);
		}

		const size_t begin = ClampUITextOffset(text, selection.Begin());
		const size_t end = ClampUITextOffset(text, selection.End());
		if (maxLength > 0)
		{
			const size_t kept = CountUICharacters(text) - CountUICharacters(std::string_view(text).substr(begin, end - begin));
			const size_t room = static_cast<size_t>(maxLength) > kept ? static_cast<size_t>(maxLength) - kept : 0;
			clean.resize(UICharacterOffset(clean, room));
		}
		if (clean.empty() && begin == end)
			return false;

		text.replace(begin, end - begin, clean);
		selection.CollapseTo(begin + clean.size());
		return true;
	}

	bool EraseUIText(std::string& text, UITextSelection& selection, bool forward, bool word)
	{
		size_t begin = ClampUITextOffset(text, selection.Begin());
		size_t end = ClampUITextOffset(text, selection.End());
		if (begin == end)
		{
			if (forward)
				end = word ? NextUIWord(text, end) : NextUICharacter(text, end);
			else
				begin = word ? PreviousUIWord(text, begin) : PreviousUICharacter(text, begin);
		}
		if (begin == end)
			return false;
		text.erase(begin, end - begin);
		selection.CollapseTo(begin);
		return true;
	}

	std::string MaskUIText(std::string_view text)
	{
		std::string masked;
		const size_t count = CountUICharacters(text);
		masked.reserve(count * 3);
		for (size_t i = 0; i < count; i++)
			masked += "\xE2\x80\xA2";
		return masked;
	}

	std::vector<std::pair<size_t, size_t>> WrapUILine(std::string_view line, const std::vector<float>& stops, float width)
	{
		const size_t count = stops.empty() ? 0 : stops.size() - 1;
		std::vector<std::pair<size_t, size_t>> rows;
		if (count == 0 || width <= 0.0f || stops.back() - stops.front() <= width)
		{
			rows.emplace_back(0, count);
			return rows;
		}

		// Each character's first byte, to find the spaces among them.
		std::vector<bool> isSpace(count, false);
		for (size_t index = 0, offset = 0; index < count && offset < line.size(); index++, offset = NextUICharacter(line, offset))
			isSpace[index] = line[offset] == ' ';

		size_t first = 0;
		while (first < count)
		{
			// The most characters that fit, then back to just after the last space among them.
			size_t end = first + 1;
			while (end < count && stops[end + 1] - stops[first] <= width)
				end++;
			if (end < count)
			{
				// A space right after the fit also ends the row (it takes no room there).
				if (isSpace[end])
					end++;
				else
				{
					size_t breakAt = end;
					while (breakAt > first + 1 && !isSpace[breakAt - 1])
						breakAt--;
					if (breakAt > first + 1)
						end = breakAt;		// Otherwise no space to break at: the word is cut
				}
			}
			rows.emplace_back(first, end);
			first = end;
		}
		return rows;
	}

	std::vector<float> ComputeUICaretStops(const Font& font, std::string_view line, float size)
	{
		std::vector<uint32_t> codepoints;
		for (size_t offset = 0; offset < line.size(); )
			codepoints.push_back(DecodeUtf8(line, offset));

		// The same pen steps as LayoutText: each character's advance, then the kerning to the next.
		std::vector<float> stops { 0.0f };
		stops.reserve(codepoints.size() + 1);
		float pen = 0.0f;
		for (size_t i = 0; i < codepoints.size(); i++)
		{
			const uint32_t codepoint = codepoints[i];
			pen += codepoint == '\t' ? font.GetGlyph(' ').Advance * 4.0f * size : font.GetGlyph(codepoint).Advance * size;
			if (i + 1 < codepoints.size())
				pen += font.GetKerning(codepoint, codepoints[i + 1]) * size;
			stops.push_back(pen);
		}
		return stops;
	}

	size_t NearestUICaretStop(const std::vector<float>& stops, float x)
	{
		size_t best = 0;
		for (size_t i = 1; i < stops.size(); i++)
		{
			if (std::abs(stops[i] - x) < std::abs(stops[best] - x))
				best = i;
		}
		return best;
	}

}
