#include "bwpch.h"
#include "Bellows/Renderer/Texture.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Renderer.h"

#include <stb_image.h>

namespace Bellows {

	namespace {

		// Exact sRGB <-> linear conversion tables for 8-bit values.
		struct SRGBTables
		{
			std::array<float, 256> ToLinear {};

			SRGBTables()
			{
				for (int i = 0; i < 256; i++)
				{
					const float c = static_cast<float>(i) / 255.0f;
					ToLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
			}
		};

		const SRGBTables& GetSRGBTables()
		{
			static SRGBTables s_Tables;
			return s_Tables;
		}

		uint8_t LinearToSRGB8(float linear)
		{
			linear = std::clamp(linear, 0.0f, 1.0f);
			const float encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(encoded * 255.0f));
		}

		// 2x2 box-filtered downsample. Color channels are averaged in linear space for sRGB textures;
		// alpha is always linear. Odd dimensions clamp at the edge.
		std::vector<uint8_t> DownsampleRGBA8(const std::vector<uint8_t>& source, uint32_t width, uint32_t height, bool srgb)
		{
			const uint32_t newWidth = std::max(width / 2, 1u);
			const uint32_t newHeight = std::max(height / 2, 1u);
			std::vector<uint8_t> result(static_cast<size_t>(newWidth) * newHeight * 4);
			const auto& tables = GetSRGBTables();

			for (uint32_t y = 0; y < newHeight; y++)
			{
				for (uint32_t x = 0; x < newWidth; x++)
				{
					float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					for (uint32_t dy = 0; dy < 2; dy++)
					{
						for (uint32_t dx = 0; dx < 2; dx++)
						{
							const uint32_t sx = std::min(x * 2 + dx, width - 1);
							const uint32_t sy = std::min(y * 2 + dy, height - 1);
							const uint8_t* pixel = &source[(static_cast<size_t>(sy) * width + sx) * 4];
							for (int c = 0; c < 3; c++)
								sum[c] += srgb ? tables.ToLinear[pixel[c]] : static_cast<float>(pixel[c]) / 255.0f;
							sum[3] += static_cast<float>(pixel[3]) / 255.0f;
						}
					}

					uint8_t* destination = &result[(static_cast<size_t>(y) * newWidth + x) * 4];
					for (int c = 0; c < 3; c++)
					{
						const float average = sum[c] * 0.25f;
						destination[c] = srgb ? LinearToSRGB8(average) : static_cast<uint8_t>(std::lround(std::clamp(average, 0.0f, 1.0f) * 255.0f));
					}
					destination[3] = static_cast<uint8_t>(std::lround(std::clamp(sum[3] * 0.25f, 0.0f, 1.0f) * 255.0f));
				}
			}
			return result;
		}

	}

	uint32_t Texture2D::CalculateMipCount(uint32_t width, uint32_t height)
	{
		uint32_t levels = 1;
		uint32_t size = std::max(width, height);
		while (size > 1)
		{
			size /= 2;
			levels++;
		}
		return levels;
	}

	Ref<Texture2D> Texture2D::LoadFromFile(const std::filesystem::path& path, TextureColorSpace colorSpace)
	{
		Buffer data = FileSystem::ReadBinaryFile(path);
		if (!data)
		{
			BW_CORE_ERROR("Failed to read texture '{}'", path.string());
			return nullptr;
		}
		return LoadFromMemory(data.Data(), data.Size(), colorSpace, path.filename().string());
	}

	Ref<Texture2D> Texture2D::LoadFromMemory(const void* data, size_t size, TextureColorSpace colorSpace, const std::string& debugName)
	{
		const auto* bytes = static_cast<const stbi_uc*>(data);
		int width = 0, height = 0, channels = 0;

		if (stbi_is_hdr_from_memory(bytes, static_cast<int>(size)))
		{
			float* pixels = stbi_loadf_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
			if (!pixels)
			{
				BW_CORE_ERROR("Failed to decode HDR image '{}': {}", debugName, stbi_failure_reason());
				return nullptr;
			}
			Ref<Texture2D> texture = CreateFromRGBA32F(static_cast<uint32_t>(width), static_cast<uint32_t>(height), pixels, debugName);
			stbi_image_free(pixels);
			return texture;
		}

		stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
		if (!pixels)
		{
			BW_CORE_ERROR("Failed to decode image '{}': {}", debugName, stbi_failure_reason());
			return nullptr;
		}
		Ref<Texture2D> texture = CreateFromRGBA8(static_cast<uint32_t>(width), static_cast<uint32_t>(height), pixels, colorSpace, debugName);
		stbi_image_free(pixels);
		return texture;
	}

	Ref<Texture2D> Texture2D::CreateFromRGBA8(uint32_t width, uint32_t height, const void* pixels, TextureColorSpace colorSpace,
		const std::string& debugName, bool generateMips)
	{
		if (width == 0 || height == 0 || !pixels)
			return nullptr;

		const bool srgb = colorSpace == TextureColorSpace::SRGB;
		const uint32_t mipCount = generateMips ? CalculateMipCount(width, height) : 1;

		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.mipLevels = mipCount;
		desc.format = srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		desc.debugName = debugName;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;

		nvrhi::IDevice* device = Renderer::GetDevice();
		nvrhi::TextureHandle handle = device->createTexture(desc);
		if (!handle)
		{
			BW_CORE_ERROR("Failed to create texture '{}'", debugName);
			return nullptr;
		}

		Renderer::ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			std::vector<uint8_t> level(static_cast<const uint8_t*>(pixels), static_cast<const uint8_t*>(pixels) + static_cast<size_t>(width) * height * 4);
			uint32_t levelWidth = width;
			uint32_t levelHeight = height;
			for (uint32_t mip = 0; mip < mipCount; mip++)
			{
				commandList->writeTexture(handle, 0, mip, level.data(), static_cast<size_t>(levelWidth) * 4);
				if (mip + 1 < mipCount)
				{
					level = DownsampleRGBA8(level, levelWidth, levelHeight, srgb);
					levelWidth = std::max(levelWidth / 2, 1u);
					levelHeight = std::max(levelHeight / 2, 1u);
				}
			}
		});

		Ref<Texture2D> texture = CreateRef<Texture2D>();
		texture->m_Texture = handle;
		texture->m_Width = width;
		texture->m_Height = height;
		texture->m_MipCount = mipCount;
		texture->m_DebugName = debugName;
		return texture;
	}

	Ref<Texture2D> Texture2D::CreateFromRGBA32F(uint32_t width, uint32_t height, const float* pixels, const std::string& debugName)
	{
		if (width == 0 || height == 0 || !pixels)
			return nullptr;

		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA32_FLOAT;
		desc.debugName = debugName;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;

		nvrhi::TextureHandle handle = Renderer::GetDevice()->createTexture(desc);
		if (!handle)
		{
			BW_CORE_ERROR("Failed to create HDR texture '{}'", debugName);
			return nullptr;
		}

		Renderer::ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			commandList->writeTexture(handle, 0, 0, pixels, static_cast<size_t>(width) * 4 * sizeof(float));
		});

		Ref<Texture2D> texture = CreateRef<Texture2D>();
		texture->m_Texture = handle;
		texture->m_Width = width;
		texture->m_Height = height;
		texture->m_MipCount = 1;
		texture->m_IsHDR = true;
		texture->m_DebugName = debugName;
		return texture;
	}

}
