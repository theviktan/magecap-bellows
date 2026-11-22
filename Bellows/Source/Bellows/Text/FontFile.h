#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	enum class OutlineSegment : uint8_t
	{
		Move = 0,	// Starts a contour at the point
		Line,		// A straight line from the previous point
		Quadratic,	// A curve from the previous point with one control point (TrueType outlines)
		Cubic,		// A curve from the previous point with two control points (CFF outlines)
	};

	// One step of a glyph's outline, in font units with y pointing up.
	struct OutlineStep
	{
		OutlineSegment Type = OutlineSegment::Move;
		int32_t X = 0;
		int32_t Y = 0;
		int32_t ControlX = 0;
		int32_t ControlY = 0;
		int32_t Control2X = 0;
		int32_t Control2Y = 0;
	};

	// The parts of a TrueType or OpenType font file that Bellows uses: metrics, the character map,
	// kerning and glyph outlines. Font files come from anywhere (a game's assets, a download), so every
	// read is checked against the file and every loop has a limit: a damaged file is refused, or reads
	// as missing characters, no kerning or empty glyphs, and never reads outside its bytes, recurses
	// without end or runs for minutes. TrueType outlines are read here; CFF outlines (most .otf files)
	// are read by stb_truetype's bounds-checked CFF code, set up here with each table's real size.
	// The bytes must outlive the FontFile, which only points at them. Reading is thread-safe.
	class FontFile
	{
	public:
		// Reads a font, or the first font of a collection (.ttc). Returns nothing and sets 'outError' to
		// why when the file isn't a font Bellows can read or a table it needs is damaged.
		static std::optional<FontFile> Parse(const uint8_t* data, size_t size, std::string& outError);

		uint32_t GetUnitsPerEm() const { return m_UnitsPerEm; }
		uint32_t GetGlyphCount() const { return m_GlyphCount; }
		bool HasCFFOutlines() const { return m_CFF.Size != 0; }
		// The typographic ascender, descender and line gap (OS/2 table, or hhea without one), in font units.
		void GetVerticalMetrics(int& ascent, int& descent, int& lineGap) const;

		// The glyph for a Unicode code point; 0 (the missing glyph) when the font has none.
		uint32_t FindGlyph(uint32_t codepoint) const;
		// How far the pen moves after a glyph, in font units.
		int GetAdvance(uint32_t glyph) const;
		// Extra space between two glyphs (negative pulls them together), in font units: GPOS pair
		// adjustment, or the kern table when GPOS has none for the pair.
		int GetKerning(uint32_t left, uint32_t right) const;
		// Replaces 'outSteps' with a glyph's outline. Returns false when the glyph's data is damaged or
		// too complex; an empty glyph (a space) returns true with no steps.
		bool GetOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps) const;
	private:
		// A range of the file. Reads past its end give 0, so a damaged offset can't reach outside it.
		struct Bytes
		{
			const uint8_t* Data = nullptr;
			uint64_t Size = 0;

			bool Has(uint64_t offset, uint64_t count) const { return offset <= Size && count <= Size - offset; }
			Bytes Range(uint64_t offset, uint64_t count) const;
			Bytes From(uint64_t offset) const { return offset <= Size ? Range(offset, Size - offset) : Bytes(); }
			uint8_t U8(uint64_t offset) const { return offset < Size ? Data[offset] : 0; }
			uint16_t U16(uint64_t offset) const;
			int16_t S16(uint64_t offset) const { return static_cast<int16_t>(U16(offset)); }
			uint32_t U32(uint64_t offset) const;
		};
		// Where a CFF structure is, from the start of the file.
		struct FileRange
		{
			uint64_t Offset = 0;
			uint64_t Size = 0;
		};

		bool ParseCharacterMap(const Bytes& table, std::string& outError);
		bool ParseCFF(const Bytes& table, std::string& outError);
		uint32_t FindGlyphInMap(uint32_t codepoint) const;
		int GetPositioningKerning(uint32_t left, uint32_t right) const;
		int GetKernTableKerning(uint32_t left, uint32_t right) const;
		bool GetTrueTypeOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps, uint32_t depth, uint32_t& componentBudget) const;
		bool GetCFFOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps) const;
	private:
		Bytes m_File;
		Bytes m_Head, m_HorizontalHeader, m_HorizontalMetrics, m_OS2, m_Kern, m_Positioning, m_Glyf, m_Loca;
		Bytes m_CharacterMap;			// The chosen subtable of 'cmap'
		uint16_t m_CharacterMapFormat = 0;
		uint32_t m_UnitsPerEm = 0;
		uint32_t m_GlyphCount = 0;
		uint32_t m_LongMetricCount = 0;
		bool m_LongLocations = false;
		FileRange m_CFF, m_CharStrings, m_GlobalSubroutines, m_Subroutines, m_FontDicts, m_FDSelect;
	};

}
