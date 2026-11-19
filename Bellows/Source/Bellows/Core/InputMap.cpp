#include "bwpch.h"
#include "Bellows/Core/InputMap.h"

#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputNames.h"

namespace Bellows {

	namespace {

		InputActionMap s_Actions;

		const std::vector<InputBinding>* FindBindings(const std::string& action)
		{
			auto it = s_Actions.find(action);
			return it != s_Actions.end() ? &it->second : nullptr;
		}

		bool EqualsIgnoreCase(const std::string& a, const char* b)
		{
			const size_t length = std::strlen(b);
			if (a.size() != length)
				return false;
			for (size_t i = 0; i < length; i++)
			{
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
					return false;
			}
			return true;
		}

	}

	std::string InputBinding::ToString() const
	{
		const char* name = nullptr;
		const char* device = "Key";
		switch (Source)
		{
			case Device::Key:		name = GetKeyCodeName(static_cast<KeyCode>(Code)); break;
			case Device::Mouse:		name = GetMouseButtonName(static_cast<MouseButton>(Code)); device = "Mouse"; break;
			case Device::Gamepad:	name = GetGamepadButtonName(static_cast<GamepadButton>(Code)); device = "Gamepad"; break;
		}
		return std::string(device) + ":" + (name ? name : std::to_string(Code));
	}

	std::optional<InputBinding> InputBinding::Parse(const std::string& text)
	{
		const size_t colon = text.find(':');
		if (colon == std::string::npos)
			return std::nullopt;
		const std::string device = text.substr(0, colon);
		const std::string name = text.substr(colon + 1);

		if (EqualsIgnoreCase(device, "Key"))
		{
			if (std::optional<KeyCode> key = FindKeyCode(name))
				return Key(*key);
		}
		else if (EqualsIgnoreCase(device, "Mouse"))
		{
			if (std::optional<MouseButton> button = FindMouseButton(name))
				return Mouse(*button);
		}
		else if (EqualsIgnoreCase(device, "Gamepad"))
		{
			if (std::optional<GamepadButton> button = FindGamepadButton(name))
				return Gamepad(*button);
		}
		return std::nullopt;
	}

	bool InputBinding::IsDown(InputAccess access) const
	{
		switch (Source)
		{
			case Device::Key:		return Input::IsKeyDown(static_cast<KeyCode>(Code), access);
			case Device::Mouse:		return Input::IsMouseButtonDown(static_cast<MouseButton>(Code), access);
			case Device::Gamepad:	return Input::IsGamepadButtonDown(static_cast<GamepadButton>(Code), access);
		}
		return false;
	}

	bool InputBinding::IsPressed(InputAccess access) const
	{
		switch (Source)
		{
			case Device::Key:		return Input::IsKeyPressed(static_cast<KeyCode>(Code), access);
			case Device::Mouse:		return Input::IsMouseButtonPressed(static_cast<MouseButton>(Code), access);
			case Device::Gamepad:	return Input::IsGamepadButtonPressed(static_cast<GamepadButton>(Code), access);
		}
		return false;
	}

	bool InputBinding::IsReleased(InputAccess access) const
	{
		switch (Source)
		{
			case Device::Key:		return Input::IsKeyReleased(static_cast<KeyCode>(Code), access);
			case Device::Mouse:		return Input::IsMouseButtonReleased(static_cast<MouseButton>(Code), access);
			case Device::Gamepad:	return Input::IsGamepadButtonReleased(static_cast<GamepadButton>(Code), access);
		}
		return false;
	}

	void InputMap::SetActions(InputActionMap actions)
	{
		s_Actions = std::move(actions);
	}

	const InputActionMap& InputMap::GetActions()
	{
		return s_Actions;
	}

	bool InputMap::HasAction(const std::string& action)
	{
		return FindBindings(action) != nullptr;
	}

	bool InputMap::IsActionDown(const std::string& action, InputAccess access)
	{
		const std::vector<InputBinding>* bindings = FindBindings(action);
		return bindings && std::any_of(bindings->begin(), bindings->end(), [access](const InputBinding& b) { return b.IsDown(access); });
	}

	bool InputMap::IsActionPressed(const std::string& action, InputAccess access)
	{
		const std::vector<InputBinding>* bindings = FindBindings(action);
		if (!bindings)
			return false;
		// A second input going down while the first is still held does not start the action again.
		bool pressed = false;
		for (const InputBinding& binding : *bindings)
		{
			if (binding.IsPressed(access))
				pressed = true;
			else if (binding.IsDown(access))
				return false;
		}
		return pressed;
	}

	bool InputMap::IsActionReleased(const std::string& action, InputAccess access)
	{
		const std::vector<InputBinding>* bindings = FindBindings(action);
		if (!bindings)
			return false;
		bool released = false;
		for (const InputBinding& binding : *bindings)
		{
			if (binding.IsDown(access))
				return false;
			released |= binding.IsReleased(access);
		}
		return released;
	}

	InputActionMap InputMap::GetDefaultActions()
	{
		using B = InputBinding;
		InputActionMap actions {
			{ "move_left", { B::Key(KeyCode::A), B::Key(KeyCode::Left), B::Gamepad(GamepadButton::DPadLeft) } },
			{ "move_right", { B::Key(KeyCode::D), B::Key(KeyCode::Right), B::Gamepad(GamepadButton::DPadRight) } },
			{ "move_forward", { B::Key(KeyCode::W), B::Key(KeyCode::Up), B::Gamepad(GamepadButton::DPadUp) } },
			{ "move_back", { B::Key(KeyCode::S), B::Key(KeyCode::Down), B::Gamepad(GamepadButton::DPadDown) } },
			{ "jump", { B::Key(KeyCode::Space), B::Gamepad(GamepadButton::A) } },
			{ "interact", { B::Key(KeyCode::E), B::Gamepad(GamepadButton::X) } },
			{ "pause", { B::Key(KeyCode::Escape), B::Gamepad(GamepadButton::Start) } },
		};
		actions.merge(GetDefaultUIActions());
		return actions;
	}

	InputActionMap InputMap::GetDefaultUIActions()
	{
		// Shift+Tab also goes back: the UI treats ui_next with Shift held as ui_previous.
		using B = InputBinding;
		return {
			{ "ui_accept", { B::Key(KeyCode::Enter), B::Key(KeyCode::KPEnter), B::Gamepad(GamepadButton::A) } },
			{ "ui_cancel", { B::Key(KeyCode::Escape), B::Gamepad(GamepadButton::B) } },
			{ "ui_up", { B::Key(KeyCode::Up), B::Gamepad(GamepadButton::DPadUp) } },
			{ "ui_down", { B::Key(KeyCode::Down), B::Gamepad(GamepadButton::DPadDown) } },
			{ "ui_left", { B::Key(KeyCode::Left), B::Gamepad(GamepadButton::DPadLeft) } },
			{ "ui_right", { B::Key(KeyCode::Right), B::Gamepad(GamepadButton::DPadRight) } },
			{ "ui_next", { B::Key(KeyCode::Tab), B::Gamepad(GamepadButton::RightBumper) } },
			{ "ui_previous", { B::Gamepad(GamepadButton::LeftBumper) } },
		};
	}

	void InputMap::AddMissingUIActions(InputActionMap& actions)
	{
		// A project that rebound a UI action keeps its bindings; only missing actions are added.
		for (auto& [name, bindings] : GetDefaultUIActions())
			actions.try_emplace(name, std::move(bindings));
	}

}
