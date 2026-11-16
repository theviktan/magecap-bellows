#pragma once

#include "Bellows/Core/Events.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Bellows {

	struct WindowSpecification
	{
		std::string Title = "MageCap Bellows";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Decorated = true;
		bool Fullscreen = false;
		bool Resizable = true;
		bool StartMaximized = false;
		std::string IconPath;	// Optional PNG used as the window/taskbar icon
	};

	// A native OS window backed by GLFW. Rendering surfaces are created by the RenderDevice.
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		explicit Window(const WindowSpecification& specification);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		// Processes window events and reads the first gamepad into Input.
		void PollEvents();

		uint32_t GetWidth() const { return m_Data.Width; }
		uint32_t GetHeight() const { return m_Data.Height; }
		uint32_t GetFramebufferWidth() const;
		uint32_t GetFramebufferHeight() const;
		float GetContentScale() const;
		bool IsMinimized() const;

		void SetTitle(const std::string& title);
		const std::string& GetTitle() const { return m_Data.Title; }
		void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
		void Maximize();
		void SetCursorLocked(bool locked);

		GLFWwindow* GetNativeWindow() const { return m_Window; }

		// GLFW must be initialized once per process before any window is created.
		static bool InitializeGLFW();
		static void ShutdownGLFW();
	private:
		void PollGamepad();
	private:
		struct WindowData
		{
			std::string Title;
			uint32_t Width = 0;
			uint32_t Height = 0;
			EventCallbackFn EventCallback;
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

}
