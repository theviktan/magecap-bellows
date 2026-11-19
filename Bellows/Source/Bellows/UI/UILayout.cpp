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

	const char* UILayoutDirectionToString(UILayoutDirection direction)
	{
		switch (direction)
		{
			case UILayoutDirection::Vertical:	return "vertical";
			case UILayoutDirection::Horizontal:	return "horizontal";
			case UILayoutDirection::Grid:		return "grid";
		}
		return "vertical";
	}

	UILayoutDirection UILayoutDirectionFromString(std::string_view text)
	{
		if (text == "horizontal")
			return UILayoutDirection::Horizontal;
		if (text == "grid")
			return UILayoutDirection::Grid;
		return UILayoutDirection::Vertical;
	}

	bool IsUILayoutDirectionName(std::string_view text)
	{
		return text == "vertical" || text == "horizontal" || text == "grid";
	}

	const char* UIAlignmentToString(UIAlignment alignment)
	{
		switch (alignment)
		{
			case UIAlignment::Start:	return "start";
			case UIAlignment::Center:	return "center";
			case UIAlignment::End:		return "end";
		}
		return "start";
	}

	UIAlignment UIAlignmentFromString(std::string_view text)
	{
		if (text == "center")
			return UIAlignment::Center;
		if (text == "end")
			return UIAlignment::End;
		return UIAlignment::Start;
	}

	bool IsUIAlignmentName(std::string_view text)
	{
		return text == "start" || text == "center" || text == "end";
	}

	const char* UISizingToString(UISizing sizing)
	{
		return sizing == UISizing::Fill ? "fill" : "fixed";
	}

	UISizing UISizingFromString(std::string_view text)
	{
		return text == "fill" ? UISizing::Fill : UISizing::Fixed;
	}

	bool IsUISizingName(std::string_view text)
	{
		return text == "fixed" || text == "fill";
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

	namespace {

		float AlignmentFactor(UIAlignment alignment)
		{
			return alignment == UIAlignment::Center ? 0.5f : (alignment == UIAlignment::End ? 1.0f : 0.0f);
		}

		// One child's place across the layout: the whole span when it fills, else its size aligned in it.
		void PlaceAcross(float spanStart, float spanSize, float size, UISizing sizing, UIAlignment alignment, float& outStart, float& outSize)
		{
			outSize = sizing == UISizing::Fill ? spanSize : size;
			outStart = spanStart + (spanSize - outSize) * AlignmentFactor(alignment);
		}

	}

	std::vector<UIRectangle> ArrangeUILayout(const UILayoutSettings& settings, const UIRectangle& area,
		const std::vector<UILayoutItem>& items, float scale, glm::vec2* contentSize)
	{
		std::vector<UIRectangle> rects(items.size());
		const glm::vec2 padMin = glm::vec2(settings.Padding.x, settings.Padding.y) * scale;
		const glm::vec2 padMax = glm::vec2(settings.Padding.z, settings.Padding.w) * scale;
		const glm::vec2 innerMin = area.Min + padMin;
		const glm::vec2 innerSize = glm::max(area.GetSize() - padMin - padMax, glm::vec2(0.0f));
		const float spacing = std::max(settings.Spacing, 0.0f) * scale;
		glm::vec2 used(0.0f);

		if (settings.Direction == UILayoutDirection::Grid)
		{
			const size_t columns = static_cast<size_t>(std::max(settings.Columns, 1));
			const float cellWidth = std::max((innerSize.x - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns), 0.0f);
			const size_t rows = (items.size() + columns - 1) / columns;
			std::vector<float> rowHeights(rows, 0.0f);
			for (size_t i = 0; i < items.size(); i++)
				rowHeights[i / columns] = std::max(rowHeights[i / columns], items[i].Size.y * scale);
			float total = 0.0f;
			for (float height : rowHeights)
				total += height;
			total += rows > 0 ? spacing * static_cast<float>(rows - 1) : 0.0f;

			// Rows that don't fill the area's height move together by MainAlignment.
			float y = innerMin.y + std::max(innerSize.y - total, 0.0f) * AlignmentFactor(settings.MainAlignment);
			for (size_t row = 0; row < rows; row++)
			{
				for (size_t column = 0; column < columns && row * columns + column < items.size(); column++)
				{
					const size_t index = row * columns + column;
					const UILayoutItem& item = items[index];
					const float cellX = innerMin.x + static_cast<float>(column) * (cellWidth + spacing);
					float x, width, top, height;
					PlaceAcross(cellX, cellWidth, item.Size.x * scale, item.Width, settings.CrossAlignment, x, width);
					PlaceAcross(y, rowHeights[row], item.Size.y * scale, item.Height, settings.CrossAlignment, top, height);
					rects[index] = { glm::vec2(x, top), glm::vec2(x + width, top + height) };
				}
				y += rowHeights[row] + spacing;
			}
			used = glm::vec2(items.empty() ? 0.0f : innerSize.x, total);
		}
		else
		{
			// Axis 0 is the layout's direction, axis 1 across it.
			const int main = settings.Direction == UILayoutDirection::Horizontal ? 0 : 1;
			const int cross = 1 - main;
			float fixedTotal = 0.0f;
			int fillCount = 0;
			for (const UILayoutItem& item : items)
			{
				const UISizing sizing = main == 0 ? item.Width : item.Height;
				if (sizing == UISizing::Fill)
					fillCount++;
				else
					fixedTotal += item.Size[main] * scale;
			}
			const float gaps = items.empty() ? 0.0f : spacing * static_cast<float>(items.size() - 1);
			const float leftover = std::max(innerSize[main] - fixedTotal - gaps, 0.0f);
			const float fillSize = fillCount > 0 ? leftover / static_cast<float>(fillCount) : 0.0f;
			const float total = fixedTotal + gaps + fillSize * static_cast<float>(fillCount);

			float position = innerMin[main] + (fillCount > 0 ? 0.0f : std::max(innerSize[main] - total, 0.0f) * AlignmentFactor(settings.MainAlignment));
			float widest = 0.0f;
			for (size_t i = 0; i < items.size(); i++)
			{
				const UILayoutItem& item = items[i];
				const UISizing mainSizing = main == 0 ? item.Width : item.Height;
				const UISizing crossSizing = cross == 0 ? item.Width : item.Height;
				const float length = mainSizing == UISizing::Fill ? fillSize : item.Size[main] * scale;
				float crossStart, crossLength;
				PlaceAcross(innerMin[cross], innerSize[cross], item.Size[cross] * scale, crossSizing, settings.CrossAlignment, crossStart, crossLength);
				glm::vec2 min, max;
				min[main] = position;
				max[main] = position + length;
				min[cross] = crossStart;
				max[cross] = crossStart + crossLength;
				rects[i] = { min, max };
				widest = std::max(widest, crossSizing == UISizing::Fill ? 0.0f : crossLength);
				position += length + spacing;
			}
			used[main] = total;
			used[cross] = widest;
		}

		if (contentSize)
			*contentSize = used + padMin + padMax;
		return rects;
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
