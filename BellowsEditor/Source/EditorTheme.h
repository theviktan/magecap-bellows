#pragma once

#include <imgui.h>

#include <filesystem>

namespace Bellows {

	// Visual identity of the MageCap Bellows editor: MageCap's navy surfaces, shared with MageCap Forge,
	// with an ember accent kept for the two things that must stand out, the selection and Play.
	// Everything else (toggles, focus, check marks, graphs) stays neutral.
	namespace Theme {

		// Surfaces (darkest to lightest)
		constexpr ImU32 Background		= IM_COL32(20, 22, 31, 255);
		constexpr ImU32 Panel			= IM_COL32(25, 28, 40, 255);
		constexpr ImU32 PanelRaised		= IM_COL32(34, 39, 64, 255);
		constexpr ImU32 Field			= IM_COL32(15, 17, 25, 255);
		constexpr ImU32 FieldHovered	= IM_COL32(21, 24, 36, 255);
		constexpr ImU32 FieldActive		= IM_COL32(27, 31, 47, 255);
		constexpr ImU32 Border			= IM_COL32(42, 47, 69, 255);
		constexpr ImU32 BorderStrong	= IM_COL32(59, 66, 98, 255);
		constexpr ImU32 Separator		= IM_COL32(38, 43, 64, 255);
		// A hovered row or button: clearly lighter, and never the selection's color.
		constexpr ImU32 Hover			= IM_COL32(34, 39, 64, 255);
		constexpr ImU32 Neutral			= IM_COL32(141, 148, 176, 255);
		constexpr ImU32 NeutralBright	= IM_COL32(163, 170, 194, 255);

		// Text
		constexpr ImU32 Text			= IM_COL32(231, 233, 242, 255);
		constexpr ImU32 TextMuted		= IM_COL32(163, 170, 194, 255);
		constexpr ImU32 TextDisabled	= IM_COL32(100, 107, 133, 255);

		// Accent (ember): the selection and Play only
		constexpr ImU32 Accent			= IM_COL32(242, 112, 26, 255);
		constexpr ImU32 AccentHovered	= IM_COL32(255, 138, 61, 255);
		constexpr ImU32 AccentMuted		= IM_COL32(242, 112, 26, 56);
		constexpr ImU32 AccentSubtle	= IM_COL32(242, 112, 26, 28);
		// A selected row's warm fill, and the same while it is pressed.
		constexpr ImU32 Selection		= IM_COL32(242, 112, 26, 46);
		constexpr ImU32 SelectionStrong	= IM_COL32(242, 112, 26, 77);

		// A primary action (Create, Save, Rename): light and neutral, so it leads without using the
		// accent that belongs to the selection and Play.
		constexpr ImU32 Primary			= IM_COL32(221, 225, 238, 255);
		constexpr ImU32 PrimaryHovered	= IM_COL32(240, 242, 249, 255);
		constexpr ImU32 PrimaryActive	= IM_COL32(200, 205, 222, 255);
		constexpr ImU32 PrimaryText		= IM_COL32(20, 22, 31, 255);

		// Semantic
		constexpr ImU32 Success			= IM_COL32(63, 207, 126, 255);
		constexpr ImU32 Warning			= IM_COL32(240, 185, 64, 255);
		constexpr ImU32 Error			= IM_COL32(229, 84, 79, 255);
		constexpr ImU32 Info			= IM_COL32(110, 139, 255, 255);

		// Axis colors for vector fields and gizmos
		constexpr ImU32 AxisX			= IM_COL32(229, 84, 79, 255);
		constexpr ImU32 AxisY			= IM_COL32(110, 196, 74, 255);
		constexpr ImU32 AxisZ			= IM_COL32(79, 126, 240, 255);

		// Corner radius of fields, buttons and tabs, before the DPI scale.
		constexpr float FrameRadius = 7.0f;

		constexpr float BaseFontSize = 15.0f;

		struct Fonts
		{
			ImFont* Regular = nullptr;
			ImFont* Medium = nullptr;
			ImFont* SemiBold = nullptr;
			ImFont* Bold = nullptr;
			ImFont* Mono = nullptr;
		};

		// Loads fonts from '<resources>/Fonts' and applies the style. 'scale' follows the monitor DPI.
		void Apply(const std::filesystem::path& resourceDirectory, float scale);
		const Fonts& GetFonts();
		float GetScale();

		inline ImVec4 ToVec4(ImU32 color) { return ImGui::ColorConvertU32ToFloat4(color); }
		// The same color with another opacity (0 to 255).
		constexpr ImU32 WithAlpha(ImU32 color, unsigned alpha) { return (color & ~IM_COL32_A_MASK) | ((alpha & 0xFFu) << IM_COL32_A_SHIFT); }

	}

}
