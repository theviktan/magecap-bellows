#pragma once

#include "Bellows/Core/Input.h"
#include "Bellows/Core/KeyCodes.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	// One input that can trigger an action: a key, a mouse button or a gamepad button.
	struct InputBinding
	{
		enum class Device : uint8_t { Key, Mouse, Gamepad };

		Device Source = Device::Key;
		uint16_t Code = 0;

		static InputBinding Key(KeyCode key) { return { Device::Key, static_cast<uint16_t>(key) }; }
		static InputBinding Mouse(MouseButton button) { return { Device::Mouse, static_cast<uint16_t>(button) }; }
		static InputBinding Gamepad(GamepadButton button) { return { Device::Gamepad, static_cast<uint16_t>(button) }; }

		// The project-file spelling: "Key:W", "Mouse:Left", "Gamepad:A".
		std::string ToString() const;
		// Accepts the ToString spelling with any capitalization; nullopt if the device or name is unknown.
		static std::optional<InputBinding> Parse(const std::string& text);

		bool IsDown(InputAccess access = InputAccess::Game) const;
		bool IsPressed(InputAccess access = InputAccess::Game) const;
		bool IsReleased(InputAccess access = InputAccess::Game) const;

		bool operator==(const InputBinding& other) const = default;
	};

	// Action name -> the inputs bound to it ("jump" -> Space, gamepad A).
	using InputActionMap = std::map<std::string, std::vector<InputBinding>>;

	// Named input actions, so games ask "is jump pressed?" instead of naming keys, and players
	// can use a keyboard or a gamepad alike. The active project fills the map (Project Settings).
	//   - IsActionDown:     any bound input is held
	//   - IsActionPressed:  the action started this frame (a bound input went down and none was already held)
	//   - IsActionReleased: the action ended this frame (a bound input went up and none is still held)
	// Unknown actions are never down.
	class InputMap
	{
	public:
		static void SetActions(InputActionMap actions);
		static const InputActionMap& GetActions();
		static bool HasAction(const std::string& action);

		static bool IsActionDown(const std::string& action, InputAccess access = InputAccess::Game);
		static bool IsActionPressed(const std::string& action, InputAccess access = InputAccess::Game);
		static bool IsActionReleased(const std::string& action, InputAccess access = InputAccess::Game);

		// The actions new projects start with: movement, jump, interact, pause and the UI's.
		static InputActionMap GetDefaultActions();
		// The actions the game UI navigates with (ui_accept, ui_cancel, ui_up, ...), which every project
		// has: projects saved before they existed get them when loaded.
		static InputActionMap GetDefaultUIActions();
		static void AddMissingUIActions(InputActionMap& actions);
	};

}
