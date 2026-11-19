#include "bwpch.h"
#include "Bellows/Core/Input.h"

#include "Bellows/Text/TextLayout.h"

namespace Bellows {

	namespace {

		struct InputState
		{
			std::array<bool, MaxKeyCode> KeysDown {};
			std::array<bool, MaxKeyCode> KeysPressed {};
			std::array<bool, MaxKeyCode> KeysReleased {};
			std::array<bool, MaxKeyCode> KeysTyped {};
			std::string TypedText;
			std::string Clipboard;
			std::function<std::string()> GetClipboard;
			std::function<void(const std::string&)> SetClipboard;

			std::array<bool, MaxMouseButton> ButtonsDown {};
			std::array<bool, MaxMouseButton> ButtonsPressed {};
			std::array<bool, MaxMouseButton> ButtonsReleased {};

			bool GamepadConnected = false;
			std::array<bool, MaxGamepadButton> GamepadDown {};
			std::array<bool, MaxGamepadButton> GamepadPressed {};
			std::array<bool, MaxGamepadButton> GamepadReleased {};

			glm::vec2 WindowMousePosition { 0.0f };
			glm::vec2 MouseDelta { 0.0f };
			glm::vec2 ScrollDelta { 0.0f };
			bool HasPreviousMousePosition = false;

			glm::vec2 ViewportPosition { 0.0f };
			glm::vec2 ViewportSize { 0.0f };

			bool Enabled = true;
			InputCapture Capture;
		};

		InputState s_State;

		// Whether input of a kind the UI may have taken can be seen by this reader.
		bool Visible(bool taken, InputAccess access)
		{
			return !taken || access == InputAccess::Raw;
		}

	}

