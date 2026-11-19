#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/UI/UIStyle.h"
#include "Bellows/UI/UITheme.h"

#include <glm/gtc/epsilon.hpp>

using namespace Bellows;

namespace {

	bool Near(const glm::vec4& a, const glm::vec4& b)
	{
		return glm::all(glm::epsilonEqual(a, b, 1.0f / 255.0f));
	}

}

TEST_CASE("Theme colors read as #RRGGBB, #RRGGBBAA or numbers, and write back as hex")
{
	CHECK(Near(*ParseUIColor("#F5873A"), UIColors::Accent));
	CHECK(Near(*ParseUIColor("#ff000080"), glm::vec4(1.0f, 0.0f, 0.0f, 128.0f / 255.0f)));
	CHECK_FALSE(ParseUIColor("F5873A"));
	CHECK_FALSE(ParseUIColor("#F5873"));
	CHECK_FALSE(ParseUIColor("#GG873A"));
	CHECK(UIColorToString(glm::vec4(1.0f, 0.5f, 0.0f, 1.0f)) == "#FF8000");
	CHECK(UIColorToString(glm::vec4(0.0f, 0.0f, 0.0f, 0.5f)) == "#00000080");
}

TEST_CASE("The default theme is the MageCap look the widgets drew before themes")
{
	const UITheme& theme = GetDefaultUITheme();
	CHECK(theme.Font.empty());
	CHECK(Near(theme.Accent, UIColors::Accent));
	const UIWidgetStyle& button = theme.Get(UIThemeWidget::Button);
	CHECK(Near(button.Get(UIWidgetState::Normal).Background, UIColors::NavyLight));
	CHECK(Near(button.Get(UIWidgetState::Hover).Background, UIColors::Slate));
	CHECK(Near(button.Get(UIWidgetState::Pressed).Background, UIColors::Accent));
	const UIStateStyle& toggle = theme.Get(UIThemeWidget::Toggle).Get(UIWidgetState::Normal);
	CHECK(Near(toggle.Background, UIColors::Navy));
	CHECK(Near(toggle.Border, UIColors::OffWhite));
	CHECK(toggle.BorderWidth == 2.0f);
	CHECK(Near(theme.Get(UIThemeWidget::Slider).Get(UIWidgetState::Pressed).Text, UIColors::AccentLight));
	CHECK(Near(theme.Get(UIThemeWidget::DropdownList).Get(UIWidgetState::Hover).Background, UIColors::Slate));
	// Builtin/MageCap.btheme is the default itself, by name or by an empty path.
	CHECK(AssetManager::GetUITheme(c_DefaultUIThemePath).get() == AssetManager::GetUITheme("").get());
	CHECK(Near(AssetManager::GetUITheme("")->Accent, theme.Accent));
}

