#pragma once

#include "Bellows/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <atomic>
#include <string>
#include <vector>

namespace Bellows {

	class Window;

	struct RenderDeviceSpecification
	{
		// When null the device renders headless into an offscreen back buffer of HeadlessWidth x HeadlessHeight.
		Window* TargetWindow = nullptr;
		uint32_t HeadlessWidth = 1280;
		uint32_t HeadlessHeight = 720;

		bool EnableValidation = false;	// Vulkan validation layer + NVRHI validation
		bool VSync = true;
		uint32_t MaxFramesInFlight = 2;

		// Optional substring of the GPU name to prefer (also read from the BELLOWS_GPU environment variable).
		std::string PreferredAdapter;
	};

	// Owns the Vulkan instance/device, the NVRHI device wrapper and the swapchain.
	//
	// Frame flow:
	//   if (device.BeginFrame()) { ...record and execute command lists targeting GetCurrentFramebuffer()... device.Present(); }
	class RenderDevice
	{
	public:
		static Scope<RenderDevice> Create(const RenderDeviceSpecification& specification);
		~RenderDevice();

		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;

		nvrhi::IDevice* GetDevice() const { return m_Device; }

		// Acquires the next back buffer. Returns false when nothing should be rendered (minimized window).
		bool BeginFrame();
		void Present();

		nvrhi::ITexture* GetCurrentBackBuffer() const;
		nvrhi::IFramebuffer* GetCurrentFramebuffer() const;
		nvrhi::Format GetBackBufferFormat() const { return m_BackBufferFormat; }
		uint32_t GetBackBufferWidth() const { return m_BackBufferWidth; }
		uint32_t GetBackBufferHeight() const { return m_BackBufferHeight; }

		// Headless back buffer resize (windowed devices follow the window automatically).
		void ResizeHeadlessBackBuffer(uint32_t width, uint32_t height);

		void SetVSync(bool enabled);
		bool IsVSync() const { return m_Specification.VSync; }
		bool IsHeadless() const { return m_Specification.TargetWindow == nullptr; }

		void WaitIdle();
		const std::string& GetAdapterName() const { return m_AdapterName; }
		// Whether the graphics queue can write GPU timestamps, which GPU timing (GpuTimer) needs.
		bool SupportsTimestamps() const { return m_SupportsTimestamps; }
		uint64_t GetFrameIndex() const { return m_FrameIndex; }

		// Number of validation/runtime errors reported by Vulkan or NVRHI since startup.
		static uint32_t GetValidationErrorCount();
		static void ResetValidationErrorCount();
	private:
		RenderDevice() = default;

		bool Initialize(const RenderDeviceSpecification& specification);
		bool CreateInstance();
		bool PickPhysicalDevice();
		bool CreateLogicalDevice();
		bool CreateSwapchain();
		void DestroySwapchain();
		void RecreateSwapchain();
		bool CreateHeadlessBackBuffers(uint32_t width, uint32_t height);
		void Shutdown();
	private:
		struct VulkanState;
		Scope<VulkanState> m_Vulkan;

		RenderDeviceSpecification m_Specification;
		nvrhi::DeviceHandle m_Device;
		nvrhi::DeviceHandle m_NativeDevice;

		std::vector<nvrhi::TextureHandle> m_BackBuffers;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;
		nvrhi::Format m_BackBufferFormat = nvrhi::Format::RGBA8_UNORM;
		uint32_t m_BackBufferWidth = 0;
		uint32_t m_BackBufferHeight = 0;
		uint32_t m_CurrentBackBuffer = 0;

		std::vector<nvrhi::EventQueryHandle> m_FramesInFlight;
		nvrhi::CommandListHandle m_PresentCommandList;
		uint64_t m_FrameIndex = 0;
		bool m_SwapchainDirty = false;

		std::string m_AdapterName;
		bool m_SupportsTimestamps = false;
	};

}
