#pragma once

#include "Bellows/Core/KeyCodes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	enum class EventType : uint8_t
	{
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowFileDrop,
		KeyPressed, KeyReleased, KeyTyped,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled
	};

#define BW_EVENT_CLASS_TYPE(type) \
	static EventType GetStaticType() { return EventType::type; } \
	EventType GetEventType() const override { return GetStaticType(); } \
	const char* GetName() const override { return #type; }

	class Event
	{
	public:
		virtual ~Event() = default;

		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;

		bool Handled = false;
	};

	// Calls the handler if the event matches its type and marks it handled when the handler returns true.
	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event)
			: m_Event(event) {}

		template<typename T, typename F>
		bool Dispatch(const F& handler)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
				return false;
			m_Event.Handled |= handler(static_cast<T&>(m_Event));
			return true;
		}
	private:
		Event& m_Event;
	};

	class WindowCloseEvent : public Event
	{
	public:
		BW_EVENT_CLASS_TYPE(WindowClose)
	};

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height)
			: m_Width(width), m_Height(height) {}

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		BW_EVENT_CLASS_TYPE(WindowResize)
	private:
		uint32_t m_Width, m_Height;
	};

	class WindowFocusEvent : public Event
	{
	public:
		explicit WindowFocusEvent(bool focused)
			: m_Focused(focused) {}

		bool IsFocused() const { return m_Focused; }

		BW_EVENT_CLASS_TYPE(WindowFocus)
	private:
		bool m_Focused;
	};

	class WindowFileDropEvent : public Event
	{
	public:
		explicit WindowFileDropEvent(std::vector<std::filesystem::path> paths)
			: m_Paths(std::move(paths)) {}

		const std::vector<std::filesystem::path>& GetPaths() const { return m_Paths; }

		BW_EVENT_CLASS_TYPE(WindowFileDrop)
	private:
		std::vector<std::filesystem::path> m_Paths;
	};

	class KeyPressedEvent : public Event
	{
	public:
		KeyPressedEvent(KeyCode key, bool isRepeat)
			: m_Key(key), m_IsRepeat(isRepeat) {}

		KeyCode GetKeyCode() const { return m_Key; }
		bool IsRepeat() const { return m_IsRepeat; }

		BW_EVENT_CLASS_TYPE(KeyPressed)
	private:
		KeyCode m_Key;
		bool m_IsRepeat;
	};

	class KeyReleasedEvent : public Event
	{
	public:
		explicit KeyReleasedEvent(KeyCode key)
			: m_Key(key) {}

		KeyCode GetKeyCode() const { return m_Key; }

		BW_EVENT_CLASS_TYPE(KeyReleased)
	private:
		KeyCode m_Key;
	};

	class KeyTypedEvent : public Event
	{
	public:
		explicit KeyTypedEvent(uint32_t codepoint)
			: m_Codepoint(codepoint) {}

		uint32_t GetCodepoint() const { return m_Codepoint; }

		BW_EVENT_CLASS_TYPE(KeyTyped)
	private:
		uint32_t m_Codepoint;
	};

	class MouseButtonPressedEvent : public Event
	{
	public:
		explicit MouseButtonPressedEvent(MouseButton button)
			: m_Button(button) {}

		MouseButton GetMouseButton() const { return m_Button; }

		BW_EVENT_CLASS_TYPE(MouseButtonPressed)
	private:
		MouseButton m_Button;
	};

	class MouseButtonReleasedEvent : public Event
	{
	public:
		explicit MouseButtonReleasedEvent(MouseButton button)
			: m_Button(button) {}

		MouseButton GetMouseButton() const { return m_Button; }

		BW_EVENT_CLASS_TYPE(MouseButtonReleased)
	private:
		MouseButton m_Button;
	};

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y)
			: m_X(x), m_Y(y) {}

		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }

		BW_EVENT_CLASS_TYPE(MouseMoved)
	private:
		float m_X, m_Y;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		BW_EVENT_CLASS_TYPE(MouseScrolled)
	private:
		float m_XOffset, m_YOffset;
	};

}
