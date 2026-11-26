#include "UIRectTool.h"

#include "EditorTheme.h"

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <utility>

namespace Bellows {

	namespace {

		constexpr float c_HandleSize = 7.0f;		// Screen pixels, times the editor's UI scale
		constexpr float c_HandleReach = 6.0f;
		constexpr float c_AnchorReach = 8.0f;
		constexpr float c_OffsetSnap = 8.0f;		// Reference pixels, with Ctrl
		constexpr float c_AnchorSnap = 0.05f;
		constexpr float c_DragThreshold = 3.0f;		// Screen pixels before a press becomes a drag

		ImVec2 ToImVec(const glm::vec2& v) { return ImVec2(v.x, v.y); }

		// The four anchor markers: each corner's anchor point in the parent area.
		std::array<glm::vec2, 4> AnchorPoints(const UIRectComponent& rect, const UIRectangle& parent)
		{
			const glm::vec2 size = parent.GetSize();
			return { parent.Min + glm::vec2(rect.AnchorMin.x, rect.AnchorMin.y) * size,
				parent.Min + glm::vec2(rect.AnchorMax.x, rect.AnchorMin.y) * size,
				parent.Min + glm::vec2(rect.AnchorMin.x, rect.AnchorMax.y) * size,
				parent.Min + glm::vec2(rect.AnchorMax.x, rect.AnchorMax.y) * size };
		}

		ImGuiMouseCursor CursorFor(UIRectHandle handle)
		{
			switch (handle)
			{
				case UIRectHandle::Move:		return ImGuiMouseCursor_ResizeAll;
				case UIRectHandle::Left:
				case UIRectHandle::Right:		return ImGuiMouseCursor_ResizeEW;
				case UIRectHandle::Top:
				case UIRectHandle::Bottom:		return ImGuiMouseCursor_ResizeNS;
				case UIRectHandle::TopLeft:
				case UIRectHandle::BottomRight:	return ImGuiMouseCursor_ResizeNWSE;
				case UIRectHandle::TopRight:
				case UIRectHandle::BottomLeft:	return ImGuiMouseCursor_ResizeNESW;
				case UIRectHandle::None:		break;
			}
			return ImGuiMouseCursor_Arrow;
		}

		// A small triangle pointing at the anchor point from its corner's side, like Godot's.
		void DrawAnchorMarker(ImDrawList* drawList, const glm::vec2& point, int corner, float size, ImU32 color)
		{
			const glm::vec2 direction(corner % 2 == 0 ? -1.0f : 1.0f, corner < 2 ? -1.0f : 1.0f);
			const glm::vec2 a = point;
			const glm::vec2 b = point + glm::vec2(direction.x * size * 1.6f, direction.y * size * 0.6f);
			const glm::vec2 c = point + glm::vec2(direction.x * size * 0.6f, direction.y * size * 1.6f);
			drawList->AddTriangleFilled(ToImVec(a), ToImVec(b), ToImVec(c), color);
			drawList->AddTriangle(ToImVec(a), ToImVec(b), ToImVec(c), Theme::Background);
		}

	}

