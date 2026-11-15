#include "bwpch.h"
#include "Bellows/Core/Input.h"

namespace Bellows {

	namespace {

		struct InputState
		{
			std::array<bool, MaxKeyCode> KeysDown {};
			std::array<bool, MaxKeyCode> KeysPressed {};
			std::array<bool, MaxKeyCode> KeysReleased {};

			std::array<bool, MaxMouseButton> ButtonsDown {};
			std::array<bool, MaxMouseButton> ButtonsPressed {};
			std::array<bool, MaxMouseButton> ButtonsReleased {};

			glm::vec2 WindowMousePosition { 0.0f };
			glm::vec2 MouseDelta { 0.0f };
			glm::vec2 ScrollDelta { 0.0f };
			bool HasPreviousMousePosition = false;

			glm::vec2 ViewportPosition { 0.0f };
			glm::vec2 ViewportSize { 0.0f };

			bool Enabled = true;
		};

		InputState s_State;

	}

	void Input::BeginFrame()
	{
		s_State.KeysPressed.fill(false);
		s_State.KeysReleased.fill(false);
		s_State.ButtonsPressed.fill(false);
		s_State.ButtonsReleased.fill(false);
		s_State.MouseDelta = glm::vec2(0.0f);
		s_State.ScrollDelta = glm::vec2(0.0f);
	}

	void Input::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent& e)
		{
			if (s_State.Enabled && !e.IsRepeat())
				SetKey(e.GetKeyCode(), true);
			return false;
		});
		dispatcher.Dispatch<KeyReleasedEvent>([](KeyReleasedEvent& e)
		{
			// Releases are always processed so keys never get stuck when input is disabled mid-press.
			SetKey(e.GetKeyCode(), false);
			return false;
		});
		dispatcher.Dispatch<MouseButtonPressedEvent>([](MouseButtonPressedEvent& e)
		{
			if (s_State.Enabled)
				SetMouseButton(e.GetMouseButton(), true);
			return false;
		});
		dispatcher.Dispatch<MouseButtonReleasedEvent>([](MouseButtonReleasedEvent& e)
		{
			SetMouseButton(e.GetMouseButton(), false);
			return false;
		});
		dispatcher.Dispatch<MouseMovedEvent>([](MouseMovedEvent& e)
		{
			const glm::vec2 position(e.GetX(), e.GetY());
			if (s_State.HasPreviousMousePosition && s_State.Enabled)
				s_State.MouseDelta += position - s_State.WindowMousePosition;
			s_State.WindowMousePosition = position;
			s_State.HasPreviousMousePosition = true;
			return false;
		});
		dispatcher.Dispatch<MouseScrolledEvent>([](MouseScrolledEvent& e)
		{
			if (s_State.Enabled)
				s_State.ScrollDelta += glm::vec2(e.GetXOffset(), e.GetYOffset());
			return false;
		});
		dispatcher.Dispatch<WindowFocusEvent>([](WindowFocusEvent& e)
		{
			if (!e.IsFocused())
				ResetState();
			return false;
		});
	}

	bool Input::IsKeyDown(KeyCode key)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysDown[index];
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysPressed[index];
	}

	bool Input::IsKeyReleased(KeyCode key)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysReleased[index];
	}

	bool Input::IsMouseButtonDown(MouseButton button)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsDown[index];
	}

	bool Input::IsMouseButtonPressed(MouseButton button)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsPressed[index];
	}

	bool Input::IsMouseButtonReleased(MouseButton button)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsReleased[index];
	}

	glm::vec2 Input::GetMousePosition()
	{
		return s_State.WindowMousePosition - s_State.ViewportPosition;
	}

	glm::vec2 Input::GetMouseDelta()
	{
		return s_State.MouseDelta;
	}

	glm::vec2 Input::GetScrollDelta()
	{
		return s_State.ScrollDelta;
	}

	void Input::SetViewportRect(const glm::vec2& position, const glm::vec2& size)
	{
		s_State.ViewportPosition = position;
		s_State.ViewportSize = size;
	}

	glm::vec2 Input::GetViewportSize()
	{
		return s_State.ViewportSize;
	}

	void Input::SetEnabled(bool enabled)
	{
		if (s_State.Enabled && !enabled)
			ResetState();
		s_State.Enabled = enabled;
	}

	bool Input::IsEnabled()
	{
		return s_State.Enabled;
	}

	void Input::InjectKey(KeyCode key, bool down)
	{
		SetKey(key, down);
	}

	void Input::InjectMouseButton(MouseButton button, bool down)
	{
		SetMouseButton(button, down);
	}

	void Input::InjectMousePosition(const glm::vec2& viewportPosition)
	{
		const glm::vec2 windowPosition = viewportPosition + s_State.ViewportPosition;
		s_State.MouseDelta += windowPosition - s_State.WindowMousePosition;
		s_State.WindowMousePosition = windowPosition;
		s_State.HasPreviousMousePosition = true;
	}

	void Input::ResetState()
	{
		for (size_t i = 0; i < MaxKeyCode; i++)
		{
			if (s_State.KeysDown[i])
				s_State.KeysReleased[i] = true;
		}
		for (size_t i = 0; i < MaxMouseButton; i++)
		{
			if (s_State.ButtonsDown[i])
				s_State.ButtonsReleased[i] = true;
		}
		s_State.KeysDown.fill(false);
		s_State.ButtonsDown.fill(false);
	}

	void Input::SetKey(KeyCode key, bool down)
	{
		const auto index = static_cast<size_t>(key);
		if (index >= MaxKeyCode)
			return;

		if (down && !s_State.KeysDown[index])
			s_State.KeysPressed[index] = true;
		else if (!down && s_State.KeysDown[index])
			s_State.KeysReleased[index] = true;
		s_State.KeysDown[index] = down;
	}

	void Input::SetMouseButton(MouseButton button, bool down)
	{
		const auto index = static_cast<size_t>(button);
		if (index >= MaxMouseButton)
			return;

		if (down && !s_State.ButtonsDown[index])
			s_State.ButtonsPressed[index] = true;
		else if (!down && s_State.ButtonsDown[index])
			s_State.ButtonsReleased[index] = true;
		s_State.ButtonsDown[index] = down;
	}

}
