#pragma once

#include <cstdint>
#include <memory>
#include <utility>

// -----------------------------------------------------------------------------
// Platform detection
// -----------------------------------------------------------------------------
#if defined(_WIN32)
	#define BW_PLATFORM_WINDOWS
#elif defined(__linux__)
	#define BW_PLATFORM_LINUX
#else
	#error "MageCap Bellows only supports Windows and Linux."
#endif

#if defined(BW_PLATFORM_WINDOWS)
	#define BW_DEBUGBREAK() __debugbreak()
#else
	#include <csignal>
	#define BW_DEBUGBREAK() std::raise(SIGTRAP)
#endif

#define BW_EXPAND_MACRO(x) x
#define BW_STRINGIFY_MACRO(x) #x
#define BIT(x) (1u << (x))

#define BW_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Bellows {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	using byte = uint8_t;

}
