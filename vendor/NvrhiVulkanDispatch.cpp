// Storage for the Vulkan-Hpp dynamic dispatcher used by NVRHI's Vulkan backend.
// NVRHI is built as a static library, which leaves the definition to the application;
// it is compiled into nvrhi_vk itself (see vendor/CMakeLists.txt) so link order never matters.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// Loads the Vulkan entry points used by NVRHI. Must be called before nvrhi::vulkan::createDevice().
void BellowsInitializeVulkanDispatcher(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr, VkDevice device)
{
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance), getInstanceProcAddr, vk::Device(device));
}
