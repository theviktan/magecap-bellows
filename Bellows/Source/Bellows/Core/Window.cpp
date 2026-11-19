#include "bwpch.h"
#include "Bellows/Core/Window.h"

#include "Bellows/Core/Input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stb_image.h>

namespace Bellows {

	namespace {

		bool s_GLFWInitialized = false;

		void GLFWErrorCallback(int error, const char* description)
		{
			BW_CORE_ERROR("GLFW error ({}): {}", error, description);
		}

	}

	bool Window::InitializeGLFW()
	{
		if (s_GLFWInitialized)
			return true;

		glfwSetErrorCallback(GLFWErrorCallback);
		if (!glfwInit())
		{
			BW_CORE_CRITICAL("Failed to initialize GLFW");
			return false;
		}
		s_GLFWInitialized = true;
		return true;
	}

	void Window::ShutdownGLFW()
	{
		if (!s_GLFWInitialized)
			return;
		glfwTerminate();
		s_GLFWInitialized = false;
	}

	Window::Window(const WindowSpecification& specification)
	{
		m_Data.Title = specification.Title;
		m_Data.Width = specification.Width;
		m_Data.Height = specification.Height;

		BW_CORE_VERIFY(s_GLFWInitialized, "GLFW must be initialized before creating a window");

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_DECORATED, specification.Decorated ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, specification.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, specification.StartMaximized ? GLFW_TRUE : GLFW_FALSE);

		GLFWmonitor* monitor = nullptr;
		if (specification.Fullscreen)
		{
			monitor = glfwGetPrimaryMonitor();
			if (const GLFWvidmode* mode = glfwGetVideoMode(monitor))
			{
				m_Data.Width = static_cast<uint32_t>(mode->width);
				m_Data.Height = static_cast<uint32_t>(mode->height);
			}
		}

		m_Window = glfwCreateWindow(static_cast<int>(m_Data.Width), static_cast<int>(m_Data.Height),
			m_Data.Title.c_str(), monitor, nullptr);
		if (!m_Window)
		{
			BW_CORE_CRITICAL("Failed to create window '{}'", m_Data.Title);
			return;
		}

		// Scripts and text fields use the system clipboard through this window.
		GLFWwindow* clipboardWindow = m_Window;
		Input::SetClipboardHandlers(
			[clipboardWindow]() { const char* text = glfwGetClipboardString(clipboardWindow); return std::string(text ? text : ""); },
			[clipboardWindow](const std::string& text) { glfwSetClipboardString(clipboardWindow, text.c_str()); });

		// Wayland has no window icons (the desktop file names one), and GLFW reports trying as an error.
		if (!specification.IconPath.empty() && glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
		{
			GLFWimage icon;
			int channels = 0;
			icon.pixels = stbi_load(specification.IconPath.c_str(), &icon.width, &icon.height, &channels, 4);
			if (icon.pixels)
			{
				glfwSetWindowIcon(m_Window, 1, &icon);
				stbi_image_free(icon.pixels);
			}
		}

		int width = 0, height = 0;
		glfwGetWindowSize(m_Window, &width, &height);
		m_Data.Width = static_cast<uint32_t>(width);
		m_Data.Height = static_cast<uint32_t>(height);

		glfwSetWindowUserPointer(m_Window, &m_Data);

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int newWidth, int newHeight)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			data.Width = static_cast<uint32_t>(newWidth);
			data.Height = static_cast<uint32_t>(newHeight);
			WindowResizeEvent event(data.Width, data.Height);
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowFocusEvent event(focused == GLFW_TRUE);
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			if (key < 0 || key >= static_cast<int>(MaxKeyCode))
				return;

			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback)
				return;

			const KeyCode keyCode = static_cast<KeyCode>(key);
			if (action == GLFW_PRESS || action == GLFW_REPEAT)
			{
				KeyPressedEvent event(keyCode, action == GLFW_REPEAT);
				data.EventCallback(event);
			}
			else if (action == GLFW_RELEASE)
			{
				KeyReleasedEvent event(keyCode);
				data.EventCallback(event);
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			KeyTypedEvent event(codepoint);
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
		{
			if (button < 0 || button >= static_cast<int>(MaxMouseButton))
				return;

			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			if (!data.EventCallback)
				return;

			const MouseButton mouseButton = static_cast<MouseButton>(button);
			if (action == GLFW_PRESS)
			{
				MouseButtonPressedEvent event(mouseButton);
				data.EventCallback(event);
			}
			else if (action == GLFW_RELEASE)
			{
				MouseButtonReleasedEvent event(mouseButton);
				data.EventCallback(event);
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
			if (data.EventCallback)
				data.EventCallback(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int pathCount, const char* paths[])
		{
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			std::vector<std::filesystem::path> droppedPaths;
			droppedPaths.reserve(static_cast<size_t>(pathCount));
			for (int i = 0; i < pathCount; i++)
				droppedPaths.emplace_back(std::filesystem::path(reinterpret_cast<const char8_t*>(paths[i])));
			WindowFileDropEvent event(std::move(droppedPaths));
			if (data.EventCallback)
				data.EventCallback(event);
		});
	}

	Window::~Window()
	{
		if (m_Window)
		{
			Input::SetClipboardHandlers(nullptr, nullptr);
			glfwDestroyWindow(m_Window);
		}
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
		PollGamepad();
	}

	void Window::PollGamepad()
	{
		std::array<bool, MaxGamepadButton> buttons {};
		for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; joystick++)
		{
			GLFWgamepadstate state;
			if (glfwJoystickIsGamepad(joystick) && glfwGetGamepadState(joystick, &state))
			{
				static_assert(MaxGamepadButton == GLFW_GAMEPAD_BUTTON_LAST + 1, "GamepadButton must follow GLFW's order");
				for (int i = 0; i < MaxGamepadButton; i++)
					buttons[i] = state.buttons[i] == GLFW_PRESS;
				Input::UpdateGamepad(true, buttons);
				return;
			}
		}
		Input::UpdateGamepad(false, buttons);
	}

	uint32_t Window::GetFramebufferWidth() const
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		return static_cast<uint32_t>(width);
	}

	uint32_t Window::GetFramebufferHeight() const
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		return static_cast<uint32_t>(height);
	}

	float Window::GetContentScale() const
	{
		float xScale = 1.0f, yScale = 1.0f;
		glfwGetWindowContentScale(m_Window, &xScale, &yScale);
		return xScale;
	}

	bool Window::IsMinimized() const
	{
		return glfwGetWindowAttrib(m_Window, GLFW_ICONIFIED) == GLFW_TRUE || GetFramebufferWidth() == 0 || GetFramebufferHeight() == 0;
	}

	void Window::SetTitle(const std::string& title)
	{
		m_Data.Title = title;
		glfwSetWindowTitle(m_Window, title.c_str());
	}

	void Window::Maximize()
	{
		glfwMaximizeWindow(m_Window);
	}

	void Window::SetCursorLocked(bool locked)
	{
		glfwSetInputMode(m_Window, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}

}
