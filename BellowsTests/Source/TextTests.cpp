#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Log.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"

#include <algorithm>
#include <string>

using namespace Bellows;

namespace {

	// Inter ships next to the programs; one copy serves every test (building an atlas takes a moment).
	const Ref<Font>& Inter()
	{
		static Ref<Font> font = Font::LoadFromFile(Platform::GetExecutableDirectory() / "Resources" / "Fonts" / "Inter" / "Inter-Regular.ttf");
		REQUIRE(font);
		return font;
	}

	// The atlas texel at a glyph's UV, as RGBA bytes.
	const uint8_t* Texel(const Font& font, glm::vec2 uv)
	{
		const uint32_t x = std::min(static_cast<uint32_t>(uv.x * static_cast<float>(font.GetAtlasWidth())), font.GetAtlasWidth() - 1);
		const uint32_t y = std::min(static_cast<uint32_t>(uv.y * static_cast<float>(font.GetAtlasHeight())), font.GetAtlasHeight() - 1);
		return &font.GetAtlasPixels()[(static_cast<size_t>(y) * font.GetAtlasWidth() + x) * 4];
	}

	// The median of the three distance channels, as the text shader reads it: above 128 is inside.
	int Median(const uint8_t* texel)
	{
		return std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2]));
	}

	TextLayoutSettings Settings(float boxWidth = 0.0f, float boxHeight = 0.0f)
	{
		TextLayoutSettings settings;
		settings.Size = 20.0f;
		settings.BoxSize = glm::vec2(boxWidth, boxHeight);
		return settings;
	}

	std::string LineText(std::string_view text, const TextLine& line) { return std::string(text.substr(line.Begin, line.End - line.Begin)); }

}

TEST_CASE("A font loads its metrics, characters and kerning")
{
	const Font& font = *Inter();
	CHECK(font.GetAscender() > 0.5f);
	CHECK(font.GetDescender() < 0.0f);
	CHECK(font.GetLineHeight() >= font.GetAscender() - font.GetDescender());

	for (uint32_t codepoint : { uint32_t('A'), uint32_t('z'), uint32_t('7'), 0xE9u, 0x2026u, 0x20ACu, 0x201Cu })
		CHECK_MESSAGE(font.HasGlyph(codepoint), "U+", codepoint);
	CHECK(font.GetGlyph('A').Advance > 0.3f);
	CHECK(font.GetGlyph('A').Visible);
	CHECK_FALSE(font.GetGlyph(' ').Visible);
	CHECK(font.GetGlyph(' ').Advance > 0.0f);

	// Inter pulls A and V together.
	CHECK(font.GetKerning('A', 'V') < 0.0f);
	CHECK(font.GetKerning('A', 'V') == font.GetKerning('A', 'V'));
	CHECK(font.GetKerning('H', 'H') == 0.0f);
}

TEST_CASE("A font's atlas holds distance fields with inside and outside the right way round")
{
	const Font& font = *Inter();
	const uint32_t width = font.GetAtlasWidth();
	CHECK(width >= 256);
	CHECK(width <= 4096);
	CHECK((width & (width - 1)) == 0);
	CHECK(font.GetAtlasHeight() <= width);
	CHECK((font.GetAtlasHeight() & (font.GetAtlasHeight() - 1)) == 0);
	CHECK(font.GetAtlasPixels().size() == static_cast<size_t>(width) * font.GetAtlasHeight() * 4);

	// The middle of an 'I' is inside the outline; the corners of its cell are outside it.
	const FontGlyph& glyph = font.GetGlyph('I');
	REQUIRE(glyph.Visible);
	CHECK(glyph.UVMax.x > glyph.UVMin.x);
	CHECK(glyph.UVMax.y > glyph.UVMin.y);
	const glm::vec2 middle = (glyph.UVMin + glyph.UVMax) * 0.5f;
	CHECK(Median(Texel(font, middle)) > 160);
	CHECK(Texel(font, middle)[3] > 128);	// The true distance agrees
	CHECK(Median(Texel(font, glyph.UVMin)) < 96);
	CHECK(Median(Texel(font, glm::vec2(glyph.UVMax.x - 0.5f / width, glyph.UVMin.y))) < 96);

	// The 'I' stands on the baseline and reaches about cap height above it.
	CHECK(glyph.PlaneMin.y < 0.0f);
	CHECK(glyph.PlaneMax.y > 0.6f);

	// Saved for a look: TestOutput/FontAtlas.png.
	Image atlas;
	atlas.Width = font.GetAtlasWidth();
	atlas.Height = font.GetAtlasHeight();
	atlas.Pixels = font.GetAtlasPixels();
	CHECK(atlas.SavePNG(BellowsTests::GetTestOutputDirectory() / "FontAtlas.png"));
}

