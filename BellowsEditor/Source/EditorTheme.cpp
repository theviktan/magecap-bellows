#include "EditorTheme.h"

#include "IconsLucide.h"

#include "Bellows/Core/Log.h"
#include "Bellows/Core/Platform.h"

#include <vector>

namespace Bellows::Theme {

	namespace {

		Fonts s_Fonts;
		float s_Scale = 1.0f;

		// A system font with plain (not color) emoji outlines, which stb_truetype can draw, or an empty
		// path. Using the system's copy keeps a large font out of the editor's files. Color-only emoji
		// fonts (Noto Color Emoji's bitmaps) can't be drawn, so they aren't on the list.
		const std::filesystem::path& FindEmojiFont()
		{
			static const std::filesystem::path s_Font = []
			{
				std::vector<std::filesystem::path> candidates;
#if defined(BW_PLATFORM_WINDOWS)
				const std::string windows = Platform::GetEnvVar("WINDIR");
				const std::filesystem::path fonts = std::filesystem::path(windows.empty() ? "C:\\Windows" : windows) / "Fonts";
				candidates = { fonts / "seguiemj.ttf", fonts / "seguisym.ttf" };
#else
				candidates = { "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf", "/usr/share/fonts/noto/NotoEmoji-Regular.ttf",
					"/usr/share/fonts/google-noto-emoji/NotoEmoji-Regular.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
					"/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf" };
#endif
				for (const std::filesystem::path& candidate : candidates)
				{
					std::error_code error;
					if (std::filesystem::is_regular_file(candidate, error))
						return candidate;
				}
				return std::filesystem::path();
			}();
			return s_Font;
		}

		ImFont* LoadFont(const std::filesystem::path& path, float size, bool mergeIcons, const std::filesystem::path& iconFont)
		{
			ImGuiIO& io = ImGui::GetIO();
			if (!std::filesystem::exists(path))
			{
				BW_CORE_WARN("Editor font '{}' is missing; falling back to the default font", path.string());
				return nullptr;
			}

			// Text fonts may carry their own private-use glyphs (Inter does), which would shadow
			// the icon font merged below; icons own the private-use area.
			static const ImWchar s_PrivateUseArea[] = { 0xE000, 0xF8FF, 0 };
			ImFontConfig config;
			config.GlyphExcludeRanges = s_PrivateUseArea;
			config.OversampleH = 2;
			config.OversampleV = 2;
			ImFont* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size, &config);

			if (font && mergeIcons && std::filesystem::exists(iconFont))
			{
				ImFontConfig iconConfig;
				iconConfig.MergeMode = true;
				iconConfig.PixelSnapH = true;
				iconConfig.GlyphMinAdvanceX = size;
				iconConfig.GlyphOffset = ImVec2(0.0f, size * 0.12f);
				io.Fonts->AddFontFromFileTTF(iconFont.string().c_str(), size, &iconConfig);
			}

