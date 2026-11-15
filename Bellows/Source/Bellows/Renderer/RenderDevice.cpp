#include "bwpch.h"
#include "Bellows/Renderer/RenderDevice.h"

#include "Bellows/Core/Platform.h"
#include "Bellows/Core/Window.h"

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Defined in vendor/NvrhiVulkanDispatch.cpp (compiled into nvrhi_vk).
bool BellowsLoadVulkanLibrary();
void BellowsLoadVulkanInstanceFunctions(VkInstance instance);
void BellowsLoadVulkanDeviceFunctions(VkDevice device);

namespace Bellows {

	namespace {

		// Every Vulkan call goes through the dynamically loaded dispatch table.
		vk::detail::DispatchLoaderDynamic& Vk()
		{
			return VULKAN_HPP_DEFAULT_DISPATCHER;
		}

		std::atomic<uint32_t> s_ValidationErrorCount = 0;

		// Messages that are known to be harmless driver/layer chatter.
		bool IsIgnoredValidationMessage(const char* message)
		{
			constexpr std::array<std::string_view, 1> ignored = {
				"Loader Message",
			};
			const std::string_view text(message ? message : "");
			for (std::string_view pattern : ignored)
			{
				if (text.find(pattern) != std::string_view::npos)
					return true;
			}
			return false;
		}

