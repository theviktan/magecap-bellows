#include "Bellows/UI/UIWidgets.h"

#include <algorithm>
#include <cmath>

namespace Bellows {

	float SnapUISliderValue(float value, float min, float max, float step)
	{
		const float low = std::min(min, max), high = std::max(min, max);
		if (!std::isfinite(value))
			value = low;
		value = std::clamp(value, low, high);
		if (step > 0.0f)
			value = std::clamp(low + std::round((value - low) / step) * step, low, high);
		return value;
	}

	UISliderGeometry ComputeUISliderGeometry(const UIRectangle& rect, float value, float min, float max, float scale)
	{
		UISliderGeometry geometry;
		const glm::vec2 size = rect.GetSize();
		geometry.KnobRadius = std::min(10.0f * scale, size.y * 0.5f);
		const float centerY = (rect.Min.y + rect.Max.y) * 0.5f;
		const float halfTrack = std::min(3.0f * scale, geometry.KnobRadius);
		const float left = rect.Min.x + geometry.KnobRadius, right = std::max(rect.Max.x - geometry.KnobRadius, left);
		geometry.Track = { glm::vec2(left, centerY - halfTrack), glm::vec2(right, centerY + halfTrack) };
		const float range = max - min;
		const float fraction = range != 0.0f ? std::clamp((value - min) / range, 0.0f, 1.0f) : 0.0f;
		geometry.Knob = glm::vec2(left + (right - left) * fraction, centerY);
		return geometry;
	}

	float UISliderValueAt(const UIRectangle& rect, float x, float min, float max, float step, float scale)
	{
		const UISliderGeometry geometry = ComputeUISliderGeometry(rect, min, min, max, scale);
		const float width = geometry.Track.Max.x - geometry.Track.Min.x;
		const float fraction = width > 0.0f ? std::clamp((x - geometry.Track.Min.x) / width, 0.0f, 1.0f) : 0.0f;
		return SnapUISliderValue(min + (max - min) * fraction, min, max, step);
	}

	float UISliderKeyStep(float min, float max, float step)
	{
		return step > 0.0f ? step : std::abs(max - min) / 20.0f;
	}

	UIRectangle ComputeUIToggleBox(const UIRectangle& rect, float scale)
	{
		const float side = std::min(rect.GetSize().y * 0.7f, 36.0f * scale);
		const float centerY = (rect.Min.y + rect.Max.y) * 0.5f;
		return { glm::vec2(rect.Min.x, centerY - side * 0.5f), glm::vec2(rect.Min.x + side, centerY + side * 0.5f) };
	}

	UIRectangle ComputeUIToggleLabelArea(const UIRectangle& rect, float scale)
	{
		const UIRectangle box = ComputeUIToggleBox(rect, scale);
		return { glm::vec2(std::min(box.Max.x + 12.0f * scale, rect.Max.x), rect.Min.y), rect.Max };
	}

	UIRectangle ComputeUIDropdownLabelArea(const UIRectangle& rect, float scale)
	{
		const float arrowSpace = std::min(rect.GetSize().y, 48.0f * scale);
		return { glm::vec2(rect.Min.x + 12.0f * scale, rect.Min.y), glm::vec2(std::max(rect.Max.x - arrowSpace, rect.Min.x), rect.Max.y) };
	}

	UIRectangle ComputeUITextInputArea(const UIRectangle& rect, float scale)
	{
		const glm::vec2 margin = glm::min(glm::vec2(12.0f, 6.0f) * scale, rect.GetSize() * 0.25f);
		return { rect.Min + margin, glm::max(rect.Max - margin, rect.Min + margin) };
	}

	UIRectangle ComputeUIDropdownList(const UIRectangle& rect, size_t optionCount, const glm::vec2& screenSize)
	{
		const float rowHeight = rect.GetSize().y;
		const float height = rowHeight * static_cast<float>(optionCount);
		const bool fitsBelow = rect.Max.y + height <= screenSize.y;
		const bool fitsAbove = rect.Min.y - height >= 0.0f;
		if (!fitsBelow && fitsAbove)
			return { glm::vec2(rect.Min.x, rect.Min.y - height), glm::vec2(rect.Max.x, rect.Min.y) };
		return { glm::vec2(rect.Min.x, rect.Max.y), glm::vec2(rect.Max.x, rect.Max.y + height) };
	}

	int UIDropdownRowAt(const UIRectangle& list, size_t optionCount, const glm::vec2& point)
	{
		if (optionCount == 0 || !list.Contains(point))
			return -1;
		const float rowHeight = list.GetSize().y / static_cast<float>(optionCount);
		if (rowHeight <= 0.0f)
			return -1;
		return std::clamp(static_cast<int>((point.y - list.Min.y) / rowHeight), 0, static_cast<int>(optionCount) - 1);
	}

}