TEST_CASE("A theme file lists only what it changes; Normal sets every state first")
{
	std::string warnings;
	const std::optional<UITheme> theme = ParseUITheme(R"({
		"Accent": "#00C8FF",
		"TextSize": 32,
		"Button": {
			"CornerRadius": 4,
			"Normal": { "Background": "#202020", "Text": [1, 1, 0] },
			"Hover": { "Background": "#404040" }
		},
		"Classes": {
			"danger": { "Button": { "Normal": { "Background": "#C02020" } } }
		}
	})", nullptr, &warnings);
	REQUIRE(theme);
	CHECK(warnings.empty());
	CHECK(Near(theme->Accent, *ParseUIColor("#00C8FF")));
	CHECK(theme->TextSize == 32.0f);
	const UIWidgetStyle& button = theme->Get(UIThemeWidget::Button);
	CHECK(button.CornerRadius == 4.0f);
	CHECK(Near(button.Get(UIWidgetState::Normal).Background, *ParseUIColor("#202020")));
	CHECK(Near(button.Get(UIWidgetState::Pressed).Background, *ParseUIColor("#202020")));	// From Normal
	CHECK(Near(button.Get(UIWidgetState::Hover).Background, *ParseUIColor("#404040")));
	CHECK(Near(button.Get(UIWidgetState::Disabled).Text, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)));
	// What the file doesn't mention stays the default's.
	CHECK(Near(theme->Get(UIThemeWidget::Toggle).Get(UIWidgetState::Normal).Background, UIColors::Navy));
	CHECK(theme->Get(UIThemeWidget::Button).Get(UIWidgetState::Normal).BorderWidth == 0.0f);

	// A class starts from the theme's own style (this file's), not the default's.
	const UIWidgetStyle& danger = theme->Get(UIThemeWidget::Button, "danger");
	CHECK(Near(danger.Get(UIWidgetState::Normal).Background, *ParseUIColor("#C02020")));
	CHECK(danger.CornerRadius == 4.0f);
	CHECK(&theme->Get(UIThemeWidget::Button, "missing") == &theme->Get(UIThemeWidget::Button));
	CHECK(&theme->Get(UIThemeWidget::Toggle, "danger") == &theme->Get(UIThemeWidget::Toggle));
}

TEST_CASE("Mistakes in a theme are reported and keep the defaults")
{
	std::string error, warnings;
	CHECK_FALSE(ParseUITheme("[1, 2]", &error));
	CHECK(error.find("JSON object") != std::string::npos);
	CHECK_FALSE(ParseUITheme("{ not json", &error));

	const std::optional<UITheme> theme = ParseUITheme(R"({ "Accent": "orange", "TextSize": -4, "Slider": { "Normal": { "BorderWidth": "thick" } }, "Classes": 3 })",
		nullptr, &warnings);
	REQUIRE(theme);
	CHECK(Near(theme->Accent, UIColors::Accent));
	CHECK(theme->TextSize == GetDefaultUITheme().TextSize);
	CHECK(theme->Get(UIThemeWidget::Slider).Get(UIWidgetState::Normal).BorderWidth == 2.0f);
	CHECK(warnings.find("Accent should be a color") != std::string::npos);
	CHECK(warnings.find("TextSize should be a positive number") != std::string::npos);
	CHECK(warnings.find("Slider.Normal.BorderWidth should be") != std::string::npos);
	CHECK(warnings.find("Classes should be") != std::string::npos);
}

TEST_CASE("A written theme reads back the same, and assets load and fall back to the default")
{
	UITheme theme = GetDefaultUITheme();
	theme.Font = "Fonts/Title.ttf";
	theme.Get(UIThemeWidget::Slider).Get(UIWidgetState::Hover).Background = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
	theme.Classes["primary"][UIThemeWidget::Button] = theme.Get(UIThemeWidget::Button);
	theme.Classes["primary"][UIThemeWidget::Button].CornerRadius = 20.0f;
	const std::string text = WriteUITheme(theme);
	const std::optional<UITheme> read = ParseUITheme(text);
	REQUIRE(read);
	CHECK(WriteUITheme(*read) == text);
	CHECK(read->Font == "Fonts/Title.ttf");
	CHECK(read->Get(UIThemeWidget::Button, "primary").CornerRadius == 20.0f);

	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "Themes";
	REQUIRE(FileSystem::WriteTextFile(assets / "Themes" / "Copy.btheme", text));
	AssetManager::SetAssetDirectory(assets);
	AssetManager::Invalidate("Themes/Copy.btheme");
	CHECK(AssetManager::GetUITheme("Themes/Copy.btheme")->Font == "Fonts/Title.ttf");
	CHECK(AssetManager::GetAssetType("Themes/Copy.btheme") == AssetType::Theme);
	// A missing theme is reported and draws with the default instead.
	CHECK(AssetManager::GetUITheme("Themes/Missing.btheme").get() == AssetManager::GetUITheme("").get());
	AssetManager::SetAssetDirectory({});
}
