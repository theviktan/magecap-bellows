#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <glm/vec4.hpp>

namespace Bellows {

	// CPU-side RGBA8 image (screenshots, readbacks, procedural textures).
	struct Image
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;	// RGBA8, rows top to bottom, tightly packed

		bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == static_cast<size_t>(Width) * Height * 4; }

		glm::u8vec4 GetPixel(uint32_t x, uint32_t y) const;
		glm::vec4 GetPixelNormalized(uint32_t x, uint32_t y) const;

		// Average color of a rectangle, normalized to [0, 1].
		glm::vec4 GetAverageColor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;

		bool SavePNG(const std::filesystem::path& path) const;
	};

}
