#pragma once

#include <cstdint>
#include <cstring>
#include <utility>

namespace Bellows {

	// Owning, move-only block of raw bytes.
	class Buffer
	{
	public:
		Buffer() = default;
		explicit Buffer(uint64_t size) { Allocate(size); }
		~Buffer() { Release(); }

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		Buffer(Buffer&& other) noexcept
			: m_Data(std::exchange(other.m_Data, nullptr)), m_Size(std::exchange(other.m_Size, 0)) {}

		Buffer& operator=(Buffer&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				m_Data = std::exchange(other.m_Data, nullptr);
				m_Size = std::exchange(other.m_Size, 0);
			}
			return *this;
		}

		static Buffer Copy(const void* data, uint64_t size)
		{
			Buffer buffer(size);
			if (size > 0)
				std::memcpy(buffer.m_Data, data, size);
			return buffer;
		}

		void Allocate(uint64_t size)
		{
			Release();
			if (size == 0)
				return;
			m_Data = new uint8_t[size];
			m_Size = size;
		}

		void Release()
		{
			delete[] m_Data;
			m_Data = nullptr;
			m_Size = 0;
		}

		uint8_t* Data() { return m_Data; }
		const uint8_t* Data() const { return m_Data; }
		uint64_t Size() const { return m_Size; }

		template<typename T>
		T* As() { return reinterpret_cast<T*>(m_Data); }

		explicit operator bool() const { return m_Data != nullptr; }
	private:
		uint8_t* m_Data = nullptr;
		uint64_t m_Size = 0;
	};

}
