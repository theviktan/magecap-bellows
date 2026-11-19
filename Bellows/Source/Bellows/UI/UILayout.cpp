#include "Bellows/UI/UILayout.h"

#include <algorithm>
#include <cmath>

namespace Bellows {

	const char* UIScaleModeToString(UIScaleMode mode)
	{
		switch (mode)
		{
			case UIScaleMode::FitHeight:		return "fit_height";
			case UIScaleMode::FitWidth:			return "fit_width";
			case UIScaleMode::Expand:			return "expand";
			case UIScaleMode::ConstantPixels:	return "constant_pixels";
		}
		return "expand";
	}

	UIScaleMode UIScaleModeFromString(std::string_view text)
	{
		if (text == "fit_height")
			return UIScaleMode::FitHeight;
		if (text == "fit_width")
			return UIScaleMode::FitWidth;
		if (text == "constant_pixels")
			return UIScaleMode::ConstantPixels;
		return UIScaleMode::Expand;
	}

	bool IsUIScaleModeName(std::string_view text)
	{
		return text == "fit_height" || text == "fit_width" || text == "expand" || text == "constant_pixels";
	}

	UIRectangle UIRectangle::Intersect(const UIRectangle& other) const
	{
		UIRectangle result;
		result.Min = glm::max(Min, other.Min);
		result.Max = glm::max(glm::min(Max, other.Max), result.Min);
		return result;
	}

	float ComputeUIScale(const glm::vec2& referenceSize, UIScaleMode mode, const glm::vec2& screenSize)
	{
		if (mode == UIScaleMode::ConstantPixels || referenceSize.x <= 0.0f || referenceSize.y <= 0.0f
			|| screenSize.x <= 0.0f || screenSize.y <= 0.0f)
			return 1.0f;
		const glm::vec2 ratio = screenSize / referenceSize;
		switch (mode)
		{
			case UIScaleMode::FitHeight:	return ratio.y;
			case UIScaleMode::FitWidth:		return ratio.x;
			default:						return std::min(ratio.x, ratio.y);
		}
	}

	UIRectangle ResolveUIRect(const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& offsetMin,
		const glm::vec2& offsetMax, const UIRectangle& parent, float scale)
	{
		const glm::vec2 parentSize = parent.GetSize();
		UIRectangle result;
		result.Min = parent.Min + anchorMin * parentSize + offsetMin * scale;
		result.Max = parent.Min + anchorMax * parentSize + offsetMax * scale;
		result.Max = glm::max(result.Max, result.Min);
		return result;
	}

	int FindUINavigationTarget(const UIRectangle& from, const std::vector<UIRectangle>& candidates, UINavigationDirection direction, int skip)
	{
		// Screen y points down, so "up" is -y.
		static constexpr glm::vec2 c_Directions[] = { { 0.0f, -1.0f }, { 0.0f, 1.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f } };
		const glm::vec2 forward = c_Directions[static_cast<int>(direction)];
		const glm::vec2 origin = (from.Min + from.Max) * 0.5f;

		int best = -1;
		float bestScore = 0.0f;
		for (int i = 0; i < static_cast<int>(candidates.size()); i++)
		{
			if (i == skip)
				continue;
			const glm::vec2 offset = (candidates[i].Min + candidates[i].Max) * 0.5f - origin;
			const float ahead = glm::dot(offset, forward);
			if (ahead <= 0.5f)
				continue;
			// Something one step to the side counts as far as two steps ahead.
			const float aside = std::abs(offset.x * forward.y - offset.y * forward.x);
			const float score = ahead + 2.0f * aside;
			if (best < 0 || score < bestScore)
			{
				best = i;
				bestScore = score;
			}
		}
		return best;
	}

	glm::vec2 GetUIRectPosition(const glm::vec2& pivot, const glm::vec2& offsetMin, const glm::vec2& offsetMax)
	{
		return offsetMin + pivot * (offsetMax - offsetMin);
	}

	glm::vec2 GetUIRectSize(const glm::vec2& offsetMin, const glm::vec2& offsetMax)
	{
		return offsetMax - offsetMin;
	}

	void SetUIRectPosition(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& position)
	{
		const glm::vec2 delta = position - GetUIRectPosition(pivot, offsetMin, offsetMax);
		offsetMin += delta;
		offsetMax += delta;
	}

	void SetUIRectSize(const glm::vec2& pivot, glm::vec2& offsetMin, glm::vec2& offsetMax, const glm::vec2& size)
	{
		const glm::vec2 delta = size - GetUIRectSize(offsetMin, offsetMax);
		offsetMin -= delta * pivot;
		offsetMax += delta * (glm::vec2(1.0f) - pivot);
	}

}