			// Emoji and symbols the text font doesn't have (an entity named with an emoji) come from the
			// system's emoji font instead of showing as "?". Merged last, so it only fills gaps.
			if (const std::filesystem::path& emojiFont = FindEmojiFont(); font && !emojiFont.empty())
			{
				static const ImWchar s_EmojiRanges[] = { 0x2000, 0x2BFF, 0xFE00, 0xFE0F, 0x1F000, 0x1FAFF, 0 };
				ImFontConfig emojiConfig;
				emojiConfig.MergeMode = true;
				emojiConfig.GlyphRanges = s_EmojiRanges;
				io.Fonts->AddFontFromFileTTF(emojiFont.string().c_str(), size, &emojiConfig);
			}
			return font;
		}

	}

	const Fonts& GetFonts()
	{
		return s_Fonts;
	}

	float GetScale()
	{
		return s_Scale;
	}

	void Apply(const std::filesystem::path& resourceDirectory, float scale)
	{
		s_Scale = std::max(scale, 0.5f);
		const float size = BaseFontSize * s_Scale;

		const std::filesystem::path fonts = resourceDirectory / "Fonts";
		const std::filesystem::path icons = fonts / "Lucide" / "lucide.ttf";

		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		s_Fonts.Medium = LoadFont(fonts / "Inter" / "Inter-Medium.ttf", size, true, icons);
		s_Fonts.Regular = LoadFont(fonts / "Inter" / "Inter-Regular.ttf", size, true, icons);
		s_Fonts.SemiBold = LoadFont(fonts / "Inter" / "Inter-SemiBold.ttf", size, true, icons);
		s_Fonts.Bold = LoadFont(fonts / "Inter" / "Inter-Bold.ttf", size, true, icons);
		s_Fonts.Mono = LoadFont(fonts / "JetBrainsMono" / "JetBrainsMono-Regular.ttf", size * 0.93f, true, icons);
		if (!s_Fonts.Medium)
			s_Fonts.Medium = io.Fonts->AddFontDefault();
		for (ImFont** font : { &s_Fonts.Regular, &s_Fonts.SemiBold, &s_Fonts.Bold, &s_Fonts.Mono })
		{
			if (!*font)
				*font = s_Fonts.Medium;
		}
		io.FontDefault = s_Fonts.Medium;

		ImGuiStyle& style = ImGui::GetStyle();
		style = ImGuiStyle();
		style.WindowPadding = ImVec2(10.0f, 10.0f);
		style.FramePadding = ImVec2(8.0f, 5.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.IndentSpacing = 14.0f;
		style.ScrollbarSize = 11.0f;
		style.GrabMinSize = 9.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.TabBorderSize = 0.0f;
		style.WindowRounding = 0.0f;
		style.ChildRounding = 4.0f;
		style.FrameRounding = 4.0f;
		style.PopupRounding = 6.0f;
		style.ScrollbarRounding = 6.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 4.0f;
		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.SeparatorTextBorderSize = 1.0f;
		style.DockingSeparatorSize = 2.0f;
		style.TabBarOverlineSize = 2.0f;
		style.TabBarBorderSize = 1.0f;

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = ToVec4(Text);
		colors[ImGuiCol_TextDisabled] = ToVec4(TextDisabled);
		colors[ImGuiCol_WindowBg] = ToVec4(Panel);
		colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_PopupBg] = ToVec4(PanelRaised);
		colors[ImGuiCol_Border] = ToVec4(Border);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_FrameBg] = ToVec4(Field);
		colors[ImGuiCol_FrameBgHovered] = ToVec4(FieldHovered);
		colors[ImGuiCol_FrameBgActive] = ToVec4(FieldActive);
		colors[ImGuiCol_TitleBg] = ToVec4(Background);
		colors[ImGuiCol_TitleBgActive] = ToVec4(Background);
		colors[ImGuiCol_TitleBgCollapsed] = ToVec4(Background);
		colors[ImGuiCol_MenuBarBg] = ToVec4(Background);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_ScrollbarGrab] = ToVec4(FieldActive);
		colors[ImGuiCol_ScrollbarGrabHovered] = ToVec4(IM_COL32(60, 67, 82, 255));
		colors[ImGuiCol_ScrollbarGrabActive] = ToVec4(Accent);
		colors[ImGuiCol_CheckMark] = ToVec4(Accent);
		colors[ImGuiCol_SliderGrab] = ToVec4(Accent);
		colors[ImGuiCol_SliderGrabActive] = ToVec4(AccentHovered);
		colors[ImGuiCol_Button] = ToVec4(Field);
		colors[ImGuiCol_ButtonHovered] = ToVec4(FieldHovered);
		colors[ImGuiCol_ButtonActive] = ToVec4(FieldActive);
		colors[ImGuiCol_Header] = ToVec4(AccentMuted);
		colors[ImGuiCol_HeaderHovered] = ToVec4(IM_COL32(242, 112, 26, 38));
		colors[ImGuiCol_HeaderActive] = ToVec4(IM_COL32(242, 112, 26, 80));
		colors[ImGuiCol_Separator] = ToVec4(Separator);
		colors[ImGuiCol_SeparatorHovered] = ToVec4(AccentMuted);
		colors[ImGuiCol_SeparatorActive] = ToVec4(Accent);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_ResizeGripHovered] = ToVec4(AccentMuted);
		colors[ImGuiCol_ResizeGripActive] = ToVec4(Accent);
		colors[ImGuiCol_InputTextCursor] = ToVec4(Accent);
		colors[ImGuiCol_Tab] = ToVec4(Background);
		colors[ImGuiCol_TabHovered] = ToVec4(PanelRaised);
		colors[ImGuiCol_TabSelected] = ToVec4(Panel);
		colors[ImGuiCol_TabSelectedOverline] = ToVec4(Accent);
		colors[ImGuiCol_TabDimmed] = ToVec4(Background);
		colors[ImGuiCol_TabDimmedSelected] = ToVec4(Panel);
		colors[ImGuiCol_TabDimmedSelectedOverline] = ToVec4(Border);
		colors[ImGuiCol_DockingPreview] = ToVec4(AccentMuted);
		colors[ImGuiCol_DockingEmptyBg] = ToVec4(Background);
		colors[ImGuiCol_PlotLines] = ToVec4(Accent);
		colors[ImGuiCol_PlotHistogram] = ToVec4(Accent);
		colors[ImGuiCol_TableHeaderBg] = ToVec4(PanelRaised);
		colors[ImGuiCol_TableBorderStrong] = ToVec4(Border);
		colors[ImGuiCol_TableBorderLight] = ToVec4(Separator);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.015f);
		colors[ImGuiCol_TextSelectedBg] = ToVec4(AccentMuted);
		colors[ImGuiCol_DragDropTarget] = ToVec4(Accent);
		colors[ImGuiCol_NavCursor] = ToVec4(Accent);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.72f);

		style.ScaleAllSizes(s_Scale);
	}

}
