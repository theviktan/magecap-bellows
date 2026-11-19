#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Bellows {

	class Font;

	enum class TextAlignment : uint8_t { Left = 0, Center, Right };
	enum class TextVerticalAlignment : uint8_t { Top = 0, Middle, Bottom };
	// What happens to text that doesn't fit its box: it runs past it, is cut at its edges when drawn,
	// or ends with "…" at the last line that fits.
	enum class TextOverflow : uint8_t { Visible = 0, Clip, Ellipsis };

	struct TextLayoutSettings
	{
		float Size = 32.0f;				// One em, in the output's units (pixels for screen text)
		float LineSpacing = 1.0f;		// A multiple of the font's line height
		// The box the text sits in. A width of 0 means no width (lines never wrap); a height of 0 means
		// no height (nothing overflows downward).
		glm::vec2 BoxSize = glm::vec2(0.0f);
		bool Wrap = true;				// Wrap at spaces (or inside a word too long for a line)
		TextAlignment Alignment = TextAlignment::Left;
		TextVerticalAlignment VerticalAlignment = TextVerticalAlignment::Top;
		TextOverflow Overflow = TextOverflow::Visible;
	};

	// One glyph to draw. Positions have (0, 0) at the box's top-left with y pointing down, in the
	// settings' units; UVs are in the font's atlas.
	struct TextGlyphQuad
	{
		glm::vec2 Min = glm::vec2(0.0f);
		glm::vec2 Max = glm::vec2(0.0f);
		glm::vec2 UVMin = glm::vec2(0.0f);
		glm::vec2 UVMax = glm::vec2(0.0f);
		uint32_t Line = 0;
	};

	struct TextLine
	{
		float Left = 0.0f;			// Where the line starts after alignment
		float Width = 0.0f;			// Its width, not counting spaces at its end
		float Baseline = 0.0f;		// Distance from the box's top to its baseline
		size_t Begin = 0;			// Byte range of the text it shows (without the ellipsis)
		size_t End = 0;
	};

	struct TextLayout
	{
		std::vector<TextGlyphQuad> Quads;
		std::vector<TextLine> Lines;
		glm::vec2 Size = glm::vec2(0.0f);	// The widest line, and the height of all lines
		bool Truncated = false;				// Text was left out to make it fit (Ellipsis)
	};

	// Lays out UTF-8 text (invalid bytes show as U+FFFD; '\n' starts a line, '\t' is four spaces).
	TextLayout LayoutText(const Font& font, std::string_view text, const TextLayoutSettings& settings);
	// The size LayoutText would give, for sizing boxes to their text.
	glm::vec2 MeasureText(const Font& font, std::string_view text, const TextLayoutSettings& settings);

	const char* TextAlignmentToString(TextAlignment alignment);
	TextAlignment TextAlignmentFromString(std::string_view text);	// Unknown text: Left
	const char* TextVerticalAlignmentToString(TextVerticalAlignment alignment);
	TextVerticalAlignment TextVerticalAlignmentFromString(std::string_view text);	// Unknown text: Top
	const char* TextOverflowToString(TextOverflow overflow);
	TextOverflow TextOverflowFromString(std::string_view text);	// Unknown text: Visible

	// Decodes one UTF-8 character at 'offset' and moves past it; U+FFFD for an invalid sequence.
	uint32_t DecodeUtf8(std::string_view text, size_t& offset);

}
