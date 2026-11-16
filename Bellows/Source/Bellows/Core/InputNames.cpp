#include "bwpch.h"
#include "Bellows/Core/InputNames.h"

namespace Bellows {

	namespace {

		// Lower case without spaces or underscores, so "Left Shift", "left_shift" and "LeftShift" match.
		std::string NormalizeName(const std::string& name)
		{
			std::string normalized;
			for (char c : name)
			{
				if (c != '_' && c != ' ')
					normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return normalized;
		}

		template<typename Entry>
		const Entry* FindByName(const std::vector<Entry>& entries, const std::string& name)
		{
			const std::string normalized = NormalizeName(name);
			for (const Entry& entry : entries)
			{
				if (NormalizeName(entry.Name) == normalized)
					return &entry;
			}
			return nullptr;
		}

	}

	const std::vector<KeyName>& GetKeyCodeNames()
	{
		static const std::vector<KeyName> s_Keys = {
			{ "Space", KeyCode::Space }, { "Apostrophe", KeyCode::Apostrophe }, { "Comma", KeyCode::Comma },
			{ "Minus", KeyCode::Minus }, { "Period", KeyCode::Period }, { "Slash", KeyCode::Slash },
			{ "D0", KeyCode::D0 }, { "D1", KeyCode::D1 }, { "D2", KeyCode::D2 }, { "D3", KeyCode::D3 }, { "D4", KeyCode::D4 },
			{ "D5", KeyCode::D5 }, { "D6", KeyCode::D6 }, { "D7", KeyCode::D7 }, { "D8", KeyCode::D8 }, { "D9", KeyCode::D9 },
			{ "Semicolon", KeyCode::Semicolon }, { "Equal", KeyCode::Equal },
			{ "A", KeyCode::A }, { "B", KeyCode::B }, { "C", KeyCode::C }, { "D", KeyCode::D }, { "E", KeyCode::E },
			{ "F", KeyCode::F }, { "G", KeyCode::G }, { "H", KeyCode::H }, { "I", KeyCode::I }, { "J", KeyCode::J },
			{ "K", KeyCode::K }, { "L", KeyCode::L }, { "M", KeyCode::M }, { "N", KeyCode::N }, { "O", KeyCode::O },
			{ "P", KeyCode::P }, { "Q", KeyCode::Q }, { "R", KeyCode::R }, { "S", KeyCode::S }, { "T", KeyCode::T },
			{ "U", KeyCode::U }, { "V", KeyCode::V }, { "W", KeyCode::W }, { "X", KeyCode::X }, { "Y", KeyCode::Y },
			{ "Z", KeyCode::Z },
			{ "LeftBracket", KeyCode::LeftBracket }, { "Backslash", KeyCode::Backslash }, { "RightBracket", KeyCode::RightBracket },
			{ "GraveAccent", KeyCode::GraveAccent },
			{ "Escape", KeyCode::Escape }, { "Enter", KeyCode::Enter }, { "Tab", KeyCode::Tab }, { "Backspace", KeyCode::Backspace },
			{ "Insert", KeyCode::Insert }, { "Delete", KeyCode::Delete },
			{ "Right", KeyCode::Right }, { "Left", KeyCode::Left }, { "Down", KeyCode::Down }, { "Up", KeyCode::Up },
			{ "PageUp", KeyCode::PageUp }, { "PageDown", KeyCode::PageDown }, { "Home", KeyCode::Home }, { "End", KeyCode::End },
			{ "CapsLock", KeyCode::CapsLock }, { "Pause", KeyCode::Pause },
			{ "F1", KeyCode::F1 }, { "F2", KeyCode::F2 }, { "F3", KeyCode::F3 }, { "F4", KeyCode::F4 }, { "F5", KeyCode::F5 },
			{ "F6", KeyCode::F6 }, { "F7", KeyCode::F7 }, { "F8", KeyCode::F8 }, { "F9", KeyCode::F9 }, { "F10", KeyCode::F10 },
			{ "F11", KeyCode::F11 }, { "F12", KeyCode::F12 },
			{ "KP0", KeyCode::KP0 }, { "KP1", KeyCode::KP1 }, { "KP2", KeyCode::KP2 }, { "KP3", KeyCode::KP3 }, { "KP4", KeyCode::KP4 },
			{ "KP5", KeyCode::KP5 }, { "KP6", KeyCode::KP6 }, { "KP7", KeyCode::KP7 }, { "KP8", KeyCode::KP8 }, { "KP9", KeyCode::KP9 },
			{ "KPEnter", KeyCode::KPEnter }, { "KPAdd", KeyCode::KPAdd }, { "KPSubtract", KeyCode::KPSubtract },
			{ "LeftShift", KeyCode::LeftShift }, { "LeftControl", KeyCode::LeftControl }, { "LeftAlt", KeyCode::LeftAlt },
			{ "RightShift", KeyCode::RightShift }, { "RightControl", KeyCode::RightControl }, { "RightAlt", KeyCode::RightAlt },
		};
		return s_Keys;
	}

