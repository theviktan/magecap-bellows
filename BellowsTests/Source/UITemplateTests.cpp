#include <doctest/doctest.h>

#include "UITemplates.h"

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Scene.h"

#include <set>
#include <string>

using namespace Bellows;

namespace {

	Entity Child(Entity parent, const std::string& name)
	{
		for (Entity child : parent.GetChildren())
		{
			if (child.GetComponent<TagComponent>().Tag == name)
				return child;
		}
		return {};
	}

	UIRectangle RectOf(Scene& scene, Entity entity)
	{
		const std::optional<Scene::UIRectPlacement> placement = scene.GetUIRectPlacement(entity);
		REQUIRE(placement);
		return placement->Rect;
	}

}

TEST_CASE("UI templates: names for scripts")
{
	std::set<std::string> names;
	for (int i = 0; i < UITemplateCount; i++)
	{
		const auto kind = static_cast<UITemplate>(i);
		const std::string name = UITemplateScriptName(kind);
		names.insert(name);
		CHECK(UITemplateFromName(name) == kind);
		CHECK(UITemplateFromName(UITemplateToString(kind)) == kind);		// "Main Menu" works too
	}
	CHECK(names.size() == static_cast<size_t>(UITemplateCount));
	CHECK(UITemplateFromName("main_menu") == UITemplate::MainMenu);
	CHECK(UITemplateFromName("Text-Input") == UITemplate::TextInput);
	CHECK_FALSE(UITemplateFromName("pause_menu"));
	CHECK(IsUIScreenTemplate(UITemplate::HUD));
	CHECK_FALSE(IsUIScreenTemplate(UITemplate::Button));

	CHECK(UIAnchorPresetFromName("top_left") == UIAnchorPreset::TopLeft);
	CHECK(UIAnchorPresetFromName("Full Rect") == UIAnchorPreset::Full);
	for (int i = 0; i < UIAnchorPresetCount; i++)
		CHECK(UIAnchorPresetFromName(UIAnchorPresetScriptName(static_cast<UIAnchorPreset>(i))) == static_cast<UIAnchorPreset>(i));
	CHECK_FALSE(UIAnchorPresetFromName("middle"));
}

TEST_CASE("UI templates: widgets go into a canvas")
{
	Ref<Scene> scene = CreateRef<Scene>();

	// Outside a canvas, a widget gets one of its own.
	Entity button = CreateUITemplate(*scene, Entity(), UITemplate::Button);
	REQUIRE(button);
	CHECK(button.HasComponent<UIButtonComponent>());
	CHECK(button.HasComponent<UIImageComponent>());
	CHECK(button.GetComponent<TextComponent>().ThemeColor);
	Entity canvas = button.GetParent();
	REQUIRE(canvas);
	CHECK(canvas.HasComponent<UICanvasComponent>());

	// Inside one, widgets join it, at any depth.
	Entity panel = CreateUITemplate(*scene, button, UITemplate::Panel);
	CHECK(panel.GetParent() == button);
	Entity toggle = CreateUITemplate(*scene, panel, UITemplate::Toggle);
	CHECK(toggle.GetParent() == panel);
	CHECK(scene->IsInsideUICanvas(toggle));

	// Every widget kind makes its components.
	CHECK(CreateUITemplate(*scene, canvas, UITemplate::Slider).HasComponent<UISliderComponent>());
	CHECK(CreateUITemplate(*scene, canvas, UITemplate::Dropdown).GetComponent<UIDropdownComponent>().Options.size() == 3);
	CHECK(CreateUITemplate(*scene, canvas, UITemplate::TextInput).GetComponent<UITextInputComponent>().Placeholder == "Type here");
	CHECK(CreateUITemplate(*scene, canvas, UITemplate::VerticalLayout).HasComponent<UILayoutComponent>());
	Entity list = CreateUITemplate(*scene, canvas, UITemplate::ScrollList);
	CHECK(list.HasComponent<UIScrollComponent>());
	CHECK(list.HasComponent<UILayoutComponent>());
	CHECK(CreateUITemplate(*scene, canvas, UITemplate::Text).HasComponent<TextComponent>());
	CHECK(CreateUITemplate(*scene, Entity(), UITemplate::Canvas).HasComponent<UICanvasComponent>());
}

