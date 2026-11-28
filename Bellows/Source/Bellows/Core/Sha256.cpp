#include "bwpch.h"
#include "Bellows/Core/Sha256.h"

#include <cstring>
#include <fstream>

namespace Bellows {

	namespace {

		constexpr uint32_t c_RoundConstants[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
		};

		uint32_t RotateRight(uint32_t value, int bits) { return (value >> bits) | (value << (32 - bits)); }

	}

	Sha256::Sha256()
	{
		static constexpr uint32_t c_Initial[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
		std::memcpy(m_State, c_Initial, sizeof(m_State));
	}

	void Sha256::Transform(const uint8_t* block)
	{
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) | (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
		for (int i = 16; i < 64; i++)
		{
			const uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = m_State[0], b = m_State[1], c = m_State[2], d = m_State[3];
		uint32_t e = m_State[4], f = m_State[5], g = m_State[6], h = m_State[7];
		for (int i = 0; i < 64; i++)
		{
			const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
			const uint32_t choice = (e & f) ^ (~e & g);
			const uint32_t temp1 = h + s1 + choice + c_RoundConstants[i] + w[i];
			const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
			const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
			const uint32_t temp2 = s0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}
		m_State[0] += a; m_State[1] += b; m_State[2] += c; m_State[3] += d;
		m_State[4] += e; m_State[5] += f; m_State[6] += g; m_State[7] += h;
	}

	void Sha256::Update(const void* data, size_t size)
	{
		BW_CORE_ASSERT(!m_Finished, "Sha256::Update after Finish");
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		m_Length += size;
		while (size > 0)
		{
			const size_t take = std::min(size, sizeof(m_Buffer) - m_BufferSize);
			std::memcpy(m_Buffer + m_BufferSize, bytes, take);
			m_BufferSize += take;
			bytes += take;
			size -= take;
			if (m_BufferSize == sizeof(m_Buffer))
			{
				Transform(m_Buffer);
				m_BufferSize = 0;
			}
		}
	}

	std::array<uint8_t, 32> Sha256::Finish()
	{
		BW_CORE_ASSERT(!m_Finished, "Sha256::Finish called twice");
		const uint64_t bits = m_Length * 8;
		const uint8_t one = 0x80;
		Update(&one, 1);
		const uint8_t zero = 0;
		while (m_BufferSize != 56)
			Update(&zero, 1);
		uint8_t length[8];
		for (int i = 0; i < 8; i++)
			length[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
		Update(length, 8);
		m_Finished = true;

		std::array<uint8_t, 32> digest {};
		for (int i = 0; i < 8; i++)
		{
			digest[i * 4] = static_cast<uint8_t>(m_State[i] >> 24);
			digest[i * 4 + 1] = static_cast<uint8_t>(m_State[i] >> 16);
			digest[i * 4 + 2] = static_cast<uint8_t>(m_State[i] >> 8);
			digest[i * 4 + 3] = static_cast<uint8_t>(m_State[i]);
		}
		return digest;
	}

	std::string Sha256::ToHex(const std::array<uint8_t, 32>& digest)
	{
		static constexpr char c_Digits[] = "0123456789abcdef";
		std::string hex;
		hex.reserve(64);
		for (uint8_t byte : digest)
		{
			hex.push_back(c_Digits[byte >> 4]);
			hex.push_back(c_Digits[byte & 0x0F]);
		}
		return hex;
	}

	std::string Sha256::Hex(std::string_view data)
	{
		Sha256 hasher;
		hasher.Update(data.data(), data.size());
		return ToHex(hasher.Finish());
	}

	std::optional<std::string> Sha256::HashFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return std::nullopt;
		Sha256 hasher;
		std::vector<char> chunk(1 << 16);
		while (file)
		{
			file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
			const std::streamsize read = file.gcount();
			if (read > 0)
				hasher.Update(chunk.data(), static_cast<size_t>(read));
		}
		if (file.bad())
			return std::nullopt;
		return ToHex(hasher.Finish());
	}

}
