#include "bwpch.h"
#include "Bellows/Text/FontFile.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>

namespace Bellows {

	namespace {

		// A composite glyph places other glyphs, which may be composites too. Real fonts nest two or
		// three deep and use a few dozen parts; a damaged one can point at itself.
		constexpr uint32_t c_MaxCompositeDepth = 8;
		constexpr uint32_t c_MaxCompositeComponents = 1024;
		// GPOS subtables tried for one pair of glyphs. Real fonts' kerning has tens; a damaged count
		// would otherwise mean billions of lookups for every pair of characters laid out.
		constexpr uint32_t c_MaxKerningSubtables = 512;

		constexpr uint32_t MakeTag(const char (&tag)[5])
		{
			return (static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) << 24) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 16)
				| (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 8) | static_cast<uint32_t>(static_cast<uint8_t>(tag[3]));
		}

		// Coordinates are 16-bit in both outline formats; a damaged composite's scale can push one past.
		int32_t ToCoordinate(float value)
		{
			return static_cast<int16_t>(std::clamp(value, -32768.0f, 32767.0f));
		}

	}

	FontFile::Bytes FontFile::Bytes::Range(uint64_t offset, uint64_t count) const
	{
		if (!Has(offset, count))
			return {};
		return { Data + offset, count };
	}

	uint16_t FontFile::Bytes::U16(uint64_t offset) const
	{
		if (!Has(offset, 2))
			return 0;
		return static_cast<uint16_t>((Data[offset] << 8) | Data[offset + 1]);
	}

	uint32_t FontFile::Bytes::U32(uint64_t offset) const
	{
		if (!Has(offset, 4))
			return 0;
		return (static_cast<uint32_t>(Data[offset]) << 24) | (static_cast<uint32_t>(Data[offset + 1]) << 16)
			| (static_cast<uint32_t>(Data[offset + 2]) << 8) | static_cast<uint32_t>(Data[offset + 3]);
	}

	std::optional<FontFile> FontFile::Parse(const uint8_t* data, size_t size, std::string& outError)
	{
		outError.clear();
		FontFile font;
		font.m_File = { data, data ? size : 0 };
		const Bytes& file = font.m_File;

		uint64_t fontStart = 0;
		if (file.U32(0) == MakeTag("ttcf"))
		{
			if (file.U32(8) == 0 || !file.Has(12, 4))
			{
				outError = "the font collection lists no fonts";
				return std::nullopt;
			}
			fontStart = file.U32(12);
		}
		const uint32_t version = file.U32(fontStart);
		if (!file.Has(fontStart, 12) || (version != 0x00010000 && version != MakeTag("true") && version != MakeTag("OTTO")))
		{
			outError = "it is not a TrueType or OpenType font";
			return std::nullopt;
		}

		const uint32_t tableCount = file.U16(fontStart + 4);
		const uint64_t directory = fontStart + 12;
		if (!file.Has(directory, static_cast<uint64_t>(tableCount) * 16))
		{
			outError = "its table directory is cut short";
			return std::nullopt;
		}
		// The first table with the tag, as other font readers take it. A table that claims bytes past
		// the end of the file is damaged.
		bool damaged = false;
		const auto findTable = [&](const char (&tag)[5])
		{
			damaged = false;
			for (uint32_t i = 0; i < tableCount; i++)
			{
				const uint64_t record = directory + static_cast<uint64_t>(i) * 16;
				if (file.U32(record) != MakeTag(tag))
					continue;
				const uint64_t offset = file.U32(record + 8);
				const uint64_t length = file.U32(record + 12);
				if (!file.Has(offset, length))
				{
					damaged = true;
					return Bytes();
				}
				return file.Range(offset, length);
			}
			return Bytes();
		};
		const auto required = [&](Bytes table, const char* name, uint64_t minimumSize)
		{
			if (table.Size < minimumSize)
			{
				outError = std::string("its '") + name + "' table is " + (damaged || table.Size ? "damaged" : "missing");
				return false;
			}
			return true;
		};

		font.m_Head = findTable("head");
		if (!required(font.m_Head, "head", 54))
			return std::nullopt;
		font.m_UnitsPerEm = font.m_Head.U16(18);
		if (font.m_UnitsPerEm < 16 || font.m_UnitsPerEm > 16384)
		{
			outError = "its units per em (" + std::to_string(font.m_UnitsPerEm) + ") must be from 16 to 16384";
			return std::nullopt;
		}

		const Bytes maximumProfile = findTable("maxp");
		if (!required(maximumProfile, "maxp", 6))
			return std::nullopt;
		font.m_GlyphCount = maximumProfile.U16(4);
		if (font.m_GlyphCount == 0)
		{
			outError = "it has no glyphs";
			return std::nullopt;
		}

		font.m_HorizontalHeader = findTable("hhea");
		if (!required(font.m_HorizontalHeader, "hhea", 36))
			return std::nullopt;
		font.m_LongMetricCount = font.m_HorizontalHeader.U16(34);
		font.m_HorizontalMetrics = findTable("hmtx");
		if (font.m_LongMetricCount == 0 || font.m_LongMetricCount > font.m_GlyphCount
			|| !required(font.m_HorizontalMetrics, "hmtx", static_cast<uint64_t>(font.m_LongMetricCount) * 4))
		{
			if (outError.empty())
				outError = "its 'hhea' table lists " + std::to_string(font.m_LongMetricCount) + " glyph widths for " + std::to_string(font.m_GlyphCount) + " glyphs";
			return std::nullopt;
		}

		const Bytes characterMap = findTable("cmap");
		if (!required(characterMap, "cmap", 4) || !font.ParseCharacterMap(characterMap, outError))
			return std::nullopt;

		// Optional tables: a damaged one is left out (no typographic metrics, no kerning).
		font.m_OS2 = findTable("OS/2");
		if (font.m_OS2.Size < 74)
			font.m_OS2 = {};
		font.m_Kern = findTable("kern");
		font.m_Positioning = findTable("GPOS");

		font.m_Glyf = findTable("glyf");
		font.m_Loca = findTable("loca");
		if (font.m_Glyf.Data)
		{
			const uint16_t locationFormat = font.m_Head.U16(50);
			font.m_LongLocations = locationFormat == 1;
			if (locationFormat > 1 || !required(font.m_Loca, "loca", (static_cast<uint64_t>(font.m_GlyphCount) + 1) * (font.m_LongLocations ? 4 : 2)))
			{
				if (outError.empty())
					outError = "its glyph location format (" + std::to_string(locationFormat) + ") is unknown";
				return std::nullopt;
			}
			return font;
		}

		const Bytes cff = findTable("CFF ");
		if (!cff.Data)
		{
			outError = damaged ? "its glyph outlines are damaged" : "it has no glyph outlines Bellows can read (TrueType 'glyf' or 'CFF ')";
			return std::nullopt;
		}
		if (!font.ParseCFF(cff, outError))
			return std::nullopt;
		return font;
	}

	bool FontFile::ParseCharacterMap(const Bytes& table, std::string& outError)
	{
		// Unicode maps first, the full range before the basic plane; a symbol font's map last.
		int bestRank = 0;
		const uint32_t subtableCount = table.U16(2);
		for (uint32_t i = 0; i < subtableCount; i++)
		{
			const uint64_t record = 4 + static_cast<uint64_t>(i) * 8;
			if (!table.Has(record, 8))
				break;
			const uint16_t platform = table.U16(record);
			const uint16_t encoding = table.U16(record + 2);
			const Bytes subtable = table.From(table.U32(record + 4));
			const uint16_t format = subtable.U16(0);

			int rank = 0;
			const bool fullRange = format == 12 || format == 13;
			if (fullRange && ((platform == 3 && encoding == 10) || (platform == 0 && (encoding == 4 || encoding == 6))))
				rank = 4;
			else if ((format == 0 || format == 4 || format == 6 || fullRange) && ((platform == 3 && (encoding == 1 || encoding == 10)) || (platform == 0 && encoding <= 4)))
				rank = 3;
			else if ((format == 0 || format == 4 || format == 6 || fullRange) && platform == 3 && encoding == 0)
				rank = 1;
			if (rank <= bestRank)
				continue;

			// Each format gives its own length; reads stay inside it.
			const uint64_t length = fullRange ? subtable.U32(4) : subtable.U16(2);
			if (length < (fullRange ? 16u : 6u) || !subtable.Has(0, length))
				continue;
			bestRank = rank;
			m_CharacterMap = subtable.Range(0, length);
			m_CharacterMapFormat = format;
		}
		if (bestRank == 0)
		{
			outError = "it has no Unicode character map Bellows can read";
			return false;
		}
		return true;
	}

	bool FontFile::ParseCFF(const Bytes& table, std::string& outError)
	{
		const uint64_t tableOffset = static_cast<uint64_t>(table.Data - m_File.Data);
		const auto fail = [&](const char* what)
		{
			outError = std::string("its CFF outlines are damaged (") + what + ")";
			return false;
		};

		// An INDEX: a count, offsets and the data they point into. Gives where it ends.
		const auto readIndex = [&](uint64_t at, FileRange& outRange, uint64_t& outEnd)
		{
			const uint32_t count = table.U16(at);
			if (!table.Has(at, 2))
				return false;
			if (count == 0)
			{
				outRange = { tableOffset + at, 2 };
				outEnd = at + 2;
				return true;
			}
			const uint32_t offsetSize = table.U8(at + 2);
			if (offsetSize < 1 || offsetSize > 4 || !table.Has(at + 3, (static_cast<uint64_t>(count) + 1) * offsetSize))
				return false;
			const auto offset = [&](uint32_t index)
			{
				uint64_t value = 0;
				for (uint32_t b = 0; b < offsetSize; b++)
					value = (value << 8) | table.U8(at + 3 + static_cast<uint64_t>(index) * offsetSize + b);
				return value;
			};
			uint64_t previous = offset(0);
			if (previous != 1)
				return false;
			for (uint32_t i = 1; i <= count; i++)
			{
				const uint64_t next = offset(i);
				if (next < previous)
					return false;
				previous = next;
			}
			const uint64_t dataStart = at + 3 + (static_cast<uint64_t>(count) + 1) * offsetSize - 1;
			const uint64_t end = dataStart + previous;
			if (end > table.Size)
				return false;
			outRange = { tableOffset + at, end - at };
			outEnd = end;
			return true;
		};
		const auto indexItem = [&](const FileRange& index, uint32_t item)
		{
			const Bytes bytes = m_File.Range(index.Offset, index.Size);
			const uint32_t count = bytes.U16(0);
			const uint32_t offsetSize = bytes.U8(2);
			if (item >= count)
				return Bytes();
			const auto offset = [&](uint32_t i)
			{
				uint64_t value = 0;
				for (uint32_t b = 0; b < offsetSize; b++)
					value = (value << 8) | bytes.U8(3 + static_cast<uint64_t>(i) * offsetSize + b);
				return value;
			};
			const uint64_t dataStart = 3 + (static_cast<uint64_t>(count) + 1) * offsetSize - 1;
			return bytes.Range(dataStart + offset(item), offset(item + 1) - offset(item));
		};
		// The integer operands of a DICT entry; false when an operand is malformed, isn't an integer or
		// is missing. An entry the DICT doesn't have leaves 'values' as they are.
		const auto readDict = [](const Bytes& dict, uint16_t key, uint32_t count, int64_t* values)
		{
			std::vector<std::pair<bool, int64_t>> operands;	// Is an integer, its value
			for (uint64_t at = 0; at < dict.Size;)
			{
				const uint8_t b0 = dict.U8(at);
				if (b0 <= 21)
				{
					uint16_t op = b0;
					at++;
					if (b0 == 12)
						op = static_cast<uint16_t>(0x100 | dict.U8(at++));
					if (op == key)
					{
						if (operands.size() < count)
							return false;
						for (uint32_t i = 0; i < count; i++)
						{
							if (!operands[i].first)
								return false;
							values[i] = operands[i].second;
						}
						return true;
					}
					operands.clear();
				}
				else if (b0 == 28)
				{
					operands.emplace_back(true, static_cast<int16_t>(dict.U16(at + 1)));
					at += 3;
				}
				else if (b0 == 29)
				{
					operands.emplace_back(true, static_cast<int32_t>(dict.U32(at + 1)));
					at += 5;
				}
				else if (b0 == 30)
				{
					// A real number: nibbles until one is 0xF.
					at++;
					while (at < dict.Size && (dict.U8(at) & 0x0F) != 0x0F && (dict.U8(at) >> 4) != 0x0F)
						at++;
					at++;
					operands.emplace_back(false, 0);
				}
				else if (b0 >= 32 && b0 <= 246)
				{
					operands.emplace_back(true, b0 - 139);
					at += 1;
				}
				else if (b0 >= 247 && b0 <= 250)
				{
					operands.emplace_back(true, (b0 - 247) * 256 + dict.U8(at + 1) + 108);
					at += 2;
				}
				else if (b0 >= 251 && b0 <= 254)
				{
					operands.emplace_back(true, -(b0 - 251) * 256 - dict.U8(at + 1) - 108);
					at += 2;
				}
				else
					return false;	// 22-27 are reserved, 31 and 255 aren't operands
				if (operands.size() > 48)
					return false;
			}
			return true;
		};

		if (table.U8(0) != 1 || table.U8(2) < 4 || table.Size > 0x3FFFFFFF)
			return fail("header");
		FileRange names, topDicts, strings;
		uint64_t at = table.U8(2);
		if (!readIndex(at, names, at) || !readIndex(at, topDicts, at) || !readIndex(at, strings, at) || !readIndex(at, m_GlobalSubroutines, at))
			return fail("indexes");
		const Bytes topDict = indexItem(topDicts, 0);
		if (!topDict.Data)
			return fail("top dictionary");

		int64_t charStrings = 0, charStringType = 2, fdArray = 0, fdSelect = 0;
		int64_t privateDict[2] = { 0, 0 };	// Size, offset
		if (!readDict(topDict, 17, 1, &charStrings) || !readDict(topDict, 0x106, 1, &charStringType) || !readDict(topDict, 0x124, 1, &fdArray)
			|| !readDict(topDict, 0x125, 1, &fdSelect) || !readDict(topDict, 18, 2, privateDict))
			return fail("top dictionary");
		if (charStringType != 2)
		{
			outError = "its CFF outlines are Type " + std::to_string(charStringType) + " charstrings; Bellows reads Type 2";
			return false;
		}
		uint64_t end = 0;
		if (charStrings <= 0 || !readIndex(static_cast<uint64_t>(charStrings), m_CharStrings, end))
			return fail("charstrings");

		if (privateDict[0] > 0 && privateDict[1] > 0)
		{
			if (!table.Has(static_cast<uint64_t>(privateDict[1]), static_cast<uint64_t>(privateDict[0])))
				return fail("private dictionary");
			int64_t subroutines = 0;
			const Bytes dict = table.Range(static_cast<uint64_t>(privateDict[1]), static_cast<uint64_t>(privateDict[0]));
			if (!readDict(dict, 19, 1, &subroutines))
				return fail("private dictionary");
			if (subroutines > 0 && !readIndex(static_cast<uint64_t>(privateDict[1] + subroutines), m_Subroutines, end))
				return fail("subroutines");
		}
		if (fdArray > 0)
		{
			// A CID font: each glyph's subroutines come from one of several font dictionaries.
			if (fdSelect <= 0 || static_cast<uint64_t>(fdSelect) >= table.Size || !readIndex(static_cast<uint64_t>(fdArray), m_FontDicts, end))
				return fail("font dictionaries");
			m_FDSelect = { tableOffset + static_cast<uint64_t>(fdSelect), table.Size - static_cast<uint64_t>(fdSelect) };
		}
		m_CFF = { tableOffset, table.Size };
		return true;
	}

	void FontFile::GetVerticalMetrics(int& ascent, int& descent, int& lineGap) const
	{
		if (m_OS2.Data)
		{
			ascent = m_OS2.S16(68);
			descent = m_OS2.S16(70);
			lineGap = m_OS2.S16(72);
			return;
		}
		ascent = m_HorizontalHeader.S16(4);
		descent = m_HorizontalHeader.S16(6);
		lineGap = m_HorizontalHeader.S16(8);
	}

	uint32_t FontFile::FindGlyph(uint32_t codepoint) const
	{
		const uint32_t glyph = FindGlyphInMap(codepoint);
		return glyph < m_GlyphCount ? glyph : 0;
	}

	uint32_t FontFile::FindGlyphInMap(uint32_t codepoint) const
	{
		const Bytes& map = m_CharacterMap;
		switch (m_CharacterMapFormat)
		{
			case 0:		// One byte per character, the first 256
				return codepoint < 256 ? map.U8(6 + codepoint) : 0;
			case 4:		// Ranges of the basic plane
			{
				if (codepoint > 0xFFFF)
					return 0;
				const uint32_t segments = map.U16(6) / 2;
				const uint64_t ends = 14;
				const uint64_t starts = ends + segments * 2 + 2;
				const uint64_t deltas = starts + segments * 2;
				const uint64_t rangeOffsets = deltas + segments * 2;
				// The first segment that ends at or after the character.
				uint32_t low = 0, high = segments;
				while (low < high)
				{
					const uint32_t middle = low + (high - low) / 2;
					if (map.U16(ends + middle * 2) < codepoint)
						low = middle + 1;
					else
						high = middle;
				}
				if (low >= segments)
					return 0;
				const uint32_t start = map.U16(starts + low * 2);
				if (codepoint < start)
					return 0;
				const uint16_t delta = map.U16(deltas + low * 2);
				const uint16_t rangeOffset = map.U16(rangeOffsets + low * 2);
				if (rangeOffset == 0)
					return static_cast<uint16_t>(codepoint + delta);
				const uint16_t glyph = map.U16(rangeOffsets + low * 2 + rangeOffset + static_cast<uint64_t>(codepoint - start) * 2);
				return glyph == 0 ? 0 : static_cast<uint16_t>(glyph + delta);
			}
			case 6:		// One run of characters
			{
				const uint32_t first = map.U16(6);
				const uint32_t count = map.U16(8);
				return codepoint >= first && codepoint - first < count ? map.U16(10 + static_cast<uint64_t>(codepoint - first) * 2) : 0;
			}
			case 12:	// Ranges of all of Unicode, glyphs in order
			case 13:	// Ranges of all of Unicode, one glyph each
			{
				const uint64_t groups = std::min<uint64_t>(map.U32(12), (map.Size - 16) / 12);
				uint64_t low = 0, high = groups;
				while (low < high)
				{
					const uint64_t middle = low + (high - low) / 2;
					const uint64_t group = 16 + middle * 12;
					if (codepoint < map.U32(group))
						high = middle;
					else if (codepoint > map.U32(group + 4))
						low = middle + 1;
					else
					{
						const uint32_t glyph = map.U32(group + 8);
						return m_CharacterMapFormat == 12 ? glyph + (codepoint - map.U32(group)) : glyph;
					}
				}
				return 0;
			}
			default:
				return 0;
		}
	}

	int FontFile::GetAdvance(uint32_t glyph) const
	{
		return m_HorizontalMetrics.U16(static_cast<uint64_t>(std::min(glyph, m_LongMetricCount - 1)) * 4);
	}

	int FontFile::GetKerning(uint32_t left, uint32_t right) const
	{
		if (left >= m_GlyphCount || right >= m_GlyphCount)
			return 0;
		int kerning = m_Positioning.Data ? GetPositioningKerning(left, right) : 0;
		if (kerning == 0 && m_Kern.Data)
			kerning = GetKernTableKerning(left, right);
		return kerning;
	}

	int FontFile::GetPositioningKerning(uint32_t left, uint32_t right) const
	{
		const Bytes& gpos = m_Positioning;
		if (gpos.U16(0) != 1 || gpos.U16(2) > 1)
			return 0;

		// A glyph's place in a coverage table, or -1.
		const auto coverageIndex = [](const Bytes& coverage, uint32_t glyph) -> int64_t
		{
			const uint32_t count = coverage.U16(2);
			int64_t low = 0, high = static_cast<int64_t>(count) - 1;
			switch (coverage.U16(0))
			{
				case 1:		// A sorted list of glyphs
					while (low <= high)
					{
						const int64_t middle = (low + high) / 2;
						const uint32_t value = coverage.U16(4 + static_cast<uint64_t>(middle) * 2);
						if (glyph < value)
							high = middle - 1;
						else if (glyph > value)
							low = middle + 1;
						else
							return middle;
					}
					return -1;
				case 2:		// Sorted ranges of glyphs
					while (low <= high)
					{
						const int64_t middle = (low + high) / 2;
						const uint64_t range = 4 + static_cast<uint64_t>(middle) * 6;
						if (glyph < coverage.U16(range))
							high = middle - 1;
						else if (glyph > coverage.U16(range + 2))
							low = middle + 1;
						else
							return static_cast<int64_t>(coverage.U16(range + 4)) + glyph - coverage.U16(range);
					}
					return -1;
				default:
					return -1;
			}
		};
		// A glyph's class in a class definition table; glyphs it doesn't list are class 0.
		const auto glyphClass = [](const Bytes& classes, uint32_t glyph) -> int64_t
		{
			switch (classes.U16(0))
			{
				case 1:
				{
					const uint32_t first = classes.U16(2);
					const uint32_t count = classes.U16(4);
					return glyph >= first && glyph - first < count ? classes.U16(6 + static_cast<uint64_t>(glyph - first) * 2) : 0;
				}
				case 2:
				{
					int64_t low = 0, high = static_cast<int64_t>(classes.U16(2)) - 1;
					while (low <= high)
					{
						const int64_t middle = (low + high) / 2;
						const uint64_t range = 4 + static_cast<uint64_t>(middle) * 6;
						if (glyph < classes.U16(range))
							high = middle - 1;
						else if (glyph > classes.U16(range + 2))
							low = middle + 1;
						else
							return classes.U16(range + 4);
					}
					return 0;
				}
				default:
					return -1;
			}
		};

		uint32_t budget = c_MaxKerningSubtables;
		const Bytes lookups = gpos.From(gpos.U16(8));
		const uint32_t lookupCount = lookups.U16(0);
		for (uint32_t i = 0; i < lookupCount; i++)
		{
			const Bytes lookup = lookups.From(lookups.U16(2 + static_cast<uint64_t>(i) * 2));
			const uint16_t lookupType = lookup.U16(0);
			if (lookupType != 2 && lookupType != 9)	// Pair adjustment, or an extension holding one
				continue;
			const uint32_t subtableCount = lookup.U16(4);
			for (uint32_t s = 0; s < subtableCount; s++)
			{
				if (budget-- == 0)
					return 0;
				Bytes subtable = lookup.From(lookup.U16(6 + static_cast<uint64_t>(s) * 2));
				if (lookupType == 9)
				{
					if (subtable.U16(0) != 1 || subtable.U16(2) != 2)
						continue;
					subtable = subtable.From(subtable.U32(4));
				}
				const int64_t coverage = coverageIndex(subtable.From(subtable.U16(2)), left);
				if (coverage < 0)
					continue;
				// Only the horizontal advance of the first glyph (value format 4, or 68 with its device table).
				const uint16_t valueFormat1 = subtable.U16(4);
				if ((valueFormat1 != 4 && valueFormat1 != 68) || subtable.U16(6) != 0)
					return 0;
				const uint64_t valueSize = valueFormat1 == 68 ? 4 : 2;
				switch (subtable.U16(0))
				{
					case 1:		// Pairs listed glyph by glyph
					{
						if (coverage >= subtable.U16(8))
							return 0;
						const Bytes pairs = subtable.From(subtable.U16(10 + static_cast<uint64_t>(coverage) * 2));
						int64_t low = 0, high = static_cast<int64_t>(pairs.U16(0)) - 1;
						while (low <= high)
						{
							const int64_t middle = (low + high) / 2;
							const uint64_t pair = 2 + static_cast<uint64_t>(middle) * (2 + valueSize);
							const uint32_t second = pairs.U16(pair);
							if (right < second)
								high = middle - 1;
							else if (right > second)
								low = middle + 1;
							else
								return pairs.S16(pair + 2);
						}
						break;
					}
					case 2:		// Pairs of glyph classes
					{
						const int64_t class1 = glyphClass(subtable.From(subtable.U16(8)), left);
						const int64_t class2 = glyphClass(subtable.From(subtable.U16(10)), right);
						const uint32_t class1Count = subtable.U16(12);
						const uint32_t class2Count = subtable.U16(14);
						if (class1 < 0 || class1 >= class1Count || class2 < 0 || class2 >= class2Count)
							return 0;
						return subtable.S16(16 + (static_cast<uint64_t>(class1) * class2Count + static_cast<uint64_t>(class2)) * valueSize);
					}
					default:
						return 0;
				}
			}
		}
		return 0;
	}

	int FontFile::GetKernTableKerning(uint32_t left, uint32_t right) const
	{
		// The first subtable, when it is horizontal format 0: pairs sorted by the two glyphs.
		const Bytes& kern = m_Kern;
		if (kern.U16(2) < 1 || kern.U16(8) != 1)
			return 0;
		const uint32_t needle = (left << 16) | right;
		int64_t low = 0, high = static_cast<int64_t>(kern.U16(10)) - 1;
		while (low <= high)
		{
			const int64_t middle = (low + high) / 2;
			const uint32_t pair = kern.U32(18 + static_cast<uint64_t>(middle) * 6);
			if (needle < pair)
				high = middle - 1;
			else if (needle > pair)
				low = middle + 1;
			else
				return kern.S16(22 + static_cast<uint64_t>(middle) * 6);
		}
		return 0;
	}

	bool FontFile::GetOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps) const
	{
		outSteps.clear();
		if (glyph >= m_GlyphCount)
			return false;
		if (HasCFFOutlines())
			return GetCFFOutline(glyph, outSteps);
		uint32_t componentBudget = c_MaxCompositeComponents;
		return GetTrueTypeOutline(glyph, outSteps, 0, componentBudget);
	}

	bool FontFile::GetTrueTypeOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps, uint32_t depth, uint32_t& componentBudget) const
	{
		if (glyph >= m_GlyphCount)
			return false;
		const uint64_t start = m_LongLocations ? m_Loca.U32(static_cast<uint64_t>(glyph) * 4) : static_cast<uint64_t>(m_Loca.U16(static_cast<uint64_t>(glyph) * 2)) * 2;
		const uint64_t end = m_LongLocations ? m_Loca.U32(static_cast<uint64_t>(glyph) * 4 + 4) : static_cast<uint64_t>(m_Loca.U16(static_cast<uint64_t>(glyph) * 2 + 2)) * 2;
		if (end < start || !m_Glyf.Has(start, end - start))
			return false;
		if (end == start)
			return true;	// No outline: a space
		const Bytes data = m_Glyf.Range(start, end - start);
		if (data.Size < 10)
			return false;

		const int16_t contourCount = data.S16(0);
		if (contourCount == 0)
			return true;
		if (contourCount > 0)
		{
			// A simple glyph: contour ends, instructions (skipped), then flags, x and y, packed.
			std::vector<uint16_t> contourEnds(static_cast<size_t>(contourCount));
			for (int16_t c = 0; c < contourCount; c++)
			{
				contourEnds[c] = data.U16(10 + static_cast<uint64_t>(c) * 2);
				if (c > 0 && contourEnds[c] <= contourEnds[c - 1])
					return false;
			}
			const uint64_t instructionsAt = 10 + static_cast<uint64_t>(contourCount) * 2;
			uint64_t at = instructionsAt + 2 + data.U16(instructionsAt);
			if (at > data.Size)
				return false;
			const size_t pointCount = static_cast<size_t>(contourEnds.back()) + 1;

			std::vector<uint8_t> flags(pointCount);
			for (size_t i = 0; i < pointCount;)
			{
				if (at >= data.Size)
					return false;
				const uint8_t flag = data.U8(at++);
				size_t repeat = 1;
				if (flag & 0x08)
				{
					if (at >= data.Size)
						return false;
					repeat += data.U8(at++);
				}
				for (; repeat > 0 && i < pointCount; repeat--)
					flags[i++] = flag;
			}
			std::vector<int32_t> xs(pointCount), ys(pointCount);
			const auto readCoordinates = [&](std::vector<int32_t>& values, uint8_t shortBit, uint8_t sameOrPositiveBit)
			{
				int32_t value = 0;
				for (size_t i = 0; i < pointCount; i++)
				{
					if (flags[i] & shortBit)
					{
						if (at >= data.Size)
							return false;
						const int32_t delta = data.U8(at++);
						value += (flags[i] & sameOrPositiveBit) ? delta : -delta;
					}
					else if (!(flags[i] & sameOrPositiveBit))
					{
						if (!data.Has(at, 2))
							return false;
						value += data.S16(at);
						at += 2;
					}
					values[i] = static_cast<int16_t>(value);
				}
				return true;
			};
			if (!readCoordinates(xs, 0x02, 0x10) || !readCoordinates(ys, 0x04, 0x20))
				return false;

			// Two off-curve points in a row have an on-curve point halfway between them.
			const auto halfway = [](int32_t a, int32_t b) { return (a + b) >> 1; };
			size_t first = 0;
			for (uint16_t last : contourEnds)
			{
				const size_t count = static_cast<size_t>(last) + 1 - first;
				const auto point = [&](size_t i) { return first + i % count; };
				const auto onCurve = [&](size_t i) { return (flags[point(i)] & 0x01) != 0; };

				// Start on an on-curve point: the first, the last, or between the two.
				int32_t startX, startY;
				size_t begin, steps;
				if (onCurve(0))
				{
					startX = xs[first], startY = ys[first];
					begin = 1, steps = count - 1;
				}
				else if (onCurve(count - 1))
				{
					startX = xs[point(count - 1)], startY = ys[point(count - 1)];
					begin = 0, steps = count - 1;
				}
				else
				{
					startX = halfway(xs[first], xs[point(count - 1)]), startY = halfway(ys[first], ys[point(count - 1)]);
					begin = 0, steps = count;
				}
				outSteps.push_back({ OutlineSegment::Move, startX, startY });

				bool hasControl = false;
				int32_t controlX = 0, controlY = 0;
				for (size_t s = 0; s < steps; s++)
				{
					const size_t p = point(begin + s);
					if (flags[p] & 0x01)
					{
						if (hasControl)
							outSteps.push_back({ OutlineSegment::Quadratic, xs[p], ys[p], controlX, controlY });
						else
							outSteps.push_back({ OutlineSegment::Line, xs[p], ys[p] });
						hasControl = false;
					}
					else
					{
						if (hasControl)
							outSteps.push_back({ OutlineSegment::Quadratic, halfway(controlX, xs[p]), halfway(controlY, ys[p]), controlX, controlY });
						controlX = xs[p], controlY = ys[p];
						hasControl = true;
					}
				}
				if (hasControl)
					outSteps.push_back({ OutlineSegment::Quadratic, startX, startY, controlX, controlY });
				else
					outSteps.push_back({ OutlineSegment::Line, startX, startY });
				first = static_cast<size_t>(last) + 1;
			}
			return true;
		}

		// A composite glyph: other glyphs, each moved, scaled or turned.
		if (depth >= c_MaxCompositeDepth)
			return false;
		uint64_t at = 10;
		for (bool more = true; more;)
		{
			if (componentBudget == 0 || !data.Has(at, 4))
				return false;
			componentBudget--;
			const uint16_t flags = data.U16(at);
			const uint16_t component = data.U16(at + 2);
			at += 4;

			float offsetX = 0.0f, offsetY = 0.0f;
			const uint64_t argumentSize = (flags & 0x0001) ? 4 : 2;
			if (!data.Has(at, argumentSize))
				return false;
			// The arguments are an offset, or (rarely) points to line up, which are read as no offset.
			if (flags & 0x0002)
			{
				offsetX = (flags & 0x0001) ? data.S16(at) : static_cast<int8_t>(data.U8(at));
				offsetY = (flags & 0x0001) ? data.S16(at + 2) : static_cast<int8_t>(data.U8(at + 1));
			}
			at += argumentSize;

			float matrix[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
			const auto f2dot14 = [&](uint64_t offset) { return static_cast<float>(data.S16(offset)) / 16384.0f; };
			if (flags & 0x0008)			// One scale
			{
				if (!data.Has(at, 2))
					return false;
				matrix[0] = matrix[3] = f2dot14(at);
				at += 2;
			}
			else if (flags & 0x0040)	// Separate x and y scales
			{
				if (!data.Has(at, 4))
					return false;
				matrix[0] = f2dot14(at);
				matrix[3] = f2dot14(at + 2);
				at += 4;
			}
			else if (flags & 0x0080)	// A 2 by 2 matrix
			{
				if (!data.Has(at, 8))
					return false;
				for (int i = 0; i < 4; i++)
					matrix[i] = f2dot14(at + static_cast<uint64_t>(i) * 2);
				at += 8;
			}
			// SCALED_COMPONENT_OFFSET without UNSCALED_COMPONENT_OFFSET scales the offset too.
			if ((flags & 0x0800) && !(flags & 0x1000))
			{
				offsetX *= std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1]);
				offsetY *= std::sqrt(matrix[2] * matrix[2] + matrix[3] * matrix[3]);
			}

			std::vector<OutlineStep> part;
			if (!GetTrueTypeOutline(component, part, depth + 1, componentBudget))
				return false;
			const auto transform = [&](int32_t& x, int32_t& y)
			{
				const float fx = static_cast<float>(x), fy = static_cast<float>(y);
				x = ToCoordinate(matrix[0] * fx + matrix[2] * fy + offsetX);
				y = ToCoordinate(matrix[1] * fx + matrix[3] * fy + offsetY);
			};
			for (OutlineStep& step : part)
			{
				transform(step.X, step.Y);
				transform(step.ControlX, step.ControlY);
			}
			outSteps.insert(outSteps.end(), part.begin(), part.end());
			more = (flags & 0x0020) != 0;
		}
		return true;
	}

	bool FontFile::GetCFFOutline(uint32_t glyph, std::vector<OutlineStep>& outSteps) const
	{
		if (glyph >= m_File.Range(m_CharStrings.Offset, m_CharStrings.Size).U16(0))
			return false;

		// stb_truetype runs the charstrings with bounds-checked buffers; these give it each one's real size.
		const auto buffer = [&](const FileRange& range)
		{
			stbtt__buf result{};
			result.data = range.Size ? const_cast<uint8_t*>(m_File.Data + range.Offset) : nullptr;
			result.size = static_cast<int>(range.Size);
			return result;
		};
		stbtt_fontinfo info{};
		info.data = const_cast<uint8_t*>(m_File.Data);
		info.numGlyphs = static_cast<int>(m_GlyphCount);
		info.cff = buffer(m_CFF);
		info.charstrings = buffer(m_CharStrings);
		info.gsubrs = buffer(m_GlobalSubroutines);
		info.subrs = buffer(m_Subroutines);
		info.fontdicts = buffer(m_FontDicts);
		info.fdselect = buffer(m_FDSelect);

		stbtt_vertex* vertices = nullptr;
		const int count = stbtt_GetGlyphShape(&info, static_cast<int>(glyph), &vertices);
		for (int i = 0; i < count; i++)
		{
			const stbtt_vertex& v = vertices[i];
			OutlineStep step { OutlineSegment::Move, v.x, v.y, v.cx, v.cy, v.cx1, v.cy1 };
			switch (v.type)
			{
				case STBTT_vmove:	step.Type = OutlineSegment::Move; break;
				case STBTT_vline:	step.Type = OutlineSegment::Line; break;
				case STBTT_vcurve:	step.Type = OutlineSegment::Quadratic; break;
				default:			step.Type = OutlineSegment::Cubic; break;
			}
			outSteps.push_back(step);
		}
		stbtt_FreeShape(&info, vertices);
		return true;
	}

}
