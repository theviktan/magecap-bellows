#pragma once

#include "Bellows/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <string>

namespace Bellows {

	enum class TextureColorSpace : uint8_t
	{
		Linear = 0,	// Data textures: normals, roughness, masks
		SRGB,		// Color textures: albedo, emissive
	};

	// Immutable 2D GPU texture with a full mip chain.
	class Texture2D
	{
	public:
		// Loads PNG/JPG/TGA/BMP (8-bit) or HDR (.hdr, 32-bit float) images.
		static Ref<Texture2D> LoadFromFile(const std::filesystem::path& path, TextureColorSpace colorSpace);
		static Ref<Texture2D> LoadFromMemory(const void* data, size_t size, TextureColorSpace colorSpace, const std::string& debugName);
		// Creates a texture from tightly packed RGBA8 pixels.
		static Ref<Texture2D> CreateFromRGBA8(uint32_t width, uint32_t height, const void* pixels, TextureColorSpace colorSpace,
			const std::string& debugName, bool generateMips = true);
		// Creates a texture from tightly packed RGBA32F pixels (no mips).
		static Ref<Texture2D> CreateFromRGBA32F(uint32_t width, uint32_t height, const float* pixels, const std::string& debugName);

		nvrhi::ITexture* GetHandle() const { return m_Texture; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetMipCount() const { return m_MipCount; }
		bool IsHDR() const { return m_IsHDR; }
		const std::string& GetDebugName() const { return m_DebugName; }

		static uint32_t CalculateMipCount(uint32_t width, uint32_t height);
	private:
		nvrhi::TextureHandle m_Texture;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_MipCount = 1;
		bool m_IsHDR = false;
		std::string m_DebugName;
	};

	// GPU cube map (environment maps). Created by Environment.
	class TextureCube
	{
	public:
		TextureCube(nvrhi::TextureHandle texture, uint32_t size, uint32_t mipCount)
			: m_Texture(std::move(texture)), m_Size(size), m_MipCount(mipCount) {}

		nvrhi::ITexture* GetHandle() const { return m_Texture; }
		uint32_t GetSize() const { return m_Size; }
		uint32_t GetMipCount() const { return m_MipCount; }
	private:
		nvrhi::TextureHandle m_Texture;
		uint32_t m_Size = 0;
		uint32_t m_MipCount = 1;
	};

}
