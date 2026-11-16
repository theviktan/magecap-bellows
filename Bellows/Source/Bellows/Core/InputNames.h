#pragma once

#include "Bellows/Core/KeyCodes.h"

#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	// The names keys, mouse buttons and gamepad buttons go by in project files and scripts.
	// Lookups ignore capitalization, spaces and underscores ("left_shift" finds "LeftShift").
	struct KeyName
	{
		const char* Name;
		KeyCode Code;
	};

	struct MouseButtonName
	{
		const char* Name;
		MouseButton Button;
	};

	struct GamepadButtonName
	{
		const char* Name;
		GamepadButton Button;
	};

	const std::vector<KeyName>& GetKeyCodeNames();
	const std::vector<MouseButtonName>& GetMouseButtonNames();
	const std::vector<GamepadButtonName>& GetGamepadButtonNames();

	// The name of a code, or nullptr when it has none.
	const char* GetKeyCodeName(KeyCode key);
	const char* GetMouseButtonName(MouseButton button);
	const char* GetGamepadButtonName(GamepadButton button);

	// "1" also finds D1.
	std::optional<KeyCode> FindKeyCode(const std::string& name);
	std::optional<MouseButton> FindMouseButton(const std::string& name);
	std::optional<GamepadButton> FindGamepadButton(const std::string& name);

}
