#include "bwpch.h"
#include "Bellows/Renderer/Image.h"

#include <stb_image_write.h>

namespace Bellows {

	glm::u8vec4 Image::GetPixel(uint32_t x, uint32_t y) const
	{
		if (!IsValid() || x >= Width || y >= Height)
			return glm::u8vec4(0);
		const size_t index = (static_cast<size_t>(y) * Width + x) * 4;
		return glm::u8vec4(Pixels[index], Pixels[index + 1], Pixels[index + 2], Pixels[index + 3]);
	}

	glm::vec4 Image::GetPixelNormalized(uint32_t x, uint32_t y) const
	{
		return glm::vec4(GetPixel(x, y)) / 255.0f;
	}

	glm::vec4 Image::GetAverageColor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const
	{
		glm::dvec4 sum(0.0);
		uint64_t count = 0;
		for (uint32_t py = y; py < std::min(y + height, Height); py++)
		{
			for (uint32_t px = x; px < std::min(x + width, Width); px++)
			{
				sum += glm::dvec4(GetPixelNormalized(px, py));
				count++;
			}
		}
		return count > 0 ? glm::vec4(sum / static_cast<double>(count)) : glm::vec4(0.0f);
	}

	bool Image::SavePNG(const std::filesystem::path& path) const
	{
		if (!IsValid())
			return false;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		const int result = stbi_write_png(path.string().c_str(), static_cast<int>(Width), static_cast<int>(Height), 4,
			Pixels.data(), static_cast<int>(Width * 4));
		if (result == 0)
			BW_CORE_ERROR("Failed to write PNG '{}'", path.string());
		return result != 0;
	}

}
