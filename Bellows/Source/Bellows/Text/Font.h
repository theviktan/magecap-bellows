#pragma once

#include "Bellows/Core/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bellows {

	class Texture2D;

	// A run of Unicode code points to put in a font's atlas, both ends included.
	struct CodepointRange
	{
		uint32_t First = 0;
		uint32_t Last = 0;
	};

	struct FontAtlasSettings
	{
		// Atlas pixels per em. Text stays sharp at any size; this only sets how fine corners can be.
		float GlyphSize = 40.0f;
		// How far the distance field reaches past an outline, in atlas pixels (outlines and soft edges
		// up to this wide can be drawn from it).
		float DistanceRange = 4.0f;
		// The characters in the atlas. Default: printable ASCII, Latin-1, and common punctuation (quotes,
		// dashes, the ellipsis, the euro sign).
		std::vector<CodepointRange> Ranges = DefaultRanges();

		static std::vector<CodepointRange> DefaultRanges();
	};

	// One character's place in the atlas and on the line. Positions are in ems, measured from the pen
	// position on the baseline, with y pointing up; UVs have (0, 0) at the atlas's top-left.
	struct FontGlyph
	{
		uint32_t Codepoint = 0;
		float Advance = 0.0f;		// How far the pen moves after it
		glm::vec2 PlaneMin = glm::vec2(0.0f);
		glm::vec2 PlaneMax = glm::vec2(0.0f);
		glm::vec2 UVMin = glm::vec2(0.0f);
		glm::vec2 UVMax = glm::vec2(0.0f);
		bool Visible = false;		// False for spaces and other glyphs without an outline
	};

	// A font loaded from a TrueType or OpenType file (.ttf, .otf), with a multi-channel signed distance
	// field (MSDF) atlas of its characters: glyph outlines come from stb_truetype, the distance fields
	// from msdfgen, and the atlas is packed with stb_rect_pack. Text drawn from it stays crisp at any
	// size, in screen and world space. The atlas is RGBA: RGB hold the multi-channel distance, A the
	// true distance (for outlines and shadows). The GPU texture is made on first use. Use a font from
	// the main thread (it remembers which missing characters it has reported).
	class Font
	{
	public:
		// Logs an error and returns null when the file can't be read or isn't a font.
		static Ref<Font> LoadFromFile(const std::filesystem::path& path, const FontAtlasSettings& settings = {});
		static Ref<Font> LoadFromMemory(std::vector<uint8_t> data, const std::string& name, const FontAtlasSettings& settings = {});

		const std::string& GetName() const { return m_Name; }
		// Line metrics in ems: the ascender is above the baseline (positive), the descender below it
		// (negative); the line height is the distance from one baseline to the next.
		float GetAscender() const { return m_Ascender; }
		float GetDescender() const { return m_Descender; }
		float GetLineHeight() const { return m_LineHeight; }

		// The glyph for a character; characters the atlas lacks get the font's "missing" glyph (a box in
		// most fonts), and each one is logged once.
		const FontGlyph& GetGlyph(uint32_t codepoint) const;
		bool HasGlyph(uint32_t codepoint) const { return m_Glyphs.contains(codepoint); }
		// Extra space between two characters (negative pulls them together), in ems.
		float GetKerning(uint32_t left, uint32_t right) const;

		uint32_t GetAtlasWidth() const { return m_AtlasWidth; }
		uint32_t GetAtlasHeight() const { return m_AtlasHeight; }
		// RGBA8, rows from the top, GetAtlasWidth() * GetAtlasHeight() * 4 bytes.
		const std::vector<uint8_t>& GetAtlasPixels() const { return m_AtlasPixels; }
		// The distance range in atlas pixels, and the atlas pixels per em, which the text shader needs.
		float GetDistanceRange() const { return m_DistanceRange; }
		float GetGlyphSize() const { return m_GlyphSize; }
		// Creates the GPU texture on first call (needs the renderer).
		const Ref<Texture2D>& GetAtlasTexture();

		~Font();
		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;
	private:
		Font();
		bool Build(const FontAtlasSettings& settings);
	private:
		std::string m_Name;
		std::vector<uint8_t> m_FileData;
		struct FontInfo;
		Scope<FontInfo> m_Info;
		float m_Scale = 1.0f;	// Font units to ems

		float m_Ascender = 0.0f;
		float m_Descender = 0.0f;
		float m_LineHeight = 0.0f;
		float m_GlyphSize = 0.0f;
		float m_DistanceRange = 0.0f;

		std::unordered_map<uint32_t, FontGlyph> m_Glyphs;
		FontGlyph m_MissingGlyph;
		mutable std::vector<uint32_t> m_ReportedMissing;
		mutable std::unordered_map<uint64_t, float> m_KerningCache;

		uint32_t m_AtlasWidth = 0;
		uint32_t m_AtlasHeight = 0;
		std::vector<uint8_t> m_AtlasPixels;
		Ref<Texture2D> m_AtlasTexture;
	};

}
