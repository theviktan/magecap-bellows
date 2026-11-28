#pragma once

#include "Bellows/Asset/AssetManager.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <string>

namespace Bellows::UI {

	// Drag-and-drop payload carrying an asset path (UTF-8, null-terminated).
	constexpr const char* AssetPayload = "BELLOWS_ASSET";
	// Drag-and-drop payload carrying an entity UUID (uint64_t).
	constexpr const char* EntityPayload = "BELLOWS_ENTITY";

	// Two-column property layout used by the inspector and settings panels.
	bool BeginPropertyGrid(const char* id, float labelWidthRatio = 0.38f);
	void EndPropertyGrid();

	bool Property(const char* label, float& value, float speed = 0.05f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f", const char* tooltip = nullptr);
	bool Property(const char* label, int& value, int min = 0, int max = 0, const char* tooltip = nullptr);
	bool Property(const char* label, uint32_t& value, uint32_t min, uint32_t max, const char* tooltip = nullptr);
	bool Property(const char* label, bool& value, const char* tooltip = nullptr);
	bool Property(const char* label, std::string& value, const char* tooltip = nullptr);
	// A few lines of text (Enter starts a new line).
	bool PropertyMultiline(const char* label, std::string& value, int lines = 3, const char* tooltip = nullptr);
	bool PropertySlider(const char* label, float& value, float min, float max, const char* format = "%.2f", const char* tooltip = nullptr);
	// Radians stored, degrees shown.
	bool PropertyAngle(const char* label, float& radians, float minDegrees = 0.0f, float maxDegrees = 0.0f, const char* tooltip = nullptr);
	bool PropertyVec2(const char* label, glm::vec2& value, float speed = 0.05f, const char* tooltip = nullptr);
	bool PropertyVec3(const char* label, glm::vec3& value, float resetValue = 0.0f, float speed = 0.05f, const char* tooltip = nullptr);
	bool PropertyVec4(const char* label, glm::vec4& value, float speed = 0.05f, const char* tooltip = nullptr);
	bool PropertyColor(const char* label, glm::vec3& color, bool hdr = false, const char* tooltip = nullptr);
	bool PropertyColor(const char* label, glm::vec4& color, bool hdr = false, const char* tooltip = nullptr);
	bool PropertyCombo(const char* label, int& selected, const char* const* items, int itemCount, const char* tooltip = nullptr);
	// An asset path field: accepts drops from the content browser and offers a searchable list of matching assets.
	bool PropertyAsset(const char* label, std::string& path, AssetType type, const char* tooltip = nullptr, bool allowBuiltins = false);
	void PropertyText(const char* label, const char* value);
	// A UI rect's anchors as a button showing the matching preset; it opens a 4 x 4 grid of presets.
	// Returns true with 'outPreset' (a UIAnchorPreset index) when one is picked.
	bool PropertyAnchorPreset(const char* label, const glm::vec2& anchorMin, const glm::vec2& anchorMax, int& outPreset, const char* tooltip = nullptr);

	// Collapsible component section. Returns true when expanded; 'outRemove' is set when "Remove" was chosen.
	bool ComponentHeader(const char* icon, const char* name, bool canRemove, bool& outRemove, bool& outReset);

	bool IconButton(const char* icon, const char* tooltip = nullptr, bool active = false, ImVec2 size = ImVec2(0.0f, 0.0f));
	// Colors for the one primary action of a dialog or screen; pop with PopPrimaryButtonColors.
	void PushPrimaryButtonColors();
	void PopPrimaryButtonColors();
	void Tooltip(const char* text);
	void TextMuted(const char* text);
	void SectionHeader(const char* text);
	void HelpMarker(const char* text);

	// Begins a dockable panel the user can close from its tab (View > Panels shows it again).
	// The dock area's own close button is hidden, so one click never closes several panels at once.
	bool BeginPanel(const char* name, bool* open, ImGuiWindowFlags flags = 0);
	// Call right before ImGui::Begin of a dockable window that doesn't use BeginPanel: docked, its
	// background is the editor's rounded card.
	void PrepareDockedBackground(const char* name);

	// Centered text inside the current window (empty states).
	void CenteredText(const char* text, float verticalOffset = 0.0f);

	// Icon glyph that represents an asset type.
	const char* AssetIcon(AssetType type);
	ImU32 AssetColor(AssetType type);

	// Opens the native file or folder dialog. Returns an empty path when cancelled.
	std::string OpenFileDialog(const char* title, const std::vector<std::string>& filters, const std::string& defaultPath = {});
	std::string SaveFileDialog(const char* title, const std::vector<std::string>& filters, const std::string& defaultPath = {});
	std::string SelectFolderDialog(const char* title, const std::string& defaultPath = {});

}
