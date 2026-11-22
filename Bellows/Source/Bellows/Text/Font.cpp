#include "bwpch.h"
#include "Bellows/Text/Font.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Text/FontFile.h"

#include <msdfgen.h>
#include <stb_rect_pack.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace Bellows {

	struct Font::FontInfo
	{
		FontFile File;
		std::unordered_map<uint32_t, uint32_t> GlyphIndices;	// Code point to the font's glyph index
	};

	namespace {

		// The largest atlas a font may need; a range too big for it is cut short with an error.
		constexpr uint32_t c_MaxAtlasSize = 4096;
		// Corners sharper than this (radians) keep their edge colors apart, which keeps them sharp.
		constexpr double c_CornerAngle = 3.0;

		// A glyph's distance field before it is placed in the atlas.
		struct GlyphBitmap
		{
			uint32_t Codepoint = 0;
			int Width = 0;
			int Height = 0;
			std::vector<uint8_t> Pixels;	// RGBA8, rows from the top
		};

		// Turns a glyph's outline into msdfgen's shape, in ems.
		bool BuildShape(const FontFile& file, uint32_t glyphIndex, float scale, msdfgen::Shape& shape)
		{
			std::vector<OutlineStep> steps;
			if (!file.GetOutline(glyphIndex, steps) || steps.empty())
				return false;

			msdfgen::Contour* contour = nullptr;
			msdfgen::Point2 pen;
			const auto point = [scale](int32_t x, int32_t y) { return msdfgen::Point2(static_cast<float>(x) * scale, static_cast<float>(y) * scale); };
			for (const OutlineStep& step : steps)
			{
				const msdfgen::Point2 to = point(step.X, step.Y);
				switch (step.Type)
				{
					case OutlineSegment::Move:
						contour = &shape.addContour();
						break;
					case OutlineSegment::Line:
						if (contour && to != pen)
							contour->addEdge(msdfgen::EdgeHolder(pen, to));
						break;
					case OutlineSegment::Quadratic:
						if (contour)
							contour->addEdge(msdfgen::EdgeHolder(pen, point(step.ControlX, step.ControlY), to));
						break;
					case OutlineSegment::Cubic:
						if (contour)
							contour->addEdge(msdfgen::EdgeHolder(pen, point(step.ControlX, step.ControlY), point(step.Control2X, step.Control2Y), to));
						break;
				}
				pen = to;
			}

			// Drop contours a stray move left empty, then fix outlines drawn in the wrong direction.
			shape.contours.erase(std::remove_if(shape.contours.begin(), shape.contours.end(),
				[](const msdfgen::Contour& c) { return c.edges.empty(); }), shape.contours.end());
			if (shape.contours.empty())
				return false;
			shape.normalize();
			shape.orientContours();
			msdfgen::edgeColoringSimple(shape, c_CornerAngle);
			return true;
		}

		uint32_t NextPowerOfTwo(uint32_t value)
		{
			uint32_t result = 1;
			while (result < value)
				result <<= 1;
			return result;
		}

	}

	std::vector<CodepointRange> FontAtlasSettings::DefaultRanges()
	{
		return {
			{ 0x0020, 0x007E },	// ASCII
			{ 0x00A0, 0x00FF },	// Latin-1: accents, ©, °, £, ¿
			{ 0x2013, 0x2014 },	// – —
			{ 0x2018, 0x201E },	// ‘ ’ ‚ “ ” „
			{ 0x2022, 0x2022 },	// •
			{ 0x2026, 0x2026 },	// …
			{ 0x20AC, 0x20AC },	// €
			{ 0x2122, 0x2122 },	// ™
		};
	}

	Ref<Font> Font::LoadFromFile(const std::filesystem::path& path, const FontAtlasSettings& settings)
	{
		Buffer data = FileSystem::ReadBinaryFile(path);
		if (!data)
		{
			BW_CORE_ERROR("Font: could not read '{}'", path.string());
			return nullptr;
		}
		std::vector<uint8_t> bytes(data.Data(), data.Data() + data.Size());
		return LoadFromMemory(std::move(bytes), path.filename().string(), settings);
	}

	Ref<Font> Font::LoadFromMemory(std::vector<uint8_t> data, const std::string& name, const FontAtlasSettings& settings)
	{
		Ref<Font> font(new Font());
		font->m_Name = name;
		font->m_FileData = std::move(data);

		std::string error;
		std::optional<FontFile> file = FontFile::Parse(font->m_FileData.data(), font->m_FileData.size(), error);
		if (!file)
		{
			BW_CORE_ERROR("Font: '{}' can't be read: {}", name, error);
			return nullptr;
		}
		font->m_Info = CreateScope<FontInfo>(FontInfo { std::move(*file), {} });
		if (!font->Build(settings))
			return nullptr;
		return font;
	}

	Font::Font() = default;
	Font::~Font() = default;

	bool Font::Build(const FontAtlasSettings& settings)
	{
		const FontFile& file = m_Info->File;
		m_Scale = 1.0f / static_cast<float>(file.GetUnitsPerEm());
		m_GlyphSize = std::max(settings.GlyphSize, 8.0f);
		m_DistanceRange = std::max(settings.DistanceRange, 1.0f);

		int ascent = 0, descent = 0, lineGap = 0;
		file.GetVerticalMetrics(ascent, descent, lineGap);
		m_Ascender = static_cast<float>(ascent) * m_Scale;
		m_Descender = static_cast<float>(descent) * m_Scale;
		m_LineHeight = static_cast<float>(ascent - descent + lineGap) * m_Scale;

		// The distance field reaches half the range outside the outline; one more pixel keeps
		// neighbors from bleeding into each other when the atlas is sampled.
		const float padding = m_DistanceRange * 0.5f + 1.0f;
		const double range = m_DistanceRange / m_GlyphSize;

		std::vector<std::pair<uint32_t, uint32_t>> wanted;	// Code point, glyph index (0 = the missing glyph)
		wanted.emplace_back(0u, 0u);
		for (const CodepointRange& codepoints : settings.Ranges)
		{
			for (uint32_t codepoint = codepoints.First; codepoint <= codepoints.Last && codepoint <= 0x10FFFF; codepoint++)
			{
				const uint32_t glyphIndex = file.FindGlyph(codepoint);
				if (glyphIndex != 0 && !m_Info->GlyphIndices.contains(codepoint))
				{
					m_Info->GlyphIndices[codepoint] = glyphIndex;
					wanted.emplace_back(codepoint, glyphIndex);
				}
			}
		}

		// Each glyph's distance field is independent, so they are generated on every core.
		std::vector<FontGlyph> glyphList(wanted.size());
		std::vector<GlyphBitmap> bitmapList(wanted.size());
		const auto generate = [&](size_t index)
		{
			const auto [codepoint, glyphIndex] = wanted[index];
			FontGlyph& glyph = glyphList[index];
			glyph.Codepoint = codepoint;
			glyph.Advance = static_cast<float>(file.GetAdvance(glyphIndex)) * m_Scale;

			msdfgen::Shape shape;
			if (!BuildShape(file, glyphIndex, m_Scale, shape))
				return;
			const msdfgen::Shape::Bounds bounds = shape.getBounds();
			const double fieldWidth = std::ceil((bounds.r - bounds.l) * m_GlyphSize + 2.0f * padding);
			const double fieldHeight = std::ceil((bounds.t - bounds.b) * m_GlyphSize + 2.0f * padding);
			// A glyph bigger than the largest atlas (a damaged outline) could never be placed in one.
			if (!(fieldWidth >= 1.0 && fieldWidth <= c_MaxAtlasSize && fieldHeight >= 1.0 && fieldHeight <= c_MaxAtlasSize))
			{
				BW_CORE_WARN("Font: '{}' has a glyph for U+{:04X} too big for its atlas; it is drawn as empty", m_Name, codepoint);
				return;
			}
			const int width = static_cast<int>(fieldWidth);
			const int height = static_cast<int>(fieldHeight);
			const msdfgen::Vector2 translate(-bounds.l + padding / m_GlyphSize, -bounds.b + padding / m_GlyphSize);

			msdfgen::Bitmap<float, 4> field(width, height);
			msdfgen::generateMTSDF(field, shape, msdfgen::Projection(msdfgen::Vector2(m_GlyphSize), translate), range);

			GlyphBitmap& bitmap = bitmapList[index];
			bitmap.Codepoint = codepoint;
			bitmap.Width = width;
			bitmap.Height = height;
			bitmap.Pixels.resize(static_cast<size_t>(width) * height * 4);
			// msdfgen's rows go up from the bottom; the atlas's go down from the top.
			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					const float* source = field(x, height - 1 - y);
					uint8_t* target = &bitmap.Pixels[(static_cast<size_t>(y) * width + x) * 4];
					for (int channel = 0; channel < 4; channel++)
						target[channel] = msdfgen::pixelFloatToByte(source[channel]);
				}
			}
			glyph.Visible = true;
			glyph.PlaneMin = glm::vec2(static_cast<float>(-translate.x), static_cast<float>(-translate.y));
			glyph.PlaneMax = glyph.PlaneMin + glm::vec2(static_cast<float>(width), static_cast<float>(height)) / m_GlyphSize;
		};
		{
			std::atomic<size_t> nextIndex = 0;
			const auto worker = [&]()
			{
				for (size_t index = nextIndex++; index < wanted.size(); index = nextIndex++)
					generate(index);
			};
			const unsigned threadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
			std::vector<std::thread> threads;
			for (unsigned i = 1; i < threadCount; i++)
				threads.emplace_back(worker);
			worker();
			for (std::thread& thread : threads)
				thread.join();
		}

		std::vector<GlyphBitmap> bitmaps;
		std::unordered_map<uint32_t, FontGlyph> glyphs;
		for (size_t i = 0; i < wanted.size(); i++)
		{
			glyphs[glyphList[i].Codepoint] = glyphList[i];
			if (glyphList[i].Visible)
				bitmaps.push_back(std::move(bitmapList[i]));
		}

		// Pack, growing the atlas until everything fits.
		size_t area = 0;
		for (const GlyphBitmap& bitmap : bitmaps)
			area += static_cast<size_t>(bitmap.Width + 1) * (bitmap.Height + 1);
		uint32_t size = std::max(256u, NextPowerOfTwo(static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(area))))));
		std::vector<stbrp_rect> rects(bitmaps.size());
		while (true)
		{
			for (size_t i = 0; i < bitmaps.size(); i++)
			{
				rects[i] = {};
				rects[i].id = static_cast<int>(i);
				rects[i].w = bitmaps[i].Width + 1;
				rects[i].h = bitmaps[i].Height + 1;
			}
			std::vector<stbrp_node> nodes(size);
			stbrp_context context;
			stbrp_init_target(&context, static_cast<int>(size), static_cast<int>(size), nodes.data(), static_cast<int>(nodes.size()));
			if (rects.empty() || stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size())))
				break;
			if (size >= c_MaxAtlasSize)
			{
				BW_CORE_ERROR("Font: the characters asked for in '{}' don't fit in a {} by {} atlas; use fewer ranges or a smaller GlyphSize",
					m_Name, c_MaxAtlasSize, c_MaxAtlasSize);
				return false;
			}
			size *= 2;
		}

		// The packer fills from the top; trim the rows it didn't need.
		int usedHeight = 1;
		for (const stbrp_rect& rect : rects)
			usedHeight = std::max(usedHeight, rect.y + rect.h);
		m_AtlasWidth = size;
		m_AtlasHeight = std::min(size, NextPowerOfTwo(static_cast<uint32_t>(usedHeight)));
		m_AtlasPixels.assign(static_cast<size_t>(m_AtlasWidth) * m_AtlasHeight * 4, 0);
		for (const stbrp_rect& rect : rects)
		{
			const GlyphBitmap& bitmap = bitmaps[static_cast<size_t>(rect.id)];
			for (int y = 0; y < bitmap.Height; y++)
			{
				std::copy_n(&bitmap.Pixels[static_cast<size_t>(y) * bitmap.Width * 4], static_cast<size_t>(bitmap.Width) * 4,
					&m_AtlasPixels[(static_cast<size_t>(rect.y + y) * size + rect.x) * 4]);
			}
			const glm::vec2 atlasSize(static_cast<float>(m_AtlasWidth), static_cast<float>(m_AtlasHeight));
			FontGlyph& glyph = glyphs[bitmap.Codepoint];
			glyph.UVMin = glm::vec2(static_cast<float>(rect.x), static_cast<float>(rect.y)) / atlasSize;
			glyph.UVMax = glm::vec2(static_cast<float>(rect.x + bitmap.Width), static_cast<float>(rect.y + bitmap.Height)) / atlasSize;
		}

		m_MissingGlyph = glyphs[0];
		glyphs.erase(0u);
		m_Glyphs = std::move(glyphs);
		BW_CORE_TRACE("Font: '{}' has {} characters in a {} by {} atlas", m_Name, m_Glyphs.size(), m_AtlasWidth, m_AtlasHeight);
		return true;
	}

	const FontGlyph& Font::GetGlyph(uint32_t codepoint) const
	{
		const auto it = m_Glyphs.find(codepoint);
		if (it != m_Glyphs.end())
			return it->second;
		if (std::find(m_ReportedMissing.begin(), m_ReportedMissing.end(), codepoint) == m_ReportedMissing.end())
		{
			m_ReportedMissing.push_back(codepoint);
			BW_CORE_WARN("Font: '{}' has no character U+{:04X} in its atlas; add its range to the font's settings", m_Name, codepoint);
		}
		return m_MissingGlyph;
	}

	float Font::GetKerning(uint32_t left, uint32_t right) const
	{
		const uint64_t key = (static_cast<uint64_t>(left) << 32) | right;
		const auto cached = m_KerningCache.find(key);
		if (cached != m_KerningCache.end())
			return cached->second;

		float kerning = 0.0f;
		const auto leftIndex = m_Info->GlyphIndices.find(left);
		const auto rightIndex = m_Info->GlyphIndices.find(right);
		if (leftIndex != m_Info->GlyphIndices.end() && rightIndex != m_Info->GlyphIndices.end())
			kerning = static_cast<float>(m_Info->File.GetKerning(leftIndex->second, rightIndex->second)) * m_Scale;
		m_KerningCache[key] = kerning;
		return kerning;
	}

	const Ref<Texture2D>& Font::GetAtlasTexture()
	{
		// No mips: a distance field is filtered by the shader, and averaging it down blurs the edges.
		if (!m_AtlasTexture)
			m_AtlasTexture = Texture2D::CreateFromRGBA8(m_AtlasWidth, m_AtlasHeight, m_AtlasPixels.data(), TextureColorSpace::Linear,
				"Font atlas " + m_Name, false);
		return m_AtlasTexture;
	}

}
