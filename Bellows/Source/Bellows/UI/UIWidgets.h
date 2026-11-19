#pragma once

#include "Bellows/UI/UILayout.h"

#include <cstddef>

namespace Bellows {

	// Where widgets draw their parts, shared by drawing and input so a press lands where the part shows.
	// Rectangles are in screen pixels; 'scale' is screen pixels per reference pixel.

	struct UISliderGeometry
	{
		UIRectangle Track;			// The thin bar, inset so the knob stays inside the rect
		glm::vec2 Knob { 0.0f };	// The knob's center
		float KnobRadius = 0.0f;
	};

	// A value kept between min and max (either order) and snapped to steps from min (step 0: no snap).
	float SnapUISliderValue(float value, float min, float max, float step);
	UISliderGeometry ComputeUISliderGeometry(const UIRectangle& rect, float value, float min, float max, float scale);
	// The value under a pointer at screen x, snapped.
	float UISliderValueAt(const UIRectangle& rect, float x, float min, float max, float step, float scale);
	// How far one ui_left / ui_right press moves a slider: a step, or a twentieth of the range.
	float UISliderKeyStep(float min, float max, float step);

	// A toggle's check box, square, at the left of its rect.
	UIRectangle ComputeUIToggleBox(const UIRectangle& rect, float scale);
	// The part of a widget's rect its label (Text) uses: beside a toggle's box, before a dropdown's arrow.
	UIRectangle ComputeUIToggleLabelArea(const UIRectangle& rect, float scale);
	UIRectangle ComputeUIDropdownLabelArea(const UIRectangle& rect, float scale);
	// The open list of a dropdown: one row per option, as tall as the dropdown, under it, or above it
	// when it doesn't fit under it but does above.
	UIRectangle ComputeUIDropdownList(const UIRectangle& rect, size_t optionCount, const glm::vec2& screenSize);
	// The option row at a point in the open list, or -1.
	int UIDropdownRowAt(const UIRectangle& list, size_t optionCount, const glm::vec2& point);

}
