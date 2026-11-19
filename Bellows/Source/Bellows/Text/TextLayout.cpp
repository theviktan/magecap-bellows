#include "bwpch.h"
#include "Bellows/Text/TextLayout.h"

#include "Bellows/Text/Font.h"

#include <algorithm>
#include <cmath>

namespace Bellows {

	namespace {

		constexpr uint32_t c_Replacement = 0xFFFD;
		constexpr uint32_t c_Ellipsis = 0x2026;
		constexpr int c_TabWidth = 4;

		struct Character
		{
			uint32_t Codepoint = 0;
			size_t Offset = 0;	// Where it starts in the text
			size_t Next = 0;	// Where the next one starts
		};

		bool IsSpace(uint32_t codepoint) { return codepoint == ' ' || codepoint == '\t' || codepoint == 0x00A0; }

		// Lines before alignment: which characters they hold.
		struct LineSpan
		{
			size_t First = 0;	// Index into the characters
			size_t Count = 0;
		};

		class Measurer
		{
		public:
			Measurer(const Font& font, float size) : m_Font(font), m_Size(size) {}

			float Advance(uint32_t codepoint) const
			{
				if (codepoint == '\t')
					return m_Font.GetGlyph(' ').Advance * c_TabWidth * m_Size;
				return m_Font.GetGlyph(codepoint).Advance * m_Size;
			}
			float Kerning(uint32_t left, uint32_t right) const { return m_Font.GetKerning(left, right) * m_Size; }

			// Width of characters [first, first + count), leaving out spaces at the end.
			float Width(const std::vector<Character>& characters, size_t first, size_t count) const
			{
				while (count > 0 && IsSpace(characters[first + count - 1].Codepoint))
					count--;
				float width = 0.0f;
				for (size_t i = 0; i < count; i++)
				{
					width += Advance(characters[first + i].Codepoint);
					if (i + 1 < count)
						width += Kerning(characters[first + i].Codepoint, characters[first + i + 1].Codepoint);
				}
				return width;
			}
		private:
			const Font& m_Font;
			float m_Size;
		};

		std::vector<Character> Decode(std::string_view text)
		{
			std::vector<Character> characters;
			size_t offset = 0;
			while (offset < text.size())
			{
				Character character;
				character.Offset = offset;
				character.Codepoint = DecodeUtf8(text, offset);
				character.Next = offset;
				if (character.Codepoint != '\r')
					characters.push_back(character);
			}
			return characters;
		}

		// Splits at '\n', then wraps each paragraph to the width at spaces, or inside a word that is
		// wider than a whole line.
		std::vector<LineSpan> BreakLines(const std::vector<Character>& characters, const Measurer& measure, float maxWidth)
		{
			std::vector<LineSpan> lines;
			size_t start = 0;
			while (true)
			{
				size_t end = start;
				while (end < characters.size() && characters[end].Codepoint != '\n')
					end++;

				size_t lineStart = start;
				while (maxWidth > 0.0f && lineStart < end)
				{
					// The longest run from lineStart that fits; prefer to end it after a space. Spaces at a
					// line's end don't count toward its width, so a space always fits.
					size_t fits = lineStart;
					size_t lastBreak = 0;	// One past the last space that fits, 0 when none
					float width = 0.0f;		// Of [lineStart, fits), spaces included
					while (fits < end)
					{
						const uint32_t codepoint = characters[fits].Codepoint;
						float next = width + measure.Advance(codepoint);
						if (fits > lineStart)
							next += measure.Kerning(characters[fits - 1].Codepoint, codepoint);
						if (!IsSpace(codepoint) && next > maxWidth)
							break;
						if (IsSpace(codepoint))
							lastBreak = fits + 1;
						width = next;
						fits++;
					}
					if (fits == end)
						break;
					// A space right after the run is a natural break too.
					if (IsSpace(characters[fits].Codepoint))
						lastBreak = fits + 1;
					size_t lineEnd = lastBreak > lineStart ? lastBreak : std::max(fits, lineStart + 1);
					lines.push_back({ lineStart, lineEnd - lineStart });
					lineStart = lineEnd;
					// Spaces where a line wrapped don't start the next one.
					while (lineStart < end && IsSpace(characters[lineStart].Codepoint))
						lineStart++;
				}
				lines.push_back({ lineStart, end - lineStart });

				if (end >= characters.size())
					break;
				start = end + 1;
			}
			return lines;
		}

	}