		class NvrhiMessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
					case nvrhi::MessageSeverity::Info:
						BW_CORE_TRACE("[NVRHI] {}", messageText);
						break;
					case nvrhi::MessageSeverity::Warning:
						BW_CORE_WARN("[NVRHI] {}", messageText);
						break;
					case nvrhi::MessageSeverity::Error:
					case nvrhi::MessageSeverity::Fatal:
						s_ValidationErrorCount++;
						BW_CORE_ERROR("[NVRHI] {}", messageText);
						break;
				}
			}
		};

		NvrhiMessageCallback s_MessageCallback;

		VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
		{
			const char* message = callbackData && callbackData->pMessage ? callbackData->pMessage : "(no message)";
			if (IsIgnoredValidationMessage(message))
				return VK_FALSE;

			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				s_ValidationErrorCount++;
				BW_CORE_ERROR("[Vulkan] {}", message);
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				BW_CORE_WARN("[Vulkan] {}", message);
			}
			return VK_FALSE;
		}

		const char* PhysicalDeviceTypeToString(VkPhysicalDeviceType type)
		{
			switch (type)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:		return "Discrete GPU";
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	return "Integrated GPU";
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:		return "Virtual GPU";
				case VK_PHYSICAL_DEVICE_TYPE_CPU:				return "CPU";
				default:										return "Other";
			}
		}

		std::string ToLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

	}

	struct RenderDevice::VulkanState
	{
		VkInstance Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkDevice Device = VK_NULL_HANDLE;
		VkQueue GraphicsQueue = VK_NULL_HANDLE;
		uint32_t GraphicsQueueFamily = UINT32_MAX;

		VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
		VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;
		std::vector<VkImage> SwapchainImages;
		std::vector<VkSemaphore> AcquireSemaphores;
		std::vector<VkSemaphore> PresentSemaphores;
		uint32_t AcquireSemaphoreIndex = 0;

		std::vector<const char*> InstanceExtensions;
		std::vector<const char*> DeviceExtensions;
		std::vector<const char*> Layers;

		bool ValidationEnabled = false;
	};

	uint32_t RenderDevice::GetValidationErrorCount()
	{
		return s_ValidationErrorCount.load();
	}

	void RenderDevice::ResetValidationErrorCount()
	{
		s_ValidationErrorCount = 0;
	}

	Scope<RenderDevice> RenderDevice::Create(const RenderDeviceSpecification& specification)
	{
		Scope<RenderDevice> device(new RenderDevice());
		if (!device->Initialize(specification))
			return nullptr;
		return device;
	}

	RenderDevice::~RenderDevice()
	{
		Shutdown();
	}

	bool RenderDevice::Initialize(const RenderDeviceSpecification& specification)
	{
		m_Specification = specification;
		m_Specification.MaxFramesInFlight = std::clamp(m_Specification.MaxFramesInFlight, 1u, 3u);
		if (m_Specification.PreferredAdapter.empty())
			m_Specification.PreferredAdapter = Platform::GetEnvVar("BELLOWS_GPU");

		m_Vulkan = CreateScope<VulkanState>();

		if (!CreateInstance())
			return false;

		if (m_Specification.TargetWindow)
		{
			const VkResult result = glfwCreateWindowSurface(m_Vulkan->Instance, m_Specification.TargetWindow->GetNativeWindow(), nullptr, &m_Vulkan->Surface);
			if (result != VK_SUCCESS)
			{
				BW_CORE_CRITICAL("Failed to create a Vulkan window surface ({})", static_cast<int>(result));
				return false;
			}
		}

		if (!PickPhysicalDevice() || !CreateLogicalDevice())
			return false;

		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &s_MessageCallback;
		deviceDesc.instance = m_Vulkan->Instance;
		deviceDesc.physicalDevice = m_Vulkan->PhysicalDevice;
		deviceDesc.device = m_Vulkan->Device;
		deviceDesc.graphicsQueue = m_Vulkan->GraphicsQueue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(m_Vulkan->GraphicsQueueFamily);
		deviceDesc.instanceExtensions = m_Vulkan->InstanceExtensions.data();
		deviceDesc.numInstanceExtensions = m_Vulkan->InstanceExtensions.size();
		deviceDesc.deviceExtensions = m_Vulkan->DeviceExtensions.data();
		deviceDesc.numDeviceExtensions = m_Vulkan->DeviceExtensions.size();

		m_NativeDevice = nvrhi::vulkan::createDevice(deviceDesc);
		if (!m_NativeDevice)
		{
			BW_CORE_CRITICAL("Failed to create the NVRHI Vulkan device");
			return false;
		}

		m_Device = m_Specification.EnableValidation ? nvrhi::validation::createValidationLayer(m_NativeDevice) : m_NativeDevice;

		if (m_Specification.TargetWindow)
		{
			if (!CreateSwapchain())
				return false;
		}
		else
		{
			if (!CreateHeadlessBackBuffers(m_Specification.HeadlessWidth, m_Specification.HeadlessHeight))
				return false;
		}

		m_FramesInFlight.resize(m_Specification.MaxFramesInFlight);
		for (nvrhi::EventQueryHandle& query : m_FramesInFlight)
			query = m_Device->createEventQuery();

		m_PresentCommandList = m_Device->createCommandList();

		BW_CORE_INFO("Render device ready: {} (Vulkan, {}{})", m_AdapterName,
			IsHeadless() ? "headless" : "windowed", m_Specification.EnableValidation ? ", validation on" : "");
		return true;
	}

	bool RenderDevice::CreateInstance()
	{
		if (!BellowsLoadVulkanLibrary())
		{
			BW_CORE_CRITICAL("Vulkan is not available on this computer. Install or update the graphics driver (Vulkan 1.3 is required).");
			return false;
		}

		uint32_t extensionCount = 0;
		Vk().vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		Vk().vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

		auto hasExtension = [&availableExtensions](const char* name)
		{
			return std::any_of(availableExtensions.begin(), availableExtensions.end(),
				[name](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, name) == 0; });
		};

		if (m_Specification.TargetWindow)
		{
			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
			if (!glfwExtensions)
			{
				BW_CORE_CRITICAL("GLFW reports that Vulkan presentation is not supported on this system");
				return false;
			}
			for (uint32_t i = 0; i < glfwExtensionCount; i++)
				m_Vulkan->InstanceExtensions.push_back(glfwExtensions[i]);
		}

		if (m_Specification.EnableValidation)
		{
			uint32_t layerCount = 0;
			Vk().vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
			std::vector<VkLayerProperties> layers(layerCount);
			Vk().vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

			const bool hasValidationLayer = std::any_of(layers.begin(), layers.end(),
				[](const VkLayerProperties& p) { return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
			if (hasValidationLayer)
				m_Vulkan->Layers.push_back("VK_LAYER_KHRONOS_validation");
			else
				BW_CORE_WARN("Vulkan validation was requested but VK_LAYER_KHRONOS_validation is not installed");

			if (hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				m_Vulkan->InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				m_Vulkan->ValidationEnabled = hasValidationLayer;
			}
		}

		for (const char* extension : m_Vulkan->InstanceExtensions)
		{
			if (!hasExtension(extension))
			{
				BW_CORE_CRITICAL("Required Vulkan instance extension '{}' is not available", extension);
				return false;
			}
		}

		VkApplicationInfo applicationInfo { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		applicationInfo.pApplicationName = "MageCap Bellows";
		applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		applicationInfo.pEngineName = "MageCap Bellows";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
		applicationInfo.apiVersion = VK_API_VERSION_1_3;

		VkDebugUtilsMessengerCreateInfoEXT messengerInfo { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messengerInfo.pfnUserCallback = VulkanDebugCallback;

		VkInstanceCreateInfo instanceInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &applicationInfo;
		instanceInfo.enabledExtensionCount = static_cast<uint32_t>(m_Vulkan->InstanceExtensions.size());
		instanceInfo.ppEnabledExtensionNames = m_Vulkan->InstanceExtensions.data();
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(m_Vulkan->Layers.size());
		instanceInfo.ppEnabledLayerNames = m_Vulkan->Layers.data();
		if (m_Vulkan->ValidationEnabled)
			instanceInfo.pNext = &messengerInfo;

		const VkResult result = Vk().vkCreateInstance(&instanceInfo, nullptr, &m_Vulkan->Instance);
		if (result != VK_SUCCESS)
		{
			BW_CORE_CRITICAL("Failed to create the Vulkan instance ({}). Is a Vulkan 1.3 driver installed?", static_cast<int>(result));
			return false;
		}
		BellowsLoadVulkanInstanceFunctions(m_Vulkan->Instance);

		if (m_Vulkan->ValidationEnabled)
		{
			auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
				Vk().vkGetInstanceProcAddr(m_Vulkan->Instance, "vkCreateDebugUtilsMessengerEXT"));
			if (createMessenger)
				createMessenger(m_Vulkan->Instance, &messengerInfo, nullptr, &m_Vulkan->DebugMessenger);
		}
		return true;
	}

	bool RenderDevice::PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		Vk().vkEnumeratePhysicalDevices(m_Vulkan->Instance, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> devices(deviceCount);
		Vk().vkEnumeratePhysicalDevices(m_Vulkan->Instance, &deviceCount, devices.data());

		const std::string preferred = ToLower(m_Specification.PreferredAdapter);

		int bestScore = -1;
		for (VkPhysicalDevice device : devices)
		{
			VkPhysicalDeviceProperties properties;
			Vk().vkGetPhysicalDeviceProperties(device, &properties);

			std::string rejectReason;
			if (properties.apiVersion < VK_API_VERSION_1_3)
				rejectReason = "Vulkan 1.3 is not supported";

			VkPhysicalDeviceVulkan12Features features12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features features13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features2.pNext = &features12;
			if (rejectReason.empty())
			{
				Vk().vkGetPhysicalDeviceFeatures2(device, &features2);
				if (!features13.dynamicRendering || !features13.synchronization2 || !features12.timelineSemaphore)
					rejectReason = "required Vulkan 1.3 features are missing";
			}

			uint32_t queueFamilyCount = 0;
			Vk().vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			Vk().vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

			uint32_t graphicsFamily = UINT32_MAX;
			for (uint32_t i = 0; i < queueFamilyCount; i++)
			{
				if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
					continue;

				if (m_Vulkan->Surface)
				{
					VkBool32 presentSupported = VK_FALSE;
					Vk().vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Vulkan->Surface, &presentSupported);
					if (!presentSupported)
						continue;
				}
				graphicsFamily = i;
				break;
			}
			if (rejectReason.empty() && graphicsFamily == UINT32_MAX)
				rejectReason = "no graphics queue that can present";

			if (rejectReason.empty() && m_Vulkan->Surface)
			{
				uint32_t extensionCount = 0;
				Vk().vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
				std::vector<VkExtensionProperties> extensions(extensionCount);
				Vk().vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
				const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(),
					[](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; });
				if (!hasSwapchain)
					rejectReason = "VK_KHR_swapchain is not supported";
			}

			if (!rejectReason.empty())
			{
				BW_CORE_INFO("Skipping GPU '{}': {}", properties.deviceName, rejectReason);
				continue;
			}

			int score = 0;
			switch (properties.deviceType)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:		score = 1000; break;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	score = 500; break;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:		score = 100; break;
				case VK_PHYSICAL_DEVICE_TYPE_CPU:				score = 10; break;
				default:										score = 1; break;
			}
			if (!preferred.empty() && ToLower(properties.deviceName).find(preferred) != std::string::npos)
				score += 100000;

			BW_CORE_INFO("Found GPU '{}' ({}), score {}", properties.deviceName, PhysicalDeviceTypeToString(properties.deviceType), score);

			if (score > bestScore)
			{
				bestScore = score;
				m_Vulkan->PhysicalDevice = device;
				m_Vulkan->GraphicsQueueFamily = graphicsFamily;
				m_AdapterName = properties.deviceName;
			}
		}

		if (!m_Vulkan->PhysicalDevice)
		{
			BW_CORE_CRITICAL("No GPU supports the features MageCap Bellows needs (Vulkan 1.3 with dynamic rendering)");
			return false;
		}
		return true;
	}

	bool RenderDevice::CreateLogicalDevice()
	{
		VkPhysicalDeviceVulkan11Features supported11 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceVulkan12Features supported12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan13Features supported13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		supported11.pNext = &supported12;
		supported12.pNext = &supported13;
		VkPhysicalDeviceFeatures2 supported { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		supported.pNext = &supported11;
		Vk().vkGetPhysicalDeviceFeatures2(m_Vulkan->PhysicalDevice, &supported);

		VkPhysicalDeviceVulkan11Features enabled11 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
		VkPhysicalDeviceVulkan12Features enabled12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
		VkPhysicalDeviceVulkan13Features enabled13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		enabled11.pNext = &enabled12;
		enabled12.pNext = &enabled13;
		VkPhysicalDeviceFeatures2 enabled { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		enabled.pNext = &enabled11;

		// Optional core features are enabled when the GPU has them.
		enabled.features.samplerAnisotropy = supported.features.samplerAnisotropy;
		enabled.features.fillModeNonSolid = supported.features.fillModeNonSolid;
		enabled.features.depthClamp = supported.features.depthClamp;
		enabled.features.imageCubeArray = supported.features.imageCubeArray;
		enabled.features.independentBlend = supported.features.independentBlend;
		enabled.features.textureCompressionBC = supported.features.textureCompressionBC;
		enabled.features.shaderImageGatherExtended = supported.features.shaderImageGatherExtended;
		enabled.features.wideLines = supported.features.wideLines;

		enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
		enabled12.timelineSemaphore = VK_TRUE;
		enabled12.scalarBlockLayout = supported12.scalarBlockLayout;
		enabled12.hostQueryReset = supported12.hostQueryReset;
		enabled13.dynamicRendering = VK_TRUE;
		enabled13.synchronization2 = VK_TRUE;
		enabled13.maintenance4 = supported13.maintenance4;

		if (m_Vulkan->Surface)
			m_Vulkan->DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueInfo.queueFamilyIndex = m_Vulkan->GraphicsQueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		VkDeviceCreateInfo deviceInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceInfo.pNext = &enabled;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<uint32_t>(m_Vulkan->DeviceExtensions.size());
		deviceInfo.ppEnabledExtensionNames = m_Vulkan->DeviceExtensions.data();

		const VkResult result = Vk().vkCreateDevice(m_Vulkan->PhysicalDevice, &deviceInfo, nullptr, &m_Vulkan->Device);
		if (result != VK_SUCCESS)
		{
			BW_CORE_CRITICAL("Failed to create the Vulkan device ({})", static_cast<int>(result));
			return false;
		}
		BellowsLoadVulkanDeviceFunctions(m_Vulkan->Device);

		Vk().vkGetDeviceQueue(m_Vulkan->Device, m_Vulkan->GraphicsQueueFamily, 0, &m_Vulkan->GraphicsQueue);
		return true;
	}

	bool RenderDevice::CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR capabilities;
		Vk().vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Vulkan->PhysicalDevice, m_Vulkan->Surface, &capabilities);

		uint32_t width = m_Specification.TargetWindow->GetFramebufferWidth();
		uint32_t height = m_Specification.TargetWindow->GetFramebufferHeight();
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			width = capabilities.currentExtent.width;
			height = capabilities.currentExtent.height;
		}
		width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		if (width == 0 || height == 0)
			return false;

		uint32_t formatCount = 0;
		Vk().vkGetPhysicalDeviceSurfaceFormatsKHR(m_Vulkan->PhysicalDevice, m_Vulkan->Surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		Vk().vkGetPhysicalDeviceSurfaceFormatsKHR(m_Vulkan->PhysicalDevice, m_Vulkan->Surface, &formatCount, formats.data());

		// The tonemap pass writes display-encoded (sRGB) values, so a UNORM swapchain is used.
		VkSurfaceFormatKHR surfaceFormat = formats.empty() ? VkSurfaceFormatKHR { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
		for (const VkSurfaceFormatKHR& format : formats)
		{
			if ((format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM)
				&& format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				surfaceFormat = format;
				break;
			}
		}

		uint32_t presentModeCount = 0;
		Vk().vkGetPhysicalDeviceSurfacePresentModesKHR(m_Vulkan->PhysicalDevice, m_Vulkan->Surface, &presentModeCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		Vk().vkGetPhysicalDeviceSurfacePresentModesKHR(m_Vulkan->PhysicalDevice, m_Vulkan->Surface, &presentModeCount, presentModes.data());

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;	// Always available
		if (!m_Specification.VSync)
		{
			auto supports = [&presentModes](VkPresentModeKHR mode) { return std::find(presentModes.begin(), presentModes.end(), mode) != presentModes.end(); };
			if (supports(VK_PRESENT_MODE_MAILBOX_KHR))
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			else if (supports(VK_PRESENT_MODE_IMMEDIATE_KHR))
				presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}

		uint32_t imageCount = std::max(capabilities.minImageCount + 1, 3u);
		if (capabilities.maxImageCount > 0)
			imageCount = std::min(imageCount, capabilities.maxImageCount);

		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		if (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
			usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		VkSwapchainCreateInfoKHR swapchainInfo { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		swapchainInfo.surface = m_Vulkan->Surface;
		swapchainInfo.minImageCount = imageCount;
		swapchainInfo.imageFormat = surfaceFormat.format;
		swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
		swapchainInfo.imageExtent = { width, height };
		swapchainInfo.imageArrayLayers = 1;
		swapchainInfo.imageUsage = usage;
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchainInfo.preTransform = capabilities.currentTransform;
		swapchainInfo.compositeAlpha = (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
			? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		swapchainInfo.presentMode = presentMode;
		swapchainInfo.clipped = VK_TRUE;
		swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

		VkResult result = Vk().vkCreateSwapchainKHR(m_Vulkan->Device, &swapchainInfo, nullptr, &m_Vulkan->Swapchain);
		if (result != VK_SUCCESS)
		{
			BW_CORE_ERROR("Failed to create the swapchain ({})", static_cast<int>(result));
			return false;
		}

		m_Vulkan->SwapchainFormat = surfaceFormat.format;
		m_BackBufferFormat = surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		m_BackBufferWidth = width;
		m_BackBufferHeight = height;

		uint32_t swapchainImageCount = 0;
		Vk().vkGetSwapchainImagesKHR(m_Vulkan->Device, m_Vulkan->Swapchain, &swapchainImageCount, nullptr);
		m_Vulkan->SwapchainImages.resize(swapchainImageCount);
		Vk().vkGetSwapchainImagesKHR(m_Vulkan->Device, m_Vulkan->Swapchain, &swapchainImageCount, m_Vulkan->SwapchainImages.data());

		for (uint32_t i = 0; i < swapchainImageCount; i++)
		{
			nvrhi::TextureDesc textureDesc;
			textureDesc.width = width;
			textureDesc.height = height;
			textureDesc.format = m_BackBufferFormat;
			textureDesc.debugName = "SwapchainImage" + std::to_string(i);
			textureDesc.isRenderTarget = true;
			textureDesc.isShaderResource = false;
			textureDesc.initialState = nvrhi::ResourceStates::Present;
			textureDesc.keepInitialState = true;

			nvrhi::TextureHandle texture = m_Device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image,
				nvrhi::Object(m_Vulkan->SwapchainImages[i]), textureDesc);
			m_BackBuffers.push_back(texture);
			m_Framebuffers.push_back(m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
		}

		VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		m_Vulkan->AcquireSemaphores.resize(m_Specification.MaxFramesInFlight + 1);
		for (VkSemaphore& semaphore : m_Vulkan->AcquireSemaphores)
			Vk().vkCreateSemaphore(m_Vulkan->Device, &semaphoreInfo, nullptr, &semaphore);
		m_Vulkan->PresentSemaphores.resize(swapchainImageCount);
		for (VkSemaphore& semaphore : m_Vulkan->PresentSemaphores)
			Vk().vkCreateSemaphore(m_Vulkan->Device, &semaphoreInfo, nullptr, &semaphore);

		m_SwapchainDirty = false;
		return true;
	}

	void RenderDevice::DestroySwapchain()
	{
		if (m_Device)
			m_Device->waitForIdle();

		m_Framebuffers.clear();
		m_BackBuffers.clear();

		if (m_Vulkan->Device)
		{
			for (VkSemaphore semaphore : m_Vulkan->AcquireSemaphores)
				Vk().vkDestroySemaphore(m_Vulkan->Device, semaphore, nullptr);
			for (VkSemaphore semaphore : m_Vulkan->PresentSemaphores)
				Vk().vkDestroySemaphore(m_Vulkan->Device, semaphore, nullptr);
			if (m_Vulkan->Swapchain)
				Vk().vkDestroySwapchainKHR(m_Vulkan->Device, m_Vulkan->Swapchain, nullptr);
		}
		m_Vulkan->AcquireSemaphores.clear();
		m_Vulkan->PresentSemaphores.clear();
		m_Vulkan->SwapchainImages.clear();
		m_Vulkan->Swapchain = VK_NULL_HANDLE;
		m_Vulkan->AcquireSemaphoreIndex = 0;
	}

	void RenderDevice::RecreateSwapchain()
	{
		DestroySwapchain();
		CreateSwapchain();
	}

	bool RenderDevice::CreateHeadlessBackBuffers(uint32_t width, uint32_t height)
	{
		m_Framebuffers.clear();
		m_BackBuffers.clear();

		m_BackBufferFormat = nvrhi::Format::RGBA8_UNORM;
		m_BackBufferWidth = std::max(width, 1u);
		m_BackBufferHeight = std::max(height, 1u);

		for (uint32_t i = 0; i < m_Specification.MaxFramesInFlight; i++)
		{
			nvrhi::TextureDesc textureDesc;
			textureDesc.width = m_BackBufferWidth;
			textureDesc.height = m_BackBufferHeight;
			textureDesc.format = m_BackBufferFormat;
			textureDesc.debugName = "HeadlessBackBuffer" + std::to_string(i);
			textureDesc.isRenderTarget = true;
			textureDesc.isShaderResource = true;
			textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			textureDesc.keepInitialState = true;
			textureDesc.setClearValue(nvrhi::Color(0.0f));

			nvrhi::TextureHandle texture = m_Device->createTexture(textureDesc);
			if (!texture)
				return false;
			m_BackBuffers.push_back(texture);
			m_Framebuffers.push_back(m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
		}
		m_CurrentBackBuffer = 0;
		return true;
	}

	void RenderDevice::ResizeHeadlessBackBuffer(uint32_t width, uint32_t height)
	{
		if (!IsHeadless() || (width == m_BackBufferWidth && height == m_BackBufferHeight))
			return;
		WaitIdle();
		CreateHeadlessBackBuffers(width, height);
	}

	bool RenderDevice::BeginFrame()
	{
		if (IsHeadless())
		{
			m_CurrentBackBuffer = static_cast<uint32_t>(m_FrameIndex % m_BackBuffers.size());
			return true;
		}

		Window* window = m_Specification.TargetWindow;
		if (window->IsMinimized())
			return false;

		if (m_SwapchainDirty || !m_Vulkan->Swapchain
			|| window->GetFramebufferWidth() != m_BackBufferWidth || window->GetFramebufferHeight() != m_BackBufferHeight)
		{
			RecreateSwapchain();
			if (!m_Vulkan->Swapchain)
				return false;
		}

		for (int attempt = 0; attempt < 2; attempt++)
		{
			VkSemaphore acquireSemaphore = m_Vulkan->AcquireSemaphores[m_Vulkan->AcquireSemaphoreIndex];
			const VkResult result = Vk().vkAcquireNextImageKHR(m_Vulkan->Device, m_Vulkan->Swapchain, UINT64_MAX,
				acquireSemaphore, VK_NULL_HANDLE, &m_CurrentBackBuffer);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				RecreateSwapchain();
				if (!m_Vulkan->Swapchain)
					return false;
				continue;
			}
			if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				BW_CORE_ERROR("vkAcquireNextImageKHR failed ({})", static_cast<int>(result));
				return false;
			}
			if (result == VK_SUBOPTIMAL_KHR)
				m_SwapchainDirty = true;

			m_Vulkan->AcquireSemaphoreIndex = (m_Vulkan->AcquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_Vulkan->AcquireSemaphores.size());

			nvrhi::vulkan::IDevice* vulkanDevice = static_cast<nvrhi::vulkan::IDevice*>(m_NativeDevice.Get());
			vulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquireSemaphore, 0);
			return true;
		}
		return false;
	}

	void RenderDevice::Present()
	{
		if (!IsHeadless())
		{
			VkSemaphore presentSemaphore = m_Vulkan->PresentSemaphores[m_CurrentBackBuffer];
			nvrhi::vulkan::IDevice* vulkanDevice = static_cast<nvrhi::vulkan::IDevice*>(m_NativeDevice.Get());
			vulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, presentSemaphore, 0);

			// An empty submission flushes the semaphore signal and returns the back buffer to the present state.
			m_PresentCommandList->open();
			m_PresentCommandList->close();
			m_Device->executeCommandList(m_PresentCommandList);

			VkPresentInfoKHR presentInfo { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = &presentSemaphore;
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &m_Vulkan->Swapchain;
			presentInfo.pImageIndices = &m_CurrentBackBuffer;

			const VkResult result = Vk().vkQueuePresentKHR(m_Vulkan->GraphicsQueue, &presentInfo);
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
				m_SwapchainDirty = true;
			else if (result != VK_SUCCESS)
				BW_CORE_ERROR("vkQueuePresentKHR failed ({})", static_cast<int>(result));
		}

		// Limit the CPU to MaxFramesInFlight frames ahead of the GPU.
		nvrhi::EventQueryHandle& query = m_FramesInFlight[m_FrameIndex % m_FramesInFlight.size()];
		if (m_FrameIndex >= m_FramesInFlight.size())
			m_Device->waitEventQuery(query);
		m_Device->resetEventQuery(query);
		m_Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);

		m_Device->runGarbageCollection();
		m_FrameIndex++;
	}

	nvrhi::ITexture* RenderDevice::GetCurrentBackBuffer() const
	{
		return m_BackBuffers.empty() ? nullptr : m_BackBuffers[m_CurrentBackBuffer].Get();
	}

	nvrhi::IFramebuffer* RenderDevice::GetCurrentFramebuffer() const
	{
		return m_Framebuffers.empty() ? nullptr : m_Framebuffers[m_CurrentBackBuffer].Get();
	}

	void RenderDevice::SetVSync(bool enabled)
	{
		if (m_Specification.VSync == enabled)
			return;
		m_Specification.VSync = enabled;
		m_SwapchainDirty = true;
	}

	void RenderDevice::WaitIdle()
	{
		if (m_Device)
			m_Device->waitForIdle();
	}

	void RenderDevice::Shutdown()
	{
		if (!m_Vulkan)
			return;

		WaitIdle();
		if (m_Device)
			m_Device->runGarbageCollection();

		m_PresentCommandList = nullptr;
		m_FramesInFlight.clear();

		if (m_Vulkan->Swapchain)
			DestroySwapchain();
		m_Framebuffers.clear();
		m_BackBuffers.clear();

		m_Device = nullptr;
		m_NativeDevice = nullptr;

		if (m_Vulkan->Device)
			Vk().vkDestroyDevice(m_Vulkan->Device, nullptr);
		if (m_Vulkan->Surface)
			Vk().vkDestroySurfaceKHR(m_Vulkan->Instance, m_Vulkan->Surface, nullptr);
		if (m_Vulkan->DebugMessenger)
		{
			auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				Vk().vkGetInstanceProcAddr(m_Vulkan->Instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (destroyMessenger)
				destroyMessenger(m_Vulkan->Instance, m_Vulkan->DebugMessenger, nullptr);
		}
		if (m_Vulkan->Instance)
			Vk().vkDestroyInstance(m_Vulkan->Instance, nullptr);

		m_Vulkan.reset();
	}

}