TEST_CASE("A character missing from the atlas draws the missing glyph and is reported once")
{
	const Font& font = *Inter();
	const uint64_t before = Log::GetLatestSequence();
	const FontGlyph& first = font.GetGlyph(0x4E2D);	// A CJK character, outside the default ranges
	const FontGlyph& second = font.GetGlyph(0x4E2D);
	CHECK(&first == &second);
	CHECK(first.Advance > 0.0f);

	size_t warnings = 0;
	for (const LogMessage& message : Log::GetMessagesSince(before))
		warnings += message.Text.find("U+4E2D") != std::string::npos ? 1 : 0;
	CHECK(warnings == 1);
}

TEST_CASE("Fonts that can't be read are errors, not crashes")
{
	CHECK_FALSE(Font::LoadFromFile(Platform::GetExecutableDirectory() / "no-such-font.ttf"));
	CHECK_FALSE(Font::LoadFromMemory({}, "empty"));
	CHECK_FALSE(Font::LoadFromMemory({ 'n', 'o', 't', ' ', 'a', ' ', 'f', 'o', 'n', 't' }, "text"));
}

TEST_CASE("UTF-8 decoding takes valid characters and replaces broken ones")
{
	const auto decode = [](std::string_view text)
	{
		std::vector<uint32_t> codepoints;
		for (size_t offset = 0; offset < text.size();)
			codepoints.push_back(DecodeUtf8(text, offset));
		return codepoints;
	};
	CHECK(decode("A") == std::vector<uint32_t>{ 'A' });
	CHECK(decode("\xC3\xA9") == std::vector<uint32_t>{ 0xE9 });					// é
	CHECK(decode("\xE2\x82\xAC") == std::vector<uint32_t>{ 0x20AC });				// €
	CHECK(decode("\xF0\x9F\x98\x80") == std::vector<uint32_t>{ 0x1F600 });			// An emoji
	CHECK(decode("\xC3") == std::vector<uint32_t>{ 0xFFFD });						// Cut short
	CHECK(decode("\xC3" "A") == std::vector<uint32_t>{ 0xFFFD, 'A' });				// Not a continuation: the A stays
	CHECK(decode("\xC0\xAF") == std::vector<uint32_t>{ 0xFFFD });					// Overlong
	CHECK(decode("\xED\xA0\x80") == std::vector<uint32_t>{ 0xFFFD });				// A surrogate
	CHECK(decode("\xFF" "B") == std::vector<uint32_t>{ 0xFFFD, 'B' });
}

TEST_CASE("Text lays out on one line with kerning and its measured size")
{
	const Font& font = *Inter();
	const TextLayout layout = LayoutText(font, "AV", Settings());
	REQUIRE(layout.Lines.size() == 1);
	REQUIRE(layout.Quads.size() == 2);
	const float expected = (font.GetGlyph('A').Advance + font.GetKerning('A', 'V') + font.GetGlyph('V').Advance) * 20.0f;
	CHECK(layout.Size.x == doctest::Approx(expected));
	CHECK(layout.Size.y == doctest::Approx(font.GetLineHeight() * 20.0f));
	CHECK(layout.Lines[0].Baseline == doctest::Approx(font.GetAscender() * 20.0f));
	CHECK(MeasureText(font, "AV", Settings()) == layout.Size);

	// Quads run left to right, above the baseline for capitals.
	CHECK(layout.Quads[0].Min.x < layout.Quads[1].Min.x);
	CHECK(layout.Quads[0].Min.y < layout.Lines[0].Baseline);

	const TextLayout empty = LayoutText(font, "", Settings());
	CHECK(empty.Lines.size() == 1);
	CHECK(empty.Quads.empty());
	CHECK(empty.Size.x == 0.0f);
}

