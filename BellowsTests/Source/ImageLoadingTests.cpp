#include <doctest/doctest.h>

#include "Bellows/Renderer/Texture.h"

#include <cstdint>
#include <vector>

using namespace Bellows;

namespace {

	void AppendBigEndian(std::vector<uint8_t>& bytes, uint32_t value)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
			bytes.push_back(static_cast<uint8_t>(value >> shift));
	}

	// A PNG that is only its header: it says width x height, RGBA, and has no pixel data.
	std::vector<uint8_t> PngHeader(uint32_t width, uint32_t height)
	{
		std::vector<uint8_t> bytes = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		AppendBigEndian(bytes, 13);
		bytes.insert(bytes.end(), { 'I', 'H', 'D', 'R' });
		AppendBigEndian(bytes, width);
		AppendBigEndian(bytes, height);
		bytes.insert(bytes.end(), { 8, 6, 0, 0, 0 });		// 8 bits, RGBA, standard compression, filters, no interlace
		AppendBigEndian(bytes, 0);							// CRC (stb_image doesn't check it)
		AppendBigEndian(bytes, 0);
		bytes.insert(bytes.end(), { 'I', 'E', 'N', 'D' });
		AppendBigEndian(bytes, 0);
		return bytes;
	}

}

TEST_CASE("Image files from strangers: oversized and unused formats are refused before decoding")
{
	SUBCASE("An image claiming to be wider or taller than 16384 pixels")
	{
		for (const auto& [width, height] : { std::pair(20000u, 16u), std::pair(16u, 20000u), std::pair(65535u, 65535u) })
		{
			const std::vector<uint8_t> png = PngHeader(width, height);
			CHECK_FALSE(Texture2D::LoadFromMemory(png.data(), png.size(), TextureColorSpace::SRGB, "huge.png"));
		}
	}
	SUBCASE("Formats Bellows doesn't use, even when the bytes are a real image of them")
	{
		// A 1 x 1 GIF and a 1 x 1 PNM: stb_image is built without these decoders.
		const std::vector<uint8_t> gif = { 'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF,
			',', 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 1, 0, ';' };
		const std::vector<uint8_t> pnm = { 'P', '6', '\n', '1', ' ', '1', '\n', '2', '5', '5', '\n', 255, 0, 0 };
		CHECK_FALSE(Texture2D::LoadFromMemory(gif.data(), gif.size(), TextureColorSpace::SRGB, "picture.gif"));
		CHECK_FALSE(Texture2D::LoadFromMemory(pnm.data(), pnm.size(), TextureColorSpace::SRGB, "picture.ppm"));
	}
	SUBCASE("No bytes at all")
	{
		const uint8_t nothing = 0;
		CHECK_FALSE(Texture2D::LoadFromMemory(&nothing, 0, TextureColorSpace::SRGB, "empty.png"));
		CHECK_FALSE(Texture2D::LoadFromMemory(nullptr, 16, TextureColorSpace::SRGB, "null.png"));
	}
}
