#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/Log.h"

#include <filesystem>

// BW_CORE_ASSERT / BW_ASSERT are compiled out of Dist builds.
// BW_CORE_VERIFY / BW_VERIFY always evaluate their condition and log on failure.

#ifdef BW_ENABLE_ASSERTS
	#define BW_INTERNAL_ASSERT_IMPL(type, check, ...) \
		do \
		{ \
			if (!(check)) \
			{ \
				BW##type##ERROR("Assertion failed: {} ({}:{}) {}", #check, \
					std::filesystem::path(__FILE__).filename().string(), __LINE__, fmt::format(__VA_ARGS__)); \
				::Bellows::Log::OnAssertionFailed(); \
			} \
		} while (false)

	#define BW_CORE_ASSERT(check, ...) BW_INTERNAL_ASSERT_IMPL(_CORE_, check, __VA_ARGS__)
	#define BW_ASSERT(check, ...) BW_INTERNAL_ASSERT_IMPL(_, check, __VA_ARGS__)
#else
	#define BW_CORE_ASSERT(check, ...) ((void)0)
	#define BW_ASSERT(check, ...) ((void)0)
#endif

#define BW_CORE_VERIFY(check, ...) \
	do \
	{ \
		if (!(check)) \
		{ \
			BW_CORE_ERROR("Verify failed: {} ({}:{}) {}", #check, \
				std::filesystem::path(__FILE__).filename().string(), __LINE__, fmt::format(__VA_ARGS__)); \
			::Bellows::Log::OnAssertionFailed(); \
		} \
	} while (false)
