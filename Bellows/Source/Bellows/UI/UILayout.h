#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

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
	// --- Layout containers -------------------------------------------------------------------------

	// How a layout container places its children: one under another, side by side, or in a grid that
	// fills each row left to right.
	enum class UILayoutDirection : uint8_t { Vertical = 0, Horizontal, Grid };
	// Where children sit along an axis when they don't fill it.
	enum class UIAlignment : uint8_t { Start = 0, Center, End };
	// A child's size along one axis in a layout: its own size, or a share of the space left over.
	enum class UISizing : uint8_t { Fixed = 0, Fill };

	const char* UILayoutDirectionToString(UILayoutDirection direction);
	UILayoutDirection UILayoutDirectionFromString(std::string_view text);	// Unknown text: Vertical
	bool IsUILayoutDirectionName(std::string_view text);
	const char* UIAlignmentToString(UIAlignment alignment);
	UIAlignment UIAlignmentFromString(std::string_view text);				// Unknown text: Start
	bool IsUIAlignmentName(std::string_view text);
	const char* UISizingToString(UISizing sizing);
	UISizing UISizingFromString(std::string_view text);						// Unknown text: Fixed
	bool IsUISizingName(std::string_view text);

	struct UILayoutSettings
	{
		UILayoutDirection Direction = UILayoutDirection::Vertical;
		float Spacing = 0.0f;						// Between children, reference pixels
		glm::vec4 Padding { 0.0f };					// Left, top, right, bottom, reference pixels
		UIAlignment MainAlignment = UIAlignment::Start;		// Along the direction (rows of a grid: across)
		UIAlignment CrossAlignment = UIAlignment::Start;	// Across it, for children that don't fill
		int Columns = 2;							// Grid only
	};

	struct UILayoutItem
	{
		glm::vec2 Size { 0.0f };					// Its own size in reference pixels (for Fixed axes)
		UISizing Width = UISizing::Fixed;
		UISizing Height = UISizing::Fixed;
	};

	// The screen rectangle of each item, in order, inside 'area' (screen pixels; 'scale' screen pixels
	// per reference pixel). Vertical and horizontal layouts share what the fixed children leave among
	// the filling ones, equally; a grid gives every column the same width and every row the height of
	// its tallest child. 'contentSize' receives the size everything takes, padding included, in screen
	// pixels (for scrolling).
	std::vector<UIRectangle> ArrangeUILayout(const UILayoutSettings& settings, const UIRectangle& area,
		const std::vector<UILayoutItem>& items, float scale, glm::vec2* contentSize = nullptr);

	enum class UINavigationDirection : uint8_t { Up = 0, Down, Left, Right };

	// The rectangle keyboard or gamepad navigation moves to from 'from' in 'direction': the nearest one
	// whose center lies that way, favoring those straight ahead over those off to the side. Returns its
	// index in 'candidates', or -1 when nothing lies that way. 'skip' (the current one) is never chosen.
	int FindUINavigationTarget(const UIRectangle& from, const std::vector<UIRectangle>& candidates, UINavigationDirection direction, int skip = -1);

	glm::vec2 GetUIRectPosition(const glm::vec2& pivot, const glm::vec2& offsetMin, const glm::vec2& offsetMax);
	glm::vec2 GetUIRectSize(const glm::vec2& offsetMin, const glm::vec2& offsetMax);
	void SetUIRectPosition(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& position);
	void SetUIRectSize(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& size);

	// Editing a rect on screen (the editor's handles, anchor presets).

	// The offsets that put a rect at 'screenRect' with these anchors: the inverse of ResolveUIRect.
	void ComputeUIOffsetsFor(const UIRectangle& screenRect, const glm::vec2& anchorMin, const glm::vec2& anchorMax,
		const UIRectangle& parent, float scale, glm::vec2& outOffsetMin, glm::vec2& outOffsetMax);

	// Godot's anchor presets: a corner, the middle of a side or the center, or a stretch across the parent.
	enum class UIAnchorPreset : uint8_t
	{
		TopLeft = 0, TopCenter, TopRight, CenterLeft, Center, CenterRight, BottomLeft, BottomCenter, BottomRight,
		TopWide, MiddleWide, BottomWide, LeftTall, CenterTall, RightTall, Full
	};
	constexpr int UIAnchorPresetCount = 16;
	void GetUIAnchorPreset(UIAnchorPreset preset, glm::vec2& outAnchorMin, glm::vec2& outAnchorMax);
	const char* UIAnchorPresetToString(UIAnchorPreset preset);	// "Top Left", ..., "Full Rect"
	// Where a rect goes when snapped to a preset in 'parent': along a stretching axis it fills the anchors;
	// along a point axis it keeps its size and sits on the point, pivoted there ('outPivot', per axis;
	// stretching axes keep 'pivot').
	UIRectangle GetUIAnchorPresetRect(UIAnchorPreset preset, const UIRectangle& current, const UIRectangle& parent,
		const glm::vec2& pivot, glm::vec2& outPivot);

	// The part of a rect a drag holds: the whole rect, an edge or a corner.
	enum class UIRectHandle : uint8_t { None = 0, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
	// The rect after dragging 'handle' by 'delta' screen pixels. A resize stops at 'minSize' (the edges
	// never cross); a move keeps the size.
	UIRectangle DragUIRect(const UIRectangle& start, UIRectHandle handle, const glm::vec2& delta, float minSize);
	// Which handle of 'rect' is at 'point': corners and edges within 'reach' pixels, else Move inside it.
	UIRectHandle FindUIRectHandle(const UIRectangle& rect, const glm::vec2& point, float reach);
	// Rounds the edges that a drag moved to multiples of 'step' offset pixels (Ctrl while dragging).
	void SnapUIOffsets(UIRectHandle handle, glm::vec2& offsetMin, glm::vec2& offsetMax, float step);

}
