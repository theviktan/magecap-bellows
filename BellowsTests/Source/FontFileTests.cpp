#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Buffer.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/FontFile.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	std::vector<uint8_t> ReadFont(const std::filesystem::path& path)
	{
		const Buffer buffer = FileSystem::ReadBinaryFile(path);
		REQUIRE(buffer.Size() > 0);
		return std::vector<uint8_t>(buffer.Data(), buffer.Data() + buffer.Size());
	}

	std::vector<uint8_t> Inter() { return ReadFont(BellowsTests::GetSourceDirectory() / "Resources" / "Fonts" / "Inter" / "Inter-Regular.ttf"); }
	std::vector<uint8_t> CFFTest() { return ReadFont(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "CFFTest.otf"); }

	uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t at)
	{
		return (static_cast<uint32_t>(bytes[at]) << 24) | (static_cast<uint32_t>(bytes[at + 1]) << 16) | (static_cast<uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
	}

	uint16_t ReadU16(const std::vector<uint8_t>& bytes, size_t at) { return static_cast<uint16_t>((bytes[at] << 8) | bytes[at + 1]); }

	void WriteU16(std::vector<uint8_t>& bytes, size_t at, uint16_t value)
	{
		bytes[at] = static_cast<uint8_t>(value >> 8);
		bytes[at + 1] = static_cast<uint8_t>(value);
	}

	void WriteU32(std::vector<uint8_t>& bytes, size_t at, uint32_t value)
	{
		for (int i = 0; i < 4; i++)
			bytes[at + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
	}

	// Where a table's directory record is in a (non-collection) font file.
	size_t FindTableRecord(const std::vector<uint8_t>& bytes, const char* tag)
	{
		const uint16_t count = ReadU16(bytes, 4);
		for (uint16_t i = 0; i < count; i++)
		{
			const size_t record = 12 + static_cast<size_t>(i) * 16;
			if (std::string(reinterpret_cast<const char*>(&bytes[record]), 4) == tag)
				return record;
		}
		FAIL("the font has no '", tag, "' table");
		return 0;
	}

	std::optional<FontFile> Parse(const std::vector<uint8_t>& bytes, std::string& error)
	{
		return FontFile::Parse(bytes.data(), bytes.size(), error);
	}

}

TEST_CASE("A CFF font (.otf) loads its metrics, cubic outlines and GPOS kerning")
{
	FontAtlasSettings settings;
	settings.Ranges = { { 0x20, 0x20 }, { 'A', 'B' } };
	const Ref<Font> font = Font::LoadFromMemory(CFFTest(), "CFFTest.otf", settings);
	REQUIRE(font);
	CHECK(font->GetAscender() == doctest::Approx(0.8f));
	CHECK(font->GetDescender() == doctest::Approx(-0.2f));
	CHECK(font->GetLineHeight() == doctest::Approx(1.2f));
	CHECK(font->GetGlyph('A').Advance == doctest::Approx(0.7f));
	CHECK(font->GetKerning('A', 'B') == doctest::Approx(-0.05f));
	CHECK(font->GetKerning('B', 'A') == 0.0f);
	CHECK_FALSE(font->GetGlyph(' ').Visible);

	// The square spans 0.1 to 0.6 em across and 0 to 0.7 up. Its plane box adds the distance field's
	// padding, half the range plus a pixel: (8 / 2 + 1) / 40 = 0.125 em.
	const FontGlyph& square = font->GetGlyph('A');
	REQUIRE(square.Visible);
	CHECK(square.PlaneMin.x == doctest::Approx(0.1f - 0.125f));
	CHECK(square.PlaneMin.y == doctest::Approx(-0.125f));
	CHECK(square.PlaneMax.x >= 0.6f + 0.125f);
	CHECK(square.PlaneMax.y >= 0.7f + 0.125f);
	REQUIRE(font->GetGlyph('B').Visible);

	std::string error;
	const std::vector<uint8_t> bytes = CFFTest();
	const std::optional<FontFile> file = Parse(bytes, error);
	REQUIRE(file);
	CHECK(file->HasCFFOutlines());
	std::vector<OutlineStep> steps;
	REQUIRE(file->GetOutline(file->FindGlyph('B'), steps));
	CHECK(std::count_if(steps.begin(), steps.end(), [](const OutlineStep& step) { return step.Type == OutlineSegment::Cubic; }) == 4);
}

TEST_CASE("Font files with damaged tables are refused with a reason")
{
	std::string error;
	CHECK_FALSE(FontFile::Parse(nullptr, 0, error));
	CHECK(error.find("not a TrueType or OpenType font") != std::string::npos);

	// The table directory says there are more tables than the file has room for.
	std::vector<uint8_t> cut = Inter();
	cut.resize(40);
	CHECK_FALSE(Parse(cut, error));
	CHECK(error.find("table directory") != std::string::npos);

	// A table that claims more bytes than the file has.
	std::vector<uint8_t> longTable = Inter();
	WriteU32(longTable, FindTableRecord(longTable, "cmap") + 12, 0x7FFFFFFF);
	CHECK_FALSE(Parse(longTable, error));
	CHECK(error.find("'cmap' table is damaged") != std::string::npos);
	CHECK_FALSE(Font::LoadFromMemory(longTable, "LongTable.ttf"));

	// No units per em would make every size infinite.
	std::vector<uint8_t> noEm = Inter();
	WriteU16(noEm, ReadU32(noEm, FindTableRecord(noEm, "head") + 8) + 18, 0);
	CHECK_FALSE(Parse(noEm, error));
	CHECK(error.find("units per em") != std::string::npos);

	// No glyph widths: a width lookup would read before the table.
	std::vector<uint8_t> noWidths = Inter();
	WriteU16(noWidths, ReadU32(noWidths, FindTableRecord(noWidths, "hhea") + 8) + 34, 0);
	CHECK_FALSE(Parse(noWidths, error));
	CHECK(error.find("glyph widths") != std::string::npos);
}

TEST_CASE("A composite glyph that contains itself loads as empty instead of recursing")
{
	std::vector<uint8_t> bytes = Inter();
	std::string error;
	uint32_t glyph = 0;
	{
		const std::optional<FontFile> file = Parse(bytes, error);
		REQUIRE(file);
		glyph = file->FindGlyph(0x00C1);	// Á: A with an acute accent placed on it
		REQUIRE(glyph != 0);
	}

	// Point Á's first part at Á itself.
	const size_t glyf = ReadU32(bytes, FindTableRecord(bytes, "glyf") + 8);
	const size_t loca = ReadU32(bytes, FindTableRecord(bytes, "loca") + 8);
	const bool longLocations = ReadU16(bytes, ReadU32(bytes, FindTableRecord(bytes, "head") + 8) + 50) == 1;
	const size_t glyphStart = glyf + (longLocations ? ReadU32(bytes, loca + glyph * 4) : ReadU16(bytes, loca + glyph * 2) * 2u);
	REQUIRE(static_cast<int16_t>(ReadU16(bytes, glyphStart)) < 0);
	WriteU16(bytes, glyphStart + 12, static_cast<uint16_t>(glyph));

	const std::optional<FontFile> file = Parse(bytes, error);
	REQUIRE(file);
	std::vector<OutlineStep> steps;
	CHECK_FALSE(file->GetOutline(glyph, steps));

	FontAtlasSettings settings;
	settings.Ranges = { { 'A', 'A' }, { 0x00C1, 0x00C1 } };
	const Ref<Font> font = Font::LoadFromMemory(bytes, "SelfComposite.ttf", settings);
	REQUIRE(font);
	CHECK(font->GetGlyph('A').Visible);
	CHECK(font->HasGlyph(0x00C1));
	CHECK_FALSE(font->GetGlyph(0x00C1).Visible);
	CHECK(font->GetGlyph(0x00C1).Advance == doctest::Approx(font->GetGlyph('A').Advance));
}

TEST_CASE("Glyph lookups past the end of a font's tables find nothing")
{
	const std::vector<uint8_t> bytes = Inter();
	std::string error;
	const std::optional<FontFile> file = Parse(bytes, error);
	REQUIRE(file);
	CHECK(file->FindGlyph(0x10FFFF) == 0);
	CHECK(file->FindGlyph(0xFFFFFFFF) == 0);
	std::vector<OutlineStep> steps;
	CHECK_FALSE(file->GetOutline(file->GetGlyphCount(), steps));
	CHECK(file->GetKerning(file->GetGlyphCount(), 1) == 0);
	CHECK(file->GetAdvance(file->GetGlyphCount() + 100) == file->GetAdvance(file->GetGlyphCount() - 1));
}
