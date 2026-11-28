#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Bellows {

	// SHA-256 (FIPS 180-4), for checking that a downloaded file is the one a manifest names.
	class Sha256
	{
	public:
		Sha256();
		void Update(const void* data, size_t size);
		// The digest; the hasher can't take more data afterwards.
		std::array<uint8_t, 32> Finish();

		// Lowercase hexadecimal, 64 characters.
		static std::string Hex(std::string_view data);
		static std::string ToHex(const std::array<uint8_t, 32>& digest);
		// A file's digest in hexadecimal, read in pieces; nothing when the file can't be read.
		static std::optional<std::string> HashFile(const std::filesystem::path& path);
	private:
		void Transform(const uint8_t* block);
	private:
		uint32_t m_State[8];
		uint8_t m_Buffer[64];
		size_t m_BufferSize = 0;
		uint64_t m_Length = 0;		// Bytes taken so far
		bool m_Finished = false;
	};

}
