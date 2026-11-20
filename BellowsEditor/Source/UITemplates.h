#pragma once

#include "Bellows/Scene/Entity.h"
#include "Bellows/UI/UILayout.h"

#include <optional>
#include <string>
#include <string_view>

namespace Bellows {

	// What Create > UI makes: single widgets, and whole screens built from them. Shared by the
	// Hierarchy's menu and bellows.editor.create_ui(), so both make exactly the same thing.
	enum class UITemplate : uint8_t
	{
		Canvas = 0, Panel, Text, Button, Toggle, Slider, Dropdown, TextInput, VerticalLayout, ScrollList,
		MainMenu, HUD
	};
	constexpr int UITemplateCount = 12;

	// "Main Menu", for menus and undo steps.
	const char* UITemplateToString(UITemplate kind);
	// The name scripts use: "main_menu", "text_input". Case and spaces don't matter ("Main Menu" works).
	std::optional<UITemplate> UITemplateFromName(std::string_view name);
	std::string UITemplateScriptName(UITemplate kind);
	// Whole screens get a canvas of their own; widgets go inside the canvas 'parent' is in.
	bool IsUIScreenTemplate(UITemplate kind);

	// Makes 'kind' under 'parent' (null: the scene's root) and returns its top entity. A widget goes into
	// the canvas 'parent' is in, or into a new canvas under 'parent' when it isn't in one; a canvas and
	// the screens make their own. The caller selects it and commits the undo step.
	Entity CreateUITemplate(Scene& scene, Entity parent, UITemplate kind);

	// "top_left", "full_rect"; case and spaces don't matter ("Top Left" works).
	std::optional<UIAnchorPreset> UIAnchorPresetFromName(std::string_view name);
	std::string UIAnchorPresetScriptName(UIAnchorPreset preset);
	// Gives a UI rect a preset's anchors. It stays where it was last laid out on screen, or with
	// 'moveRect' goes to the preset: filling stretching axes, and sitting on the point, pivoted there,
	// along the others. A rect that hasn't been laid out yet only gets the anchors. False when the
	// entity has no UI Rect. The caller commits the undo step.
	bool ApplyUIAnchorPreset(Entity entity, UIAnchorPreset preset, bool moveRect);

}