TEST_CASE("Text wraps at spaces, breaks words too long for a line, and keeps new lines")
{
	const Font& font = *Inter();
	const std::string text = "hello world";
	const float helloWidth = MeasureText(font, "hello", Settings()).x;
	const float wordWidth = std::max(helloWidth, MeasureText(font, "world", Settings()).x);
	const TextLayout wrapped = LayoutText(font, text, Settings(wordWidth + 1.0f));
	REQUIRE(wrapped.Lines.size() == 2);
	CHECK(LineText(text, wrapped.Lines[0]) == "hello ");
	CHECK(LineText(text, wrapped.Lines[1]) == "world");
	CHECK(wrapped.Lines[0].Width == doctest::Approx(helloWidth));	// The space at the end doesn't count
	CHECK(wrapped.Lines[1].Baseline == doctest::Approx(wrapped.Lines[0].Baseline + font.GetLineHeight() * 20.0f));

	TextLayoutSettings noWrap = Settings(wordWidth + 1.0f);
	noWrap.Wrap = false;
	CHECK(LayoutText(font, text, noWrap).Lines.size() == 1);

	const std::string longWord = "abcdefghijklmnopqrstuvwxyz";
	const TextLayout broken = LayoutText(font, longWord, Settings(helloWidth));
	CHECK(broken.Lines.size() > 1);
	for (const TextLine& line : broken.Lines)
		CHECK(line.Width <= helloWidth + 0.001f);
	std::string joined;
	for (const TextLine& line : broken.Lines)
		joined += LineText(longWord, line);
	CHECK(joined == longWord);

	const std::string paragraphs = "one\n\ntwo\r\n";
	const TextLayout lines = LayoutText(font, paragraphs, Settings());
	REQUIRE(lines.Lines.size() == 4);
	CHECK(LineText(paragraphs, lines.Lines[0]) == "one");
	CHECK(lines.Lines[1].Width == 0.0f);
	CHECK(LineText(paragraphs, lines.Lines[2]) == "two");

	// A tab is four spaces wide.
	CHECK(MeasureText(font, "a\tb", Settings()).x == doctest::Approx(MeasureText(font, "a    b", Settings()).x).epsilon(0.02));
}

TEST_CASE("Text aligns in its box")
{
	const Font& font = *Inter();
	TextLayoutSettings settings = Settings(300.0f, 200.0f);
	const float width = MeasureText(font, "Start", settings).x;

	settings.Alignment = TextAlignment::Center;
	settings.VerticalAlignment = TextVerticalAlignment::Middle;
	TextLayout layout = LayoutText(font, "Start", settings);
	CHECK(layout.Lines[0].Left == doctest::Approx((300.0f - width) * 0.5f));
	const float lineHeight = font.GetLineHeight() * 20.0f;
	CHECK(layout.Lines[0].Baseline == doctest::Approx((200.0f - lineHeight) * 0.5f + font.GetAscender() * 20.0f));

	settings.Alignment = TextAlignment::Right;
	settings.VerticalAlignment = TextVerticalAlignment::Bottom;
	layout = LayoutText(font, "Start", settings);
	CHECK(layout.Lines[0].Left + layout.Lines[0].Width == doctest::Approx(300.0f));
	CHECK(layout.Lines[0].Baseline == doctest::Approx(200.0f - lineHeight + font.GetAscender() * 20.0f));

	// Without a box width, lines align against the widest one.
	settings.BoxSize = glm::vec2(0.0f);
	settings.Alignment = TextAlignment::Right;
	layout = LayoutText(font, "a\nwide line", settings);
	CHECK(layout.Lines[0].Left + layout.Lines[0].Width == doctest::Approx(layout.Lines[1].Left + layout.Lines[1].Width));
}

TEST_CASE("Text that doesn't fit ends with an ellipsis")
{
	const Font& font = *Inter();
	const std::string text = "The quick brown fox jumps over the lazy dog";
	const float lineHeight = font.GetLineHeight() * 20.0f;

	// One line high: the first line ends in "…" and the rest is left out.
	TextLayoutSettings settings = Settings(150.0f, lineHeight * 1.5f);
	settings.Overflow = TextOverflow::Ellipsis;
	TextLayout layout = LayoutText(font, text, settings);
	REQUIRE(layout.Lines.size() == 1);
	CHECK(layout.Truncated);
	CHECK(layout.Lines[0].Width <= 150.0f + 0.001f);
	const FontGlyph& ellipsis = font.GetGlyph(0x2026);
	CHECK(layout.Quads.back().UVMin == ellipsis.UVMin);

	// Without wrapping, a line too wide is shortened the same way.
	settings.Wrap = false;
	settings.BoxSize.y = 0.0f;
	layout = LayoutText(font, text, settings);
	CHECK(layout.Truncated);
	CHECK(layout.Lines[0].Width <= 150.0f + 0.001f);

	// Text that fits is left alone, and so is text with Visible overflow.
	CHECK_FALSE(LayoutText(font, "fox", settings).Truncated);
	settings.Overflow = TextOverflow::Visible;
	layout = LayoutText(font, text, settings);
	CHECK_FALSE(layout.Truncated);
	CHECK(layout.Lines[0].Width > 150.0f);
}
