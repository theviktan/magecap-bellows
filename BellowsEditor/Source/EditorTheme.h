#pragma once

#include <imgui.h>

#include <filesystem>

namespace Bellows {

	// Visual identity of the MageCap Bellows editor: graphite surfaces with an ember accent.
	namespace Theme {

		// Surfaces (darkest to lightest)
		constexpr ImU32 Background		= IM_COL32(13, 15, 19, 255);
		constexpr ImU32 Panel			= IM_COL32(20, 23, 29, 255);
		constexpr ImU32 PanelRaised		= IM_COL32(26, 30, 37, 255);
		constexpr ImU32 Field			= IM_COL32(30, 34, 42, 255);
		constexpr ImU32 FieldHovered	= IM_COL32(37, 42, 52, 255);
		constexpr ImU32 FieldActive		= IM_COL32(44, 50, 62, 255);
		constexpr ImU32 Border			= IM_COL32(40, 45, 55, 255);
		constexpr ImU32 Separator		= IM_COL32(33, 37, 46, 255);

		// Text
		constexpr ImU32 Text			= IM_COL32(228, 231, 236, 255);
		constexpr ImU32 TextMuted		= IM_COL32(148, 155, 168, 255);
		constexpr ImU32 TextDisabled	= IM_COL32(96, 103, 115, 255);

		// Accent (ember)
		constexpr ImU32 Accent			= IM_COL32(242, 112, 26, 255);
		constexpr ImU32 AccentHovered	= IM_COL32(255, 138, 61, 255);
		constexpr ImU32 AccentMuted		= IM_COL32(242, 112, 26, 56);
		constexpr ImU32 AccentSubtle	= IM_COL32(242, 112, 26, 28);

		// Semantic
		constexpr ImU32 Success			= IM_COL32(74, 200, 128, 255);
		constexpr ImU32 Warning			= IM_COL32(240, 185, 64, 255);
		constexpr ImU32 Error			= IM_COL32(236, 84, 84, 255);
		constexpr ImU32 Info			= IM_COL32(96, 160, 245, 255);

		// Axis colors for vector fields and gizmos
		constexpr ImU32 AxisX			= IM_COL32(222, 74, 74, 255);
		constexpr ImU32 AxisY			= IM_COL32(110, 196, 74, 255);
		constexpr ImU32 AxisZ			= IM_COL32(74, 120, 230, 255);

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

	}

}
