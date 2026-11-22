#pragma once

#include "Bellows/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Bellows {

	enum class TextureColorSpace : uint8_t
	{
		Linear = 0,	// Data textures: normals, roughness, masks
		SRGB,		// Color textures: albedo, emissive
	};

	// An image file decoded on the CPU, before it becomes a texture: Width * Height RGBA pixels, 8-bit,
	// or 32-bit float for high dynamic range files (.hdr, .exr). It owns the decoder's own buffer, so a
	// big image isn't copied on its way to the GPU (stb_image and tinyexr both allocate with malloc).
	struct DecodedImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		bool HighDynamicRange = false;
		std::unique_ptr<void, void (*)(void*)> Pixels { nullptr, &std::free };

		const uint8_t* GetPixels8() const { return HighDynamicRange ? nullptr : static_cast<const uint8_t*>(Pixels.get()); }
		const float* GetPixelsFloat() const { return HighDynamicRange ? static_cast<const float*>(Pixels.get()) : nullptr; }
	};

	// Immutable 2D GPU texture with a full mip chain.
	class Texture2D
	{
	public:
		// Loads PNG/JPG/TGA/BMP (8-bit) or high dynamic range images (.hdr, OpenEXR .exr).
		// High dynamic range images become 32-bit float textures (no mips) when 'keepHighDynamicRange'
		// is set (sky environments); otherwise they are clamped to 0-1 and stored as 8-bit with mips,
		// like any material map, which keeps big downloaded maps small and free of shimmer.
		static Ref<Texture2D> LoadFromFile(const std::filesystem::path& path, TextureColorSpace colorSpace, bool keepHighDynamicRange = false);
		static Ref<Texture2D> LoadFromMemory(const void* data, size_t size, TextureColorSpace colorSpace, const std::string& debugName,
			bool keepHighDynamicRange = false);
		// Decodes an image file with the same checks LoadFromMemory makes (size, formats, dimensions), without
		// a GPU. Logs why and returns nothing when the file is refused or damaged.
		static std::optional<DecodedImage> Decode(const void* data, size_t size, const std::string& debugName);
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
