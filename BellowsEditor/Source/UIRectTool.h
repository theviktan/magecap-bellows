#pragma once

#include "Bellows/Core/UUID.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/UI/UILayout.h"

#include <glm/glm.hpp>

#include <string>
#include <utility>

namespace Bellows {

	class Scene;

	// The UI view's handles on the selected UI rect: drag it to move it, an edge or corner to resize it,
	// or one of its anchor markers (in its parent) to move its anchors while it stays where it is. Ctrl
	// snaps offsets to 8 reference pixels and anchors to steps of 0.05. Rects placed by a UILayout show
	// their outline only.
	class UIRectTool
	{
	public:
		// The viewport image's top-left corner on screen, and screen pixels per rendered pixel.
		struct ImageMapping
		{
			glm::vec2 Origin { 0.0f };
			glm::vec2 Scale { 1.0f };
		};

		// Draws the selected rect's handles over the viewport and runs a drag. Returns true while the
		// tool has the mouse (the viewport then doesn't pick). 'editable' is false outside Edit mode.
		bool OnImGui(Scene& scene, Entity selected, const ImageMapping& mapping, bool hovered, bool editable);
		bool IsDragging() const { return m_Drag != DragKind::None; }
		// After a drag ends: what it did, for the undo list ("Move UI rect", ...). Empty otherwise.
		std::string TakeFinishedDrag();
		// True once after a press inside the rect was let go without moving: a click, so the viewport
		// picks what is under it (a child of the rect, say).
		bool TakeClick() { return std::exchange(m_Clicked, false); }
	private:
		enum class DragKind : uint8_t { None, Rect, Anchor };
	private:
		DragKind m_Drag = DragKind::None;
		UIRectHandle m_Handle = UIRectHandle::None;
		int m_AnchorCorner = 0;							// 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
		glm::bvec2 m_AnchorsTogether { false };			// Per axis: min and max anchors were one point, and move as one
		UUID m_Entity = 0;
		glm::vec2 m_StartMouse { 0.0f };				// In rendered pixels
		UIRectangle m_StartRect;
		bool m_Moved = false;							// Past the drag threshold; nothing changes before
		bool m_Clicked = false;
		std::string m_Finished;
	};

}