TEST_CASE("UI templates: the Main Menu screen")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity menu = CreateUITemplate(*scene, Entity(), UITemplate::MainMenu);
	REQUIRE(menu.HasComponent<UICanvasComponent>());
	CHECK(menu.GetComponent<TagComponent>().Tag == "Main Menu");

	Entity column = Child(menu, "Buttons");
	REQUIRE(column);
	CHECK(column.HasComponent<UILayoutComponent>());
	const std::vector<Entity> buttons = column.GetChildren();
	REQUIRE(buttons.size() == 3);
	const char* names[] = { "Play", "Settings", "Quit" };
	for (size_t i = 0; i < buttons.size(); i++)
	{
		CHECK(buttons[i].GetComponent<TagComponent>().Tag == names[i]);
		CHECK(buttons[i].GetComponent<TextComponent>().Text == names[i]);
		CHECK(buttons[i].HasComponent<UIButtonComponent>());
	}
	REQUIRE(Child(menu, "Title"));
	REQUIRE(Child(menu, "Background"));
	REQUIRE(Child(menu, "Version"));

	// Laid out on a 1920 x 1080 screen: the background fills it, the title sits above the buttons,
	// the buttons stack evenly inside their column, and the version line is at the bottom right.
	scene->UpdateUILayout(glm::vec2(1920.0f, 1080.0f));
	const UIRectangle background = RectOf(*scene, Child(menu, "Background"));
	CHECK(background.Min == glm::vec2(0.0f));
	CHECK(background.Max == glm::vec2(1920.0f, 1080.0f));
	const UIRectangle title = RectOf(*scene, Child(menu, "Title"));
	const UIRectangle play = RectOf(*scene, buttons[0]);
	const UIRectangle quit = RectOf(*scene, buttons[2]);
	CHECK(title.Max.y <= play.Min.y);
	CHECK(play.Max.y <= RectOf(*scene, buttons[1]).Min.y);
	CHECK(quit.GetSize().x == doctest::Approx(RectOf(*scene, column).GetSize().x));
	CHECK((play.Min.x + play.Max.x) * 0.5f == doctest::Approx(960.0f));
	const UIRectangle version = RectOf(*scene, Child(menu, "Version"));
	CHECK(version.Max.x == doctest::Approx(1920.0f - 24.0f));
	CHECK(version.Max.y == doctest::Approx(1080.0f - 24.0f));
}

TEST_CASE("UI templates: the HUD screen")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity hud = CreateUITemplate(*scene, Entity(), UITemplate::HUD);
	REQUIRE(hud.HasComponent<UICanvasComponent>());
	Entity bar = Child(hud, "Health Bar");
	REQUIRE(bar);
	Entity fill = Child(bar, "Fill");
	REQUIRE(fill);
	for (Entity entity : { bar, fill, Child(hud, "Score"), Child(hud, "Message") })
	{
		REQUIRE(entity);
		CHECK_FALSE(entity.GetComponent<UIRectComponent>().Interactable);	// A HUD never takes the pointer
	}

	scene->UpdateUILayout(glm::vec2(1920.0f, 1080.0f));
	const UIRectangle barRect = RectOf(*scene, bar);
	CHECK(barRect.Min == glm::vec2(32.0f));
	// The fill's anchor max x is the amount left: 0.75 of the bar, inside its 3-pixel inset.
	const UIRectangle fillRect = RectOf(*scene, fill);
	CHECK(fillRect.Min.x == doctest::Approx(barRect.Min.x + 3.0f));
	CHECK(fillRect.Max.x == doctest::Approx(barRect.Min.x + 0.75f * barRect.GetSize().x - 3.0f));
	CHECK(RectOf(*scene, Child(hud, "Score")).Max.x == doctest::Approx(1920.0f - 32.0f));
	CHECK((RectOf(*scene, Child(hud, "Message")).Min.x + RectOf(*scene, Child(hud, "Message")).Max.x) * 0.5f == doctest::Approx(960.0f));
}

TEST_CASE("UI templates: anchor presets keep a laid-out rect in place, or move it")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity panel = CreateUITemplate(*scene, Entity(), UITemplate::Panel);
	scene->UpdateUILayout(glm::vec2(1920.0f, 1080.0f));
	const UIRectangle before = RectOf(*scene, panel);

	REQUIRE(ApplyUIAnchorPreset(panel, UIAnchorPreset::BottomRight, false));
	const UIRectComponent& rect = panel.GetComponent<UIRectComponent>();
	CHECK(rect.AnchorMin == glm::vec2(1.0f));
	scene->UpdateUILayout(glm::vec2(1920.0f, 1080.0f));
	CHECK(RectOf(*scene, panel).Min.x == doctest::Approx(before.Min.x));
	CHECK(RectOf(*scene, panel).Max.y == doctest::Approx(before.Max.y));

	REQUIRE(ApplyUIAnchorPreset(panel, UIAnchorPreset::Full, true));
	scene->UpdateUILayout(glm::vec2(1920.0f, 1080.0f));
	CHECK(RectOf(*scene, panel).Min == glm::vec2(0.0f));
	CHECK(RectOf(*scene, panel).Max == glm::vec2(1920.0f, 1080.0f));

	Entity plain = scene->CreateEntity("Not UI");
	CHECK_FALSE(ApplyUIAnchorPreset(plain, UIAnchorPreset::Center, false));
}