	void Input::BeginFrame()
	{
		s_State.KeysPressed.fill(false);
		s_State.KeysReleased.fill(false);
		s_State.KeysTyped.fill(false);
		s_State.TypedText.clear();
		s_State.ButtonsPressed.fill(false);
		s_State.ButtonsReleased.fill(false);
		s_State.GamepadPressed.fill(false);
		s_State.GamepadReleased.fill(false);
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
			else if (s_State.Enabled)
				InjectKeyRepeat(e.GetKeyCode());
			return false;
		});
		dispatcher.Dispatch<KeyTypedEvent>([](KeyTypedEvent& e)
		{
			if (s_State.Enabled)
				AppendTypedCodepoint(e.GetCodepoint());
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

	bool Input::IsKeyDown(KeyCode key, InputAccess access)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysDown[index] && Visible(s_State.Capture.Keyboard, access);
	}

	bool Input::IsKeyPressed(KeyCode key, InputAccess access)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysPressed[index] && Visible(s_State.Capture.Keyboard, access);
	}

	bool Input::IsKeyReleased(KeyCode key, InputAccess access)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysReleased[index] && Visible(s_State.Capture.Keyboard, access);
	}

	bool Input::IsMouseButtonDown(MouseButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsDown[index] && Visible(s_State.Capture.Pointer, access);
	}

	bool Input::IsMouseButtonPressed(MouseButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsPressed[index] && Visible(s_State.Capture.Pointer, access);
	}

	bool Input::IsMouseButtonReleased(MouseButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxMouseButton && s_State.ButtonsReleased[index] && Visible(s_State.Capture.Pointer, access);
	}

	bool Input::IsGamepadConnected()
	{
		return s_State.GamepadConnected;
	}

	bool Input::IsGamepadButtonDown(GamepadButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxGamepadButton && s_State.GamepadDown[index] && Visible(s_State.Capture.Gamepad, access);
	}

	bool Input::IsGamepadButtonPressed(GamepadButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxGamepadButton && s_State.GamepadPressed[index] && Visible(s_State.Capture.Gamepad, access);
	}

	bool Input::IsGamepadButtonReleased(GamepadButton button, InputAccess access)
	{
		const auto index = static_cast<size_t>(button);
		return index < MaxGamepadButton && s_State.GamepadReleased[index] && Visible(s_State.Capture.Gamepad, access);
	}

	void Input::UpdateGamepad(bool connected, const std::array<bool, MaxGamepadButton>& buttonsDown)
	{
		// Without a gamepad the state is left alone, so injected buttons (tests, agents) stay down.
		if (!connected)
		{
			if (s_State.GamepadConnected)
			{
				for (uint8_t i = 0; i < MaxGamepadButton; i++)
					SetGamepadButton(static_cast<GamepadButton>(i), false);
			}
			s_State.GamepadConnected = false;
			return;
		}

		s_State.GamepadConnected = true;
		for (uint8_t i = 0; i < MaxGamepadButton; i++)
		{
			// Like keys: presses are ignored while input is disabled, releases always count.
			const bool down = buttonsDown[i] && (s_State.Enabled || s_State.GamepadDown[i]);
			SetGamepadButton(static_cast<GamepadButton>(i), down);
		}
	}

	glm::vec2 Input::GetMousePosition()
	{
		return s_State.WindowMousePosition - s_State.ViewportPosition;
	}

	glm::vec2 Input::GetMouseDelta()
	{
		return s_State.MouseDelta;
	}

	glm::vec2 Input::GetScrollDelta(InputAccess access)
	{
		return Visible(s_State.Capture.Pointer, access) ? s_State.ScrollDelta : glm::vec2(0.0f);
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

	void Input::InjectKeyRepeat(KeyCode key)
	{
		const auto index = static_cast<size_t>(key);
		if (index < MaxKeyCode && s_State.KeysDown[index])
			s_State.KeysTyped[index] = true;
	}

	void Input::InjectText(const std::string& text)
	{
		for (size_t offset = 0; offset < text.size(); )
			AppendTypedCodepoint(DecodeUtf8(text, offset));
	}

	bool Input::IsKeyTyped(KeyCode key, InputAccess access)
	{
		const auto index = static_cast<size_t>(key);
		return index < MaxKeyCode && s_State.KeysTyped[index] && Visible(s_State.Capture.Keyboard, access);
	}

	const std::string& Input::GetTypedText(InputAccess access)
	{
		static const std::string c_Empty;
		return Visible(s_State.Capture.Keyboard, access) ? s_State.TypedText : c_Empty;
	}

	std::string Input::GetClipboardText()
	{
		return s_State.GetClipboard ? s_State.GetClipboard() : s_State.Clipboard;
	}

	void Input::SetClipboardText(const std::string& text)
	{
		if (s_State.SetClipboard)
			s_State.SetClipboard(text);
		else
			s_State.Clipboard = text;
	}

	void Input::SetClipboardHandlers(std::function<std::string()> get, std::function<void(const std::string&)> set)
	{
		s_State.GetClipboard = std::move(get);
		s_State.SetClipboard = std::move(set);
	}

	void Input::AppendTypedCodepoint(uint32_t codepoint)
	{
		// Control characters (and invalid code points) are keys, not text.
		if (codepoint < 0x20 || codepoint == 0x7F || (codepoint >= 0x80 && codepoint < 0xA0) || codepoint > 0x10FFFF
			|| (codepoint >= 0xD800 && codepoint <= 0xDFFF))
			return;
		AppendUtf8(s_State.TypedText, codepoint);
	}

	void Input::InjectMouseButton(MouseButton button, bool down)
	{
		SetMouseButton(button, down);
	}

	void Input::InjectGamepadButton(GamepadButton button, bool down)
	{
		SetGamepadButton(button, down);
	}

	void Input::InjectMousePosition(const glm::vec2& viewportPosition)
	{
		const glm::vec2 windowPosition = viewportPosition + s_State.ViewportPosition;
		s_State.MouseDelta += windowPosition - s_State.WindowMousePosition;
		s_State.WindowMousePosition = windowPosition;
		s_State.HasPreviousMousePosition = true;
	}

	void Input::SetCapture(const InputCapture& capture)
	{
		s_State.Capture = capture;
	}

	const InputCapture& Input::GetCapture()
	{
		return s_State.Capture;
	}

	void Input::InjectScroll(const glm::vec2& offset)
	{
		s_State.ScrollDelta += offset;
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
		for (size_t i = 0; i < MaxGamepadButton; i++)
		{
			if (s_State.GamepadDown[i])
				s_State.GamepadReleased[i] = true;
		}
		s_State.KeysDown.fill(false);
		s_State.ButtonsDown.fill(false);
		s_State.GamepadDown.fill(false);
	}

	void Input::SetKey(KeyCode key, bool down)
	{
		const auto index = static_cast<size_t>(key);
		if (index >= MaxKeyCode)
			return;

		if (down && !s_State.KeysDown[index])
		{
			s_State.KeysPressed[index] = true;
			s_State.KeysTyped[index] = true;
		}
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

	void Input::SetGamepadButton(GamepadButton button, bool down)
	{
		const auto index = static_cast<size_t>(button);
		if (index >= MaxGamepadButton)
			return;

		if (down && !s_State.GamepadDown[index])
			s_State.GamepadPressed[index] = true;
		else if (!down && s_State.GamepadDown[index])
			s_State.GamepadReleased[index] = true;
		s_State.GamepadDown[index] = down;
	}

}