	uint32_t DecodeUtf8(std::string_view text, size_t& offset)
	{
		const auto byte = [&](size_t i) { return static_cast<uint8_t>(text[i]); };
		const uint8_t first = byte(offset);
		size_t length = 0;
		uint32_t codepoint = 0;
		if (first < 0x80)
		{
			offset++;
			return first;
		}
		if ((first & 0xE0) == 0xC0) { length = 2; codepoint = first & 0x1F; }
		else if ((first & 0xF0) == 0xE0) { length = 3; codepoint = first & 0x0F; }
		else if ((first & 0xF8) == 0xF0) { length = 4; codepoint = first & 0x07; }
		else
		{
			offset++;
			return c_Replacement;
		}
		if (offset + length > text.size())
		{
			offset = text.size();
			return c_Replacement;
		}
		for (size_t i = 1; i < length; i++)
		{
			const uint8_t next = byte(offset + i);
			if ((next & 0xC0) != 0x80)
			{
				offset += i;
				return c_Replacement;
			}
			codepoint = (codepoint << 6) | (next & 0x3F);
		}
		offset += length;
		// Overlong forms, surrogates and values past Unicode's end are not characters.
		static constexpr uint32_t c_Minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
		if (codepoint < c_Minimum[length] || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
			return c_Replacement;
		return codepoint;
	}

	TextLayout LayoutText(const Font& font, std::string_view text, const TextLayoutSettings& settings)
	{
		TextLayout layout;
		const float size = std::max(settings.Size, 0.0f);
		const Measurer measure(font, size);
		const std::vector<Character> characters = Decode(text);
		const float lineHeight = font.GetLineHeight() * size * settings.LineSpacing;
		const float boxWidth = std::max(settings.BoxSize.x, 0.0f);
		const float boxHeight = std::max(settings.BoxSize.y, 0.0f);

		std::vector<LineSpan> lines = BreakLines(characters, measure, settings.Wrap ? boxWidth : 0.0f);

		// Overflow: keep the lines that fit, and end the last one with an ellipsis when text was cut.
		std::vector<bool> ellipsis(lines.size(), false);
		if (settings.Overflow == TextOverflow::Ellipsis)
		{
			if (boxHeight > 0.0f && lineHeight > 0.0f)
			{
				const size_t fitting = std::max<size_t>(1, static_cast<size_t>(std::floor(boxHeight / lineHeight + 1e-4f)));
				if (lines.size() > fitting)
				{
					lines.resize(fitting);
					ellipsis.resize(fitting);
					ellipsis.back() = true;
				}
			}
			if (boxWidth > 0.0f)
			{
				for (size_t i = 0; i < lines.size(); i++)
				{
					if (measure.Width(characters, lines[i].First, lines[i].Count) > boxWidth)
						ellipsis[i] = true;
				}
			}
		}

		const bool hasEllipsisGlyph = font.HasGlyph(c_Ellipsis);
		const std::vector<uint32_t> ellipsisText = hasEllipsisGlyph ? std::vector<uint32_t>{ c_Ellipsis } : std::vector<uint32_t>{ '.', '.', '.' };
		float ellipsisWidth = 0.0f;
		for (uint32_t codepoint : ellipsisText)
			ellipsisWidth += measure.Advance(codepoint);

		for (size_t i = 0; i < lines.size(); i++)
		{
			LineSpan& span = lines[i];
			if (ellipsis[i])
			{
				// Drop characters until the rest and the ellipsis fit.
				while (span.Count > 0 && boxWidth > 0.0f && measure.Width(characters, span.First, span.Count) + ellipsisWidth > boxWidth)
					span.Count--;
				while (span.Count > 0 && IsSpace(characters[span.First + span.Count - 1].Codepoint))
					span.Count--;
				layout.Truncated = true;
			}

			TextLine line;
			line.Width = measure.Width(characters, span.First, span.Count) + (ellipsis[i] ? ellipsisWidth : 0.0f);
			line.Begin = span.Count > 0 ? characters[span.First].Offset : (span.First < characters.size() ? characters[span.First].Offset : text.size());
			line.End = span.Count > 0 ? characters[span.First + span.Count - 1].Next : line.Begin;
			line.Baseline = font.GetAscender() * size + lineHeight * static_cast<float>(i);
			layout.Lines.push_back(line);
			layout.Size.x = std::max(layout.Size.x, line.Width);
		}
		layout.Size.y = lineHeight * static_cast<float>(layout.Lines.size());

		// Align lines in the box (or in the widest line when there is no width), and the block of
		// lines in the box's height.
		const float alignWidth = boxWidth > 0.0f ? boxWidth : layout.Size.x;
		float top = 0.0f;
		if (boxHeight > 0.0f && settings.VerticalAlignment == TextVerticalAlignment::Middle)
			top = (boxHeight - layout.Size.y) * 0.5f;
		else if (boxHeight > 0.0f && settings.VerticalAlignment == TextVerticalAlignment::Bottom)
			top = boxHeight - layout.Size.y;

		for (size_t i = 0; i < lines.size(); i++)
		{
			TextLine& line = layout.Lines[i];
			if (settings.Alignment == TextAlignment::Center)
				line.Left = (alignWidth - line.Width) * 0.5f;
			else if (settings.Alignment == TextAlignment::Right)
				line.Left = alignWidth - line.Width;
			line.Baseline += top;

			std::vector<uint32_t> codepoints;
			for (size_t c = 0; c < lines[i].Count; c++)
				codepoints.push_back(characters[lines[i].First + c].Codepoint);
			if (ellipsis[i])
				codepoints.insert(codepoints.end(), ellipsisText.begin(), ellipsisText.end());

			float pen = line.Left;
			for (size_t c = 0; c < codepoints.size(); c++)
			{
				const uint32_t codepoint = codepoints[c];
				const FontGlyph& glyph = font.GetGlyph(codepoint == '\t' ? ' ' : codepoint);
				if (glyph.Visible)
				{
					TextGlyphQuad quad;
					quad.Min = glm::vec2(pen + glyph.PlaneMin.x * size, line.Baseline - glyph.PlaneMax.y * size);
					quad.Max = glm::vec2(pen + glyph.PlaneMax.x * size, line.Baseline - glyph.PlaneMin.y * size);
					quad.UVMin = glyph.UVMin;
					quad.UVMax = glyph.UVMax;
					quad.Line = static_cast<uint32_t>(i);
					layout.Quads.push_back(quad);
				}
				pen += measure.Advance(codepoint);
				if (c + 1 < codepoints.size())
					pen += measure.Kerning(codepoint, codepoints[c + 1]);
			}
		}
		return layout;
	}

	glm::vec2 MeasureText(const Font& font, std::string_view text, const TextLayoutSettings& settings)
	{
		return LayoutText(font, text, settings).Size;
	}

}
