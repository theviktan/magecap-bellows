#include "bwpch.h"
#include "Bellows/Core/UUID.h"

#include <random>

namespace Bellows {

	namespace {

		std::mt19937_64& GetEngine()
		{
			thread_local std::mt19937_64 s_Engine(std::random_device{}() ^
				static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
			return s_Engine;
		}

	}

	UUID::UUID()
	{
		std::uniform_int_distribution<uint64_t> distribution(1, UINT64_MAX);
		m_UUID = distribution(GetEngine());
	}

	UUID::UUID(uint64_t uuid)
		: m_UUID(uuid)
	{
	}

}
