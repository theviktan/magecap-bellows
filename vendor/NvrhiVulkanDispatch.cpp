// Storage for the Vulkan-Hpp dynamic dispatcher, shared by NVRHI's Vulkan backend and the engine.
// NVRHI is built as a static library, which leaves the definition to the application; it is
// compiled into nvrhi_vk itself (see vendor/CMakeLists.txt) so link order never matters.
//
// The Vulkan loader (vulkan-1.dll / libvulkan.so.1) is opened at run time instead of being
// linked, so the engine starts - and reports a clear error - on machines without Vulkan.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <exception>
#include <optional>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace {

	std::optional<vk::detail::DynamicLoader> s_VulkanLibrary;

}

// Opens the Vulkan loader and loads the global entry points. Returns false when Vulkan is not installed.
bool BellowsLoadVulkanLibrary()
{
	if (!s_VulkanLibrary)
	{
		try
		{
			s_VulkanLibrary.emplace();
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	const auto getInstanceProcAddr = s_VulkanLibrary->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	if (!getInstanceProcAddr)
		return false;
	VULKAN_HPP_DEFAULT_DISPATCHER.init(getInstanceProcAddr);
	return true;
}

// Loads the instance-level entry points (call right after vkCreateInstance).
void BellowsLoadVulkanInstanceFunctions(VkInstance instance)
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance));
}

// Loads the device-level entry points (call right after vkCreateDevice, before nvrhi::vulkan::createDevice).
void BellowsLoadVulkanDeviceFunctions(VkDevice device)
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(device));
}
