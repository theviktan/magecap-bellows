#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace Bellows {

	enum class UIWidgetState : uint8_t { Normal = 0, Hover, Pressed, Focused, Disabled };
	constexpr size_t UIWidgetStateCount = 5;

	// How a widget (or one of its parts) looks in one state. Text is the color of its label.
	struct UIStateStyle
	{
		glm::vec4 Background { 0.0f };
		glm::vec4 Border { 0.0f };
		float BorderWidth = 0.0f;
		glm::vec4 Text { 1.0f };
	};

	struct UIWidgetStyle
	{
		float CornerRadius = 0.0f;
		UIStateStyle States[UIWidgetStateCount];

		const UIStateStyle& Get(UIWidgetState state) const { return States[static_cast<size_t>(state)]; }
		UIStateStyle& Get(UIWidgetState state) { return States[static_cast<size_t>(state)]; }
	};

	// The widget types a theme styles. A panel is a plain UIImage; a toggle's style is its check box
	// (the check is the accent); a slider's is its track (Background) and knob (Text, its outline the
	// Border), its fill the accent; a dropdown list's Normal is the open list and Hover its highlighted row.
	enum class UIThemeWidget : uint8_t { Panel = 0, Button, Toggle, Slider, Dropdown, DropdownList, TextInput };
	constexpr size_t UIThemeWidgetCount = 7;

	// A UI theme: the look every widget on a canvas gets unless it sets its own. Stored as a .btheme
	// asset (JSON) that lists only what it changes from the default; "Builtin/MageCap.btheme" is the
	// default itself, built into the engine.
	struct UITheme
	{
		std::string Font;						// Font asset path; empty for the built-in Inter
		float TextSize = 28.0f;					// Reference pixels
		glm::vec4 TextColor { 1.0f };
		glm::vec4 Accent { 1.0f };				// Focus ring, toggle check, slider fill, text selection
		glm::vec4 AccentLight { 1.0f };			// A slider knob while dragged, the selected dropdown option
		float FocusRingWidth = 2.0f;
		float FocusRingGap = 3.0f;
		UIWidgetStyle Widgets[UIThemeWidgetCount];
		// Named variants ("primary", "danger") that a UIRect picks with its style class: complete widget
		// styles, starting from the theme's own and changed by the file.
		std::map<std::string, std::map<UIThemeWidget, UIWidgetStyle>> Classes;

		const UIWidgetStyle& Get(UIThemeWidget widget) const { return Widgets[static_cast<size_t>(widget)]; }
		UIWidgetStyle& Get(UIThemeWidget widget) { return Widgets[static_cast<size_t>(widget)]; }
		// The widget's style for a style class, or the theme's own when the class is empty or unknown.
		const UIWidgetStyle& Get(UIThemeWidget widget, const std::string& styleClass) const;
	};

	constexpr const char* c_DefaultUIThemePath = "Builtin/MageCap.btheme";

	// The MageCap look: navy, slate, off-white and an orange accent, matching the icon family.
	const UITheme& GetDefaultUITheme();

	const char* UIWidgetStateToString(UIWidgetState state);
	const char* UIThemeWidgetToString(UIThemeWidget widget);

	// Reads a theme from JSON text. Values it doesn't list (and values it gets wrong, which are reported
	// in 'warnings' when given) keep the default theme's. Returns nullopt when the text isn't a JSON object.
	std::optional<UITheme> ParseUITheme(const std::string& text, std::string* outError = nullptr, std::string* outWarnings = nullptr);
	std::optional<UITheme> LoadUITheme(const std::filesystem::path& path, std::string* outError = nullptr);
	// Writes every value, so a saved copy is a complete starting point for a game's own theme.
	std::string WriteUITheme(const UITheme& theme);

	// Colors in theme files: "#RRGGBB" or "#RRGGBBAA" (sRGB), or [r, g, b, a] from 0 to 1.
	std::optional<glm::vec4> ParseUIColor(const std::string& text);
	std::string UIColorToString(const glm::vec4& color);

}
