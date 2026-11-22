#include "EditorUI.h"

#include "EditorTheme.h"
#include "IconsLucide.h"

#include "Bellows/Core/Log.h"
#include "Bellows/UI/UILayout.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <portable-file-dialogs.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Bellows::UI {

	namespace {

		float s_LabelWidthRatio = 0.38f;

		// The last property row that refused a number that isn't finite, so it can say why for a moment.
		ImGuiID s_RefusedRow = 0;
		double s_RefusedTime = 0.0;
		constexpr double c_RefusedMessageSeconds = 3.0;
		constexpr const char* c_RefusedMessage = "Only finite numbers can be used here (not NaN or infinity), so the old value was kept.";

		ImGuiID CurrentRowID()
		{
			return ImGui::GetID("##row");
		}

		// Typing "nan" or "inf" into a field (ImGui parses both) would put a value in the scene that
		// saves as JSON null and that physics can't use. Puts the old values back when any is not
		// finite and returns true, so the caller reports no change.
		bool RefuseNonFinite(float* values, const float* before, int count, const char* label)
		{
			bool finite = true;
			for (int i = 0; i < count; i++)
				finite &= std::isfinite(values[i]);
			if (finite)
				return false;
			std::copy(before, before + count, values);
			if (s_RefusedRow != CurrentRowID() || ImGui::GetTime() - s_RefusedTime > c_RefusedMessageSeconds)
				BW_WARN("{}: {}", label, c_RefusedMessage);
			s_RefusedRow = CurrentRowID();
			s_RefusedTime = ImGui::GetTime();
			return true;
		}

		template<typename Widget>
		bool FiniteEdit(float* values, int count, const char* label, Widget&& widget)
		{
			float before[4];
			std::copy(values, values + count, before);
			const bool changed = widget();
			return !RefuseNonFinite(values, before, count, label) && changed;
		}

		const char* s_CurrentLabel = "";

		void BeginRow(const char* label, const char* tooltip)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
			ImGui::TextUnformatted(label);
			ImGui::PopStyleColor();
			if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", tooltip);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::PushID(label);
			s_CurrentLabel = label;
		}

		void EndRow()
		{
			if (s_RefusedRow == CurrentRowID() && ImGui::GetTime() - s_RefusedTime <= c_RefusedMessageSeconds)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
				ImGui::TextWrapped("%s", c_RefusedMessage);
				ImGui::PopStyleColor();
			}
			ImGui::PopID();
		}

		// How a vector's fields are placed: side by side when each value fits in its field, otherwise one
		// under the other at full width, so a number is never shown cut off ("0.5(" for 0.500).
		struct VectorLayout
		{
			float Width = 0.0f;
			bool Stacked = false;
		};

		VectorLayout GetVectorLayout(const float* values, int count)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float available = ImGui::GetContentRegionAvail().x;
			const float sideBySide = (available - style.ItemInnerSpacing.x * static_cast<float>(count - 1)) / static_cast<float>(count);
			float widest = ImGui::CalcTextSize("-0.000").x;
			char text[64];
			for (int i = 0; i < count; i++)
			{
				std::snprintf(text, sizeof(text), "%.3f", values[i]);
				widest = std::max(widest, ImGui::CalcTextSize(text).x);
			}
			const float needed = ImGui::GetFrameHeight() * 0.85f + widest + style.FramePadding.x * 2.0f;
			if (sideBySide >= needed)
				return { sideBySide, false };
			return { available, true };
		}

		void NextAxis(const VectorLayout& layout)
		{
			if (!layout.Stacked)
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		}

		// One colored component of a vector field.
		bool AxisDrag(const char* axis, float& value, float resetValue, float speed, ImU32 color, float width)
		{
			bool changed = false;
			const float lineHeight = ImGui::GetFrameHeight();
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			ImGui::PushFont(Theme::GetFonts().Bold, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			if (ImGui::Button(axis, ImVec2(lineHeight * 0.85f, lineHeight)))
			{
				value = resetValue;
				changed = true;
			}
			ImGui::PopStyleVar();
			ImGui::PopFont();
			ImGui::PopStyleColor(3);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Reset to %g", resetValue);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(std::max(width - lineHeight * 0.85f, 10.0f));
			std::string id = std::string("##") + axis;
			changed |= FiniteEdit(&value, 1, s_CurrentLabel, [&] { return ImGui::DragFloat(id.c_str(), &value, speed, 0.0f, 0.0f, "%.3f"); });
			ImGui::PopStyleVar();
			return changed;
		}

	}

	bool BeginPropertyGrid(const char* id, float labelWidthRatio)
	{
		s_LabelWidthRatio = labelWidthRatio;
		const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
		if (!ImGui::BeginTable(id, 2, flags))
			return false;
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, s_LabelWidthRatio);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f - s_LabelWidthRatio);
		return true;
	}

	void EndPropertyGrid()
	{
		ImGui::EndTable();
	}

	bool Property(const char* label, float& value, float speed, float min, float max, const char* format, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = FiniteEdit(&value, 1, label,
			[&] { return ImGui::DragFloat("##value", &value, speed, min, max, format, min < max ? ImGuiSliderFlags_AlwaysClamp : 0); });
		EndRow();
		return changed;
	}

	bool Property(const char* label, int& value, int min, int max, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = ImGui::DragInt("##value", &value, 0.2f, min, max, "%d", min < max ? ImGuiSliderFlags_AlwaysClamp : 0);
		EndRow();
		return changed;
	}

	bool Property(const char* label, uint32_t& value, uint32_t min, uint32_t max, const char* tooltip)
	{
		int intValue = static_cast<int>(value);
		const bool changed = Property(label, intValue, static_cast<int>(min), static_cast<int>(max), tooltip);
		if (changed)
			value = static_cast<uint32_t>(std::clamp(intValue, static_cast<int>(min), static_cast<int>(max)));
		return changed;
	}

	bool Property(const char* label, bool& value, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = ImGui::Checkbox("##value", &value);
		EndRow();
		return changed;
	}

	bool Property(const char* label, std::string& value, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = ImGui::InputText("##value", &value);
		EndRow();
		return changed;
	}

	bool PropertyMultiline(const char* label, std::string& value, int lines, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const float height = ImGui::GetTextLineHeight() * static_cast<float>(std::max(lines, 1)) + ImGui::GetStyle().FramePadding.y * 2.0f;
		const bool changed = ImGui::InputTextMultiline("##value", &value, ImVec2(-1.0f, height));
		EndRow();
		return changed;
	}

	bool PropertySlider(const char* label, float& value, float min, float max, const char* format, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = FiniteEdit(&value, 1, label, [&] { return ImGui::SliderFloat("##value", &value, min, max, format); });
		EndRow();
		return changed;
	}

	bool PropertyAngle(const char* label, float& radians, float minDegrees, float maxDegrees, const char* tooltip)
	{
		float degrees = glm::degrees(radians);
		BeginRow(label, tooltip);
		const bool changed = FiniteEdit(&degrees, 1, label, [&] {
			return ImGui::DragFloat("##value", &degrees, 0.25f, minDegrees, maxDegrees, "%.1f°", minDegrees < maxDegrees ? ImGuiSliderFlags_AlwaysClamp : 0);
		});
		EndRow();
		if (changed)
			radians = glm::radians(degrees);
		return changed;
	}

	bool PropertyVec2(const char* label, glm::vec2& value, float speed, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const VectorLayout layout = GetVectorLayout(glm::value_ptr(value), 2);
		bool changed = AxisDrag("X", value.x, 0.0f, speed, Theme::AxisX, layout.Width);
		NextAxis(layout);
		changed |= AxisDrag("Y", value.y, 0.0f, speed, Theme::AxisY, layout.Width);
		EndRow();
		return changed;
	}

	bool PropertyVec3(const char* label, glm::vec3& value, float resetValue, float speed, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const VectorLayout layout = GetVectorLayout(glm::value_ptr(value), 3);
		bool changed = AxisDrag("X", value.x, resetValue, speed, Theme::AxisX, layout.Width);
		NextAxis(layout);
		changed |= AxisDrag("Y", value.y, resetValue, speed, Theme::AxisY, layout.Width);
		NextAxis(layout);
		changed |= AxisDrag("Z", value.z, resetValue, speed, Theme::AxisZ, layout.Width);
		EndRow();
		return changed;
	}

	bool PropertyVec4(const char* label, glm::vec4& value, float speed, const char* tooltip)
	{
		BeginRow(label, tooltip);
		const bool changed = FiniteEdit(glm::value_ptr(value), 4, label, [&] { return ImGui::DragFloat4("##value", glm::value_ptr(value), speed); });
		EndRow();
		return changed;
	}

	bool PropertyColor(const char* label, glm::vec3& color, bool hdr, const char* tooltip)
	{
		BeginRow(label, tooltip);
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float;
		if (hdr)
			flags |= ImGuiColorEditFlags_HDR;
		const bool changed = FiniteEdit(glm::value_ptr(color), 3, label, [&] { return ImGui::ColorEdit3("##value", glm::value_ptr(color), flags); });
		EndRow();
		return changed;
	}

	bool PropertyColor(const char* label, glm::vec4& color, bool hdr, const char* tooltip)
	{
		BeginRow(label, tooltip);
		ImGuiColorEditFlags flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar;
		if (hdr)
			flags |= ImGuiColorEditFlags_HDR;
		const bool changed = FiniteEdit(glm::value_ptr(color), 4, label, [&] { return ImGui::ColorEdit4("##value", glm::value_ptr(color), flags); });
		EndRow();
		return changed;
	}

	bool PropertyCombo(const char* label, int& selected, const char* const* items, int itemCount, const char* tooltip)
	{
		BeginRow(label, tooltip);
		bool changed = false;
		const char* preview = selected >= 0 && selected < itemCount ? items[selected] : "";
		if (ImGui::BeginCombo("##value", preview))
		{
			for (int i = 0; i < itemCount; i++)
			{
				const bool isSelected = i == selected;
				if (ImGui::Selectable(items[i], isSelected))
				{
					selected = i;
					changed = true;
				}
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		EndRow();
		return changed;
	}

	bool PropertyAsset(const char* label, std::string& path, AssetType type, const char* tooltip, bool allowBuiltins)
	{
		BeginRow(label, tooltip);
		bool changed = false;

		const float buttonWidth = ImGui::GetFrameHeight();
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float fieldWidth = ImGui::GetContentRegionAvail().x - (buttonWidth + spacing) * (path.empty() ? 1.0f : 2.0f);

		const std::string display = path.empty() ? std::string("None") : path;
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(path.empty() ? Theme::TextDisabled : Theme::Text));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
		const std::string buttonLabel = std::string(AssetIcon(type)) + "  " + display;
		ImGui::Button(buttonLabel.c_str(), ImVec2(std::max(fieldWidth, 20.0f), 0.0f));
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		if (!path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("%s", path.c_str());

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetPayload))
			{
				const std::string dropped(static_cast<const char*>(payload->Data));
				if (AssetManager::CanUseAs(dropped, type))
				{
					path = dropped;
					changed = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine(0.0f, spacing);
		if (ImGui::Button(ICON_SEARCH, ImVec2(buttonWidth, 0.0f)))
			ImGui::OpenPopup("##AssetPicker");
		Tooltip("Choose an asset");

		if (!path.empty())
		{
			ImGui::SameLine(0.0f, spacing);
			if (ImGui::Button(ICON_X, ImVec2(buttonWidth, 0.0f)))
			{
				path.clear();
				changed = true;
			}
			Tooltip("Clear");
		}

		ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f * Theme::GetScale(), 0.0f), ImVec2(520.0f * Theme::GetScale(), 360.0f * Theme::GetScale()));
		if (ImGui::BeginPopup("##AssetPicker"))
		{
			static std::string s_Filter;
			if (ImGui::IsWindowAppearing())
			{
				s_Filter.clear();
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Search", &s_Filter);

			auto matches = [](const std::string& candidate)
			{
				if (s_Filter.empty())
					return true;
				std::string a = candidate, b = s_Filter;
				std::transform(a.begin(), a.end(), a.begin(), ::tolower);
				std::transform(b.begin(), b.end(), b.begin(), ::tolower);
				return a.find(b) != std::string::npos;
			};

			if (allowBuiltins && type == AssetType::Mesh)
			{
				for (const std::string& builtin : AssetManager::GetBuiltinMeshPaths())
				{
					if (matches(builtin) && ImGui::Selectable((std::string(ICON_SHAPES "  ") + builtin).c_str(), builtin == path))
					{
						path = builtin;
						changed = true;
					}
				}
				ImGui::Separator();
			}

			const std::filesystem::path assets = AssetManager::GetAssetDirectory();
			std::error_code error;
			int shown = 0;
			if (!assets.empty() && std::filesystem::exists(assets, error))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(assets, std::filesystem::directory_options::skip_permission_denied, error))
				{
					if (!entry.is_regular_file(error) || !AssetManager::CanUseAs(entry.path(), type))
						continue;
					const std::string assetPath = AssetManager::GetAssetPath(entry.path());
					if (assetPath.empty() || assetPath.rfind(".bellows", 0) == 0 || !matches(assetPath))
						continue;
					shown++;
					if (ImGui::Selectable((std::string(AssetIcon(type)) + "  " + assetPath).c_str(), assetPath == path))
					{
						path = assetPath;
						changed = true;
					}
				}
			}
			if (shown == 0 && !(allowBuiltins && type == AssetType::Mesh))
				TextMuted("No matching assets in the project");
			ImGui::EndPopup();
		}

		EndRow();
		return changed;
	}

	namespace {

		// A small picture of a preset: the parent's frame, and the anchors as a dot, a bar or a block.
		void DrawAnchorPresetIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, UIAnchorPreset preset, bool highlighted)
		{
			glm::vec2 anchorMin, anchorMax;
			GetUIAnchorPreset(preset, anchorMin, anchorMax);
			const float inset = (max.x - min.x) * 0.2f;
			const ImVec2 frameMin(min.x + inset, min.y + inset), frameMax(max.x - inset, max.y - inset);
			const ImVec2 frameSize(frameMax.x - frameMin.x, frameMax.y - frameMin.y);
			const ImU32 color = highlighted ? Theme::Accent : Theme::Text;
			drawList->AddRect(frameMin, frameMax, Theme::TextMuted, 1.0f);
			const float mark = std::max(frameSize.x * 0.14f, 2.0f);
			ImVec2 markMin(frameMin.x + anchorMin.x * frameSize.x - mark, frameMin.y + anchorMin.y * frameSize.y - mark);
			ImVec2 markMax(frameMin.x + anchorMax.x * frameSize.x + mark, frameMin.y + anchorMax.y * frameSize.y + mark);
			markMin = ImVec2(std::max(markMin.x, frameMin.x), std::max(markMin.y, frameMin.y));
			markMax = ImVec2(std::min(markMax.x, frameMax.x), std::min(markMax.y, frameMax.y));
			drawList->AddRectFilled(markMin, markMax, color, 1.0f);
		}

		int FindAnchorPreset(const glm::vec2& anchorMin, const glm::vec2& anchorMax)
		{
			for (int i = 0; i < UIAnchorPresetCount; i++)
			{
				glm::vec2 presetMin, presetMax;
				GetUIAnchorPreset(static_cast<UIAnchorPreset>(i), presetMin, presetMax);
				if (glm::all(glm::lessThan(glm::abs(presetMin - anchorMin), glm::vec2(1e-4f))) && glm::all(glm::lessThan(glm::abs(presetMax - anchorMax), glm::vec2(1e-4f))))
					return i;
			}
			return -1;
		}

	}

	bool PropertyAnchorPreset(const char* label, const glm::vec2& anchorMin, const glm::vec2& anchorMax, int& outPreset, const char* tooltip)
	{
		// Laid out like the presets' meaning: the nine points, then a stretching row and column.
		static constexpr UIAnchorPreset c_Grid[4][4] = {
			{ UIAnchorPreset::TopLeft, UIAnchorPreset::TopCenter, UIAnchorPreset::TopRight, UIAnchorPreset::TopWide },
			{ UIAnchorPreset::CenterLeft, UIAnchorPreset::Center, UIAnchorPreset::CenterRight, UIAnchorPreset::MiddleWide },
			{ UIAnchorPreset::BottomLeft, UIAnchorPreset::BottomCenter, UIAnchorPreset::BottomRight, UIAnchorPreset::BottomWide },
			{ UIAnchorPreset::LeftTall, UIAnchorPreset::CenterTall, UIAnchorPreset::RightTall, UIAnchorPreset::Full },
		};

		bool picked = false;
		BeginRow(label, tooltip);
		const int current = FindAnchorPreset(anchorMin, anchorMax);
		const std::string text = std::string(current >= 0 ? UIAnchorPresetToString(static_cast<UIAnchorPreset>(current)) : "Custom") + "  " ICON_CHEVRON_DOWN;
		if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f)))
			ImGui::OpenPopup("AnchorPresets");
		if (ImGui::BeginPopup("AnchorPresets"))
		{
			TextMuted("Click: anchors only, the rect stays put");
			TextMuted("Shift+click: also move it there");
			const float size = ImGui::GetFrameHeight() * 1.6f;
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			for (int row = 0; row < 4; row++)
			{
				for (int column = 0; column < 4; column++)
				{
					const UIAnchorPreset preset = c_Grid[row][column];
					ImGui::PushID(row * 4 + column);
					if (column > 0)
						ImGui::SameLine(0.0f, column == 3 ? ImGui::GetStyle().ItemSpacing.x * 3.0f : -1.0f);
					const ImVec2 position = ImGui::GetCursorScreenPos();
					if (ImGui::InvisibleButton("##preset", ImVec2(size, size)))
					{
						outPreset = static_cast<int>(preset);
						picked = true;
						ImGui::CloseCurrentPopup();
					}
					const bool hovered = ImGui::IsItemHovered();
					if (hovered)
						ImGui::SetTooltip("%s", UIAnchorPresetToString(preset));
					const ImVec2 end(position.x + size, position.y + size);
					drawList->AddRectFilled(position, end, hovered ? Theme::FieldHovered : Theme::Field, 4.0f);
					DrawAnchorPresetIcon(drawList, position, end, preset, hovered || static_cast<int>(preset) == current);
					ImGui::PopID();
				}
				if (row == 2)
					ImGui::Spacing();
			}
			ImGui::EndPopup();
		}
		EndRow();
		return picked;
	}

	void PropertyText(const char* label, const char* value)
	{
		BeginRow(label, nullptr);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(value);
		EndRow();
	}

	bool ComponentHeader(const char* icon, const char* name, bool canRemove, bool& outRemove, bool& outReset)
	{
		outRemove = false;
		outReset = false;

		ImGui::PushID(name);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
		ImGui::PushStyleColor(ImGuiCol_Header, Theme::ToVec4(Theme::PanelRaised));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::ToVec4(Theme::Field));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::ToVec4(Theme::FieldHovered));
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);

		const std::string title = std::string(icon) + "   " + name;
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
		const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		const bool open = ImGui::TreeNodeEx("##component", flags, "%s", title.c_str());

		ImGui::PopFont();
		ImGui::PopStyleColor(3);
		const float frameHeight = ImGui::GetFrameHeight();
		ImGui::PopStyleVar();

		ImGui::SameLine(rightEdge - frameHeight);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		if (ImGui::Button(ICON_ELLIPSIS, ImVec2(frameHeight, frameHeight)))
			ImGui::OpenPopup("##ComponentMenu");
		ImGui::PopStyleColor();

		if (ImGui::BeginPopup("##ComponentMenu"))
		{
			if (ImGui::MenuItem(ICON_REFRESH_CW "  Reset"))
				outReset = true;
			if (ImGui::MenuItem(ICON_TRASH_2 "  Remove component", nullptr, false, canRemove))
				outRemove = true;
			ImGui::EndPopup();
		}

		if (open)
			ImGui::TreePop();
		ImGui::PopID();
		return open;
	}

	bool IconButton(const char* icon, const char* tooltip, bool active, ImVec2 size)
	{
		if (size.x <= 0.0f)
			size = ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

		ImGui::PushStyleColor(ImGuiCol_Button, active ? Theme::ToVec4(Theme::AccentMuted) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(active ? Theme::AccentMuted : Theme::FieldHovered));
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(active ? Theme::AccentHovered : Theme::Text));
		const bool pressed = ImGui::Button(icon, size);
		ImGui::PopStyleColor(3);
		if (tooltip)
			Tooltip(tooltip);
		return pressed;
	}

	void Tooltip(const char* text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
			ImGui::SetTooltip("%s", text);
			ImGui::PopStyleVar();
		}
	}

	bool BeginPanel(const char* name, bool* open, ImGuiWindowFlags flags)
	{
		ImGuiWindowClass windowClass;
		windowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
		ImGui::SetNextWindowClass(&windowClass);
		return ImGui::Begin(name, open, flags);
	}

	void TextMuted(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}

	void SectionHeader(const char* text)
	{
		ImGui::Spacing();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
		ImGui::SeparatorText(text);
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	void HelpMarker(const char* text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFO);
		Tooltip(text);
	}

	void CenteredText(const char* text, float verticalOffset)
	{
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, available.x * 0.8f);
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5f,
			ImGui::GetCursorPosY() + (available.y - size.y) * 0.5f + verticalOffset));
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextDisabled));
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + available.x * 0.8f);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	const char* AssetIcon(AssetType type)
	{
		switch (type)
		{
			case AssetType::Scene:			return ICON_LAYERS;
			case AssetType::Prefab:			return ICON_PACKAGE;
			case AssetType::Mesh:			return ICON_BOX;
			case AssetType::Material:		return ICON_PALETTE;
			case AssetType::Texture:		return ICON_IMAGE;
			case AssetType::Environment:	return ICON_MOUNTAIN;
			case AssetType::Audio:			return ICON_MUSIC;
			case AssetType::Script:			return ICON_FILE_CODE;
			case AssetType::Font:			return ICON_FILE_TYPE;
			case AssetType::Theme:			return ICON_PALETTE;
			default:						return ICON_FILE;
		}
	}

	ImU32 AssetColor(AssetType type)
	{
		switch (type)
		{
			case AssetType::Scene:			return IM_COL32(242, 112, 26, 255);
			case AssetType::Prefab:			return IM_COL32(96, 160, 245, 255);
			case AssetType::Mesh:			return IM_COL32(150, 200, 255, 255);
			case AssetType::Material:		return IM_COL32(230, 120, 200, 255);
			case AssetType::Texture:		return IM_COL32(110, 205, 160, 255);
			case AssetType::Environment:	return IM_COL32(240, 200, 90, 255);
			case AssetType::Audio:			return IM_COL32(170, 140, 245, 255);
			case AssetType::Script:			return IM_COL32(120, 210, 230, 255);
			case AssetType::Font:			return IM_COL32(235, 225, 200, 255);
			case AssetType::Theme:			return IM_COL32(245, 135, 58, 255);
			default:						return Theme::TextMuted;
		}
	}

	std::string OpenFileDialog(const char* title, const std::vector<std::string>& filters, const std::string& defaultPath)
	{
		if (!pfd::settings::available())
			return {};
		const std::vector<std::string> result = pfd::open_file(title, defaultPath, filters, pfd::opt::none).result();
		return result.empty() ? std::string() : result.front();
	}

	std::string SaveFileDialog(const char* title, const std::vector<std::string>& filters, const std::string& defaultPath)
	{
		if (!pfd::settings::available())
			return {};
		return pfd::save_file(title, defaultPath, filters, pfd::opt::force_overwrite).result();
	}

	std::string SelectFolderDialog(const char* title, const std::string& defaultPath)
	{
		if (!pfd::settings::available())
			return {};
		return pfd::select_folder(title, defaultPath).result();
	}

}
