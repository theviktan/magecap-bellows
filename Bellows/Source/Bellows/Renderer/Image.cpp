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

	Image Image::Resized(uint32_t width, uint32_t height) const
	{
		Image result;
		if (!IsValid() || width == 0 || height == 0)
			return result;
		result.Width = width;
		result.Height = height;
		result.Pixels.resize(static_cast<size_t>(width) * height * 4);

		const double scaleX = static_cast<double>(Width) / width;
		const double scaleY = static_cast<double>(Height) / height;
		for (uint32_t y = 0; y < height; y++)
		{
			// The source rows and columns this pixel covers (at least one of each).
			const uint32_t y0 = static_cast<uint32_t>(y * scaleY);
			const uint32_t y1 = std::max(y0 + 1, std::min(Height, static_cast<uint32_t>((y + 1) * scaleY)));
			for (uint32_t x = 0; x < width; x++)
			{
				const uint32_t x0 = static_cast<uint32_t>(x * scaleX);
				const uint32_t x1 = std::max(x0 + 1, std::min(Width, static_cast<uint32_t>((x + 1) * scaleX)));
				uint32_t sum[4] = {};
				for (uint32_t sy = y0; sy < y1; sy++)
				{
					const uint8_t* row = &Pixels[(static_cast<size_t>(sy) * Width + x0) * 4];
					for (uint32_t sx = x0; sx < x1; sx++, row += 4)
					{
						for (int c = 0; c < 4; c++)
							sum[c] += row[c];
					}
				}
				const uint32_t count = (y1 - y0) * (x1 - x0);
				uint8_t* out = &result.Pixels[(static_cast<size_t>(y) * width + x) * 4];
				for (int c = 0; c < 4; c++)
					out[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
			}
		}
		return result;
	}

	bool Image::SaveOpaque(const std::filesystem::path& path, int jpegQuality) const
	{
		if (!IsValid())
			return false;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::vector<uint8_t> rgb(static_cast<size_t>(Width) * Height * 3);
		for (size_t i = 0, count = static_cast<size_t>(Width) * Height; i < count; i++)
		{
			rgb[i * 3 + 0] = Pixels[i * 4 + 0];
			rgb[i * 3 + 1] = Pixels[i * 4 + 1];
			rgb[i * 3 + 2] = Pixels[i * 4 + 2];
		}

		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const std::string file = path.string();
		const int result = extension == ".jpg" || extension == ".jpeg"
			? stbi_write_jpg(file.c_str(), static_cast<int>(Width), static_cast<int>(Height), 3, rgb.data(), std::clamp(jpegQuality, 1, 100))
			: stbi_write_png(file.c_str(), static_cast<int>(Width), static_cast<int>(Height), 3, rgb.data(), static_cast<int>(Width * 3));
		if (result == 0)
			BW_CORE_ERROR("Failed to write image '{}'", file);
		return result != 0;
	}

}