	const std::vector<MouseButtonName>& GetMouseButtonNames()
	{
		static const std::vector<MouseButtonName> s_Buttons = {
			{ "Left", MouseButton::Left }, { "Right", MouseButton::Right }, { "Middle", MouseButton::Middle },
			{ "Back", MouseButton::Button3 }, { "Forward", MouseButton::Button4 },
		};
		return s_Buttons;
	}

	const std::vector<GamepadButtonName>& GetGamepadButtonNames()
	{
		static const std::vector<GamepadButtonName> s_Buttons = {
			{ "A", GamepadButton::A }, { "B", GamepadButton::B }, { "X", GamepadButton::X }, { "Y", GamepadButton::Y },
			{ "LeftBumper", GamepadButton::LeftBumper }, { "RightBumper", GamepadButton::RightBumper },
			{ "Back", GamepadButton::Back }, { "Start", GamepadButton::Start }, { "Guide", GamepadButton::Guide },
			{ "LeftThumb", GamepadButton::LeftThumb }, { "RightThumb", GamepadButton::RightThumb },
			{ "DPadUp", GamepadButton::DPadUp }, { "DPadRight", GamepadButton::DPadRight },
			{ "DPadDown", GamepadButton::DPadDown }, { "DPadLeft", GamepadButton::DPadLeft },
		};
		return s_Buttons;
	}

	const char* GetKeyCodeName(KeyCode key)
	{
		for (const KeyName& entry : GetKeyCodeNames())
		{
			if (entry.Code == key)
				return entry.Name;
		}
		return nullptr;
	}

	const char* GetMouseButtonName(MouseButton button)
	{
		for (const MouseButtonName& entry : GetMouseButtonNames())
		{
			if (entry.Button == button)
				return entry.Name;
		}
		return nullptr;
	}

	const char* GetGamepadButtonName(GamepadButton button)
	{
		for (const GamepadButtonName& entry : GetGamepadButtonNames())
		{
			if (entry.Button == button)
				return entry.Name;
		}
		return nullptr;
	}

	std::optional<KeyCode> FindKeyCode(const std::string& name)
	{
		if (const KeyName* entry = FindByName(GetKeyCodeNames(), name))
			return entry->Code;
		const std::string normalized = NormalizeName(name);
		if (normalized.size() == 1 && std::isdigit(static_cast<unsigned char>(normalized[0])))
			return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (normalized[0] - '0'));
		return std::nullopt;
	}

	std::optional<MouseButton> FindMouseButton(const std::string& name)
	{
		if (const MouseButtonName* entry = FindByName(GetMouseButtonNames(), name))
			return entry->Button;
		return std::nullopt;
	}

	std::optional<GamepadButton> FindGamepadButton(const std::string& name)
	{
		if (const GamepadButtonName* entry = FindByName(GetGamepadButtonNames(), name))
			return entry->Button;
		return std::nullopt;
	}

}
