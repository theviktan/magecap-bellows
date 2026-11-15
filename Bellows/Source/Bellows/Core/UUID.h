#pragma once

#include <cstdint>
#include <functional>

namespace Bellows {

	// 64-bit universally unique identifier used for entities and assets.
	// A value of 0 is reserved and means "no UUID".
	class UUID
	{
	public:
		UUID();
		UUID(uint64_t uuid);
		UUID(const UUID&) = default;
		UUID& operator=(const UUID&) = default;

		operator uint64_t() const { return m_UUID; }
		bool IsValid() const { return m_UUID != 0; }
	private:
		uint64_t m_UUID;
	};

}

namespace std {

	template<>
	struct hash<Bellows::UUID>
	{
		std::size_t operator()(const Bellows::UUID& uuid) const noexcept
		{
			return std::hash<uint64_t>()(static_cast<uint64_t>(uuid));
		}
	};

}