	bool UIRectTool::OnImGui(Scene& scene, Entity selected, const ImageMapping& mapping, bool hovered, bool editable)
	{
		const std::optional<Scene::UIRectPlacement> placement = scene.GetUIRectPlacement(selected);
		auto* rect = selected ? selected.TryGetComponent<UIRectComponent>() : nullptr;
		if (!placement || !rect || (m_Drag != DragKind::None && selected.GetUUID() != m_Entity))
		{
			m_Drag = DragKind::None;
			return false;
		}

		const float uiScale = Theme::GetScale();
		const auto toScreen = [&](const glm::vec2& pixel) { return mapping.Origin + pixel * mapping.Scale; };
		const auto toPixel = [&](const ImVec2& screen) { return (glm::vec2(screen.x, screen.y) - mapping.Origin) / mapping.Scale; };
		const UIRectangle screenRect { toScreen(placement->Rect.Min), toScreen(placement->Rect.Max) };
		const UIRectangle screenParent { toScreen(placement->ParentArea.Min), toScreen(placement->ParentArea.Max) };
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const bool canEdit = editable && !placement->PlacedByLayout;

		// The parent area faintly, then the rect.
		drawList->AddRect(ToImVec(screenParent.Min), ToImVec(screenParent.Max), IM_COL32(96, 160, 245, 90), 0.0f, 1.0f * uiScale);
		drawList->AddRect(ToImVec(screenRect.Min), ToImVec(screenRect.Max), canEdit ? Theme::Accent : Theme::TextMuted, 0.0f, 1.5f * uiScale);
		if (!canEdit)
		{
			if (placement->PlacedByLayout && hovered && screenRect.Contains(glm::vec2(ImGui::GetMousePos().x, ImGui::GetMousePos().y)))
				ImGui::SetTooltip("Placed by its parent's UI Layout: change the layout, or its size in the Inspector");
			m_Drag = DragKind::None;
			return false;
		}

		const std::array<glm::vec2, 4> anchors = AnchorPoints(*rect, placement->ParentArea);
		const float handle = c_HandleSize * uiScale;
		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		// Which part the mouse is over (anchor markers first, they sit on top).
		int anchorUnder = -1;
		for (int i = 0; i < 4; i++)
		{
			if (glm::distance(toScreen(anchors[static_cast<size_t>(i)]), mouse) <= c_AnchorReach * uiScale)
				anchorUnder = i;
		}
		const UIRectHandle handleUnder = anchorUnder >= 0 ? UIRectHandle::None : FindUIRectHandle(screenRect, mouse, c_HandleReach * uiScale);

		// Start a drag on a press over a part.
		if (m_Drag == DragKind::None && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyAlt)
		{
			if (anchorUnder >= 0 || handleUnder != UIRectHandle::None)
			{
				m_Drag = anchorUnder >= 0 ? DragKind::Anchor : DragKind::Rect;
				m_Handle = handleUnder;
				m_AnchorCorner = anchorUnder;
				m_Entity = selected.GetUUID();
				m_StartMouse = toPixel(ImGui::GetMousePos());
				m_StartRect = placement->Rect;
				m_AnchorsTogether = glm::equal(rect->AnchorMin, rect->AnchorMax);
				m_Moved = false;
			}
		}

		const bool snap = ImGui::GetIO().KeyCtrl;
		if (m_Drag != DragKind::None && !m_Moved)
			m_Moved = glm::length((toPixel(ImGui::GetMousePos()) - m_StartMouse) * mapping.Scale) >= c_DragThreshold * uiScale;
		// Until the mouse moves past the threshold nothing changes: letting go then is a click.
		if (m_Drag == DragKind::Rect && m_Moved)
		{
			ImGui::SetMouseCursor(CursorFor(m_Handle));
			const glm::vec2 delta = toPixel(ImGui::GetMousePos()) - m_StartMouse;
			const UIRectangle dragged = DragUIRect(m_StartRect, m_Handle, delta, 1.0f);
			ComputeUIOffsetsFor(dragged, rect->AnchorMin, rect->AnchorMax, placement->ParentArea, placement->Scale, rect->OffsetMin, rect->OffsetMax);
			if (snap)
				SnapUIOffsets(m_Handle, rect->OffsetMin, rect->OffsetMax, c_OffsetSnap);
		}
		else if (m_Drag == DragKind::Anchor && m_Moved)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			const glm::vec2 parentSize = glm::max(placement->ParentArea.GetSize(), glm::vec2(1.0f));
			glm::vec2 point = glm::clamp((toPixel(ImGui::GetMousePos()) - placement->ParentArea.Min) / parentSize, 0.0f, 1.0f);
			if (snap)
				point = glm::round(point / c_AnchorSnap) * c_AnchorSnap;
			// A corner's marker moves that corner's anchors, along an axis where they were one point both of
			// them (the markers sit on top of each other there); min never passes max.
			const glm::bvec2 movesMax(m_AnchorCorner % 2 == 1, m_AnchorCorner >= 2);
			for (int axis = 0; axis < 2; axis++)
			{
				if (m_AnchorsTogether[axis])
					rect->AnchorMin[axis] = rect->AnchorMax[axis] = point[axis];
				else if (movesMax[axis])
					rect->AnchorMax[axis] = std::max(point[axis], rect->AnchorMin[axis]);
				else
					rect->AnchorMin[axis] = std::min(point[axis], rect->AnchorMax[axis]);
			}
			// The rect stays where it was on screen.
			ComputeUIOffsetsFor(m_StartRect, rect->AnchorMin, rect->AnchorMax, placement->ParentArea, placement->Scale, rect->OffsetMin, rect->OffsetMax);
		}
		else if (m_Drag == DragKind::None && hovered && (anchorUnder >= 0 || handleUnder != UIRectHandle::None))
		{
			ImGui::SetMouseCursor(anchorUnder >= 0 ? ImGuiMouseCursor_Hand : CursorFor(handleUnder));
		}

		if (m_Drag != DragKind::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (m_Moved)
				m_Finished = m_Drag == DragKind::Anchor ? "Move UI anchors" : (m_Handle == UIRectHandle::Move ? "Move UI rect" : "Resize UI rect");
			else
				m_Clicked = m_Drag == DragKind::Rect && m_Handle == UIRectHandle::Move;
			m_Drag = DragKind::None;
		}

		// Handles at the corners and the middle of each edge, then the anchor markers.
		const glm::vec2 center = (screenRect.Min + screenRect.Max) * 0.5f;
		const std::array<glm::vec2, 8> handles = { screenRect.Min, glm::vec2(center.x, screenRect.Min.y), glm::vec2(screenRect.Max.x, screenRect.Min.y),
			glm::vec2(screenRect.Min.x, center.y), glm::vec2(screenRect.Max.x, center.y),
			glm::vec2(screenRect.Min.x, screenRect.Max.y), glm::vec2(center.x, screenRect.Max.y), screenRect.Max };
		for (const glm::vec2& point : handles)
		{
			drawList->AddRectFilled(ToImVec(point - handle * 0.5f), ToImVec(point + handle * 0.5f), Theme::Text);
			drawList->AddRect(ToImVec(point - handle * 0.5f), ToImVec(point + handle * 0.5f), Theme::Accent);
		}
		for (int i = 0; i < 4; i++)
		{
			const bool active = m_Drag == DragKind::Anchor && m_AnchorCorner == i;
			DrawAnchorMarker(drawList, toScreen(anchors[static_cast<size_t>(i)]), i, 6.0f * uiScale,
				active || anchorUnder == i ? Theme::AccentHovered : Theme::Success);
		}
		return m_Drag != DragKind::None || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (anchorUnder >= 0 || handleUnder != UIRectHandle::None));
	}

	std::string UIRectTool::TakeFinishedDrag()
	{
		return std::exchange(m_Finished, std::string());
	}

}
