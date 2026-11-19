#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace Bellows {

	// How a canvas's reference size maps to the screen. Positions and sizes in a UI are in reference
	// pixels; the canvas scale turns them into screen pixels.
	//   FitHeight      - the reference height fills the screen's height (widths follow).
	//   FitWidth       - the reference width fills the screen's width.
	//   Expand         - the whole reference area fits on screen; the extra space on the longer side grows.
	//   ConstantPixels - one reference pixel is one screen pixel, whatever the screen size.
	enum class UIScaleMode : uint8_t { FitHeight = 0, FitWidth, Expand, ConstantPixels };

	const char* UIScaleModeToString(UIScaleMode mode);
	UIScaleMode UIScaleModeFromString(std::string_view text);	// Unknown text: Expand
	bool IsUIScaleModeName(std::string_view text);

	// A rectangle in screen pixels: (0, 0) is the top-left of the screen and y points down.
	struct UIRectangle
	{
		glm::vec2 Min = glm::vec2(0.0f);
		glm::vec2 Max = glm::vec2(0.0f);

		glm::vec2 GetSize() const { return Max - Min; }
		bool Contains(const glm::vec2& point) const { return point.x >= Min.x && point.y >= Min.y && point.x < Max.x && point.y < Max.y; }
		UIRectangle Intersect(const UIRectangle& other) const;
	};

	// Screen pixels per reference pixel. A zero-sized reference or screen gives 1.
	float ComputeUIScale(const glm::vec2& referenceSize, UIScaleMode mode, const glm::vec2& screenSize);

	// Anchors are fractions of the parent rectangle ((0, 0) its top-left, (1, 1) its bottom-right); the
	// offsets, in reference pixels, move the anchored corners. Like Godot's anchors and offsets: equal
	// anchors pin a fixed-size rectangle to a point of the parent, different anchors stretch with it.
	// A rectangle whose offsets cross is given a size of 0 rather than turning inside out.
	UIRectangle ResolveUIRect(const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& offsetMin,
		const glm::vec2& offsetMax, const UIRectangle& parent, float scale);

	// The pivot is a point of the rectangle ((0, 0) top-left, (1, 1) bottom-right) that stays put when
	// its size changes. "Position" is where the pivot sits relative to the anchors and "size" is the
	// offsets' span (the whole size when the anchors are equal; the size beyond the stretched part
	// otherwise), both in reference pixels.
	glm::vec2 GetUIRectPosition(const glm::vec2& pivot, const glm::vec2& offsetMin, const glm::vec2& offsetMax);
	glm::vec2 GetUIRectSize(const glm::vec2& offsetMin, const glm::vec2& offsetMax);
	void SetUIRectPosition(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& position);
	void SetUIRectSize(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& size);

}
