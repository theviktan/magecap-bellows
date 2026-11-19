#include <doctest/doctest.h>

#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UIWidgets.h"

#include <functional>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	void CheckRect(const UIRectangle& rect, glm::vec2 min, glm::vec2 max)
	{
		CHECK(rect.Min.x == doctest::Approx(min.x));
		CHECK(rect.Min.y == doctest::Approx(min.y));
		CHECK(rect.Max.x == doctest::Approx(max.x));
		CHECK(rect.Max.y == doctest::Approx(max.y));
	}

}

TEST_CASE("UI canvas scale modes map reference pixels to the screen")
{
	const glm::vec2 reference(1920.0f, 1080.0f);
	// A 4:3 screen, taller than 16:9 for its width.
	const glm::vec2 screen(1024.0f, 768.0f);
	CHECK(ComputeUIScale(reference, UIScaleMode::FitHeight, screen) == doctest::Approx(768.0f / 1080.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::FitWidth, screen) == doctest::Approx(1024.0f / 1920.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::Expand, screen) == doctest::Approx(1024.0f / 1920.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::Expand, glm::vec2(3840.0f, 1080.0f)) == doctest::Approx(1.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::ConstantPixels, screen) == doctest::Approx(1.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::FitHeight, glm::vec2(3840.0f, 2160.0f)) == doctest::Approx(2.0f));

	// Degenerate sizes never divide by zero.
	CHECK(ComputeUIScale(glm::vec2(0.0f), UIScaleMode::FitHeight, screen) == doctest::Approx(1.0f));
	CHECK(ComputeUIScale(reference, UIScaleMode::Expand, glm::vec2(0.0f)) == doctest::Approx(1.0f));

	for (UIScaleMode mode : { UIScaleMode::FitHeight, UIScaleMode::FitWidth, UIScaleMode::Expand, UIScaleMode::ConstantPixels })
	{
		CHECK(UIScaleModeFromString(UIScaleModeToString(mode)) == mode);
		CHECK(IsUIScaleModeName(UIScaleModeToString(mode)));
	}
	CHECK(UIScaleModeFromString("sideways") == UIScaleMode::Expand);
	CHECK_FALSE(IsUIScaleModeName("FitHeight"));
}

TEST_CASE("UI rects follow their anchors and offsets")
{
	UIRectangle screen;
	screen.Max = glm::vec2(800.0f, 600.0f);

	SUBCASE("A centered fixed-size rect")
	{
		const UIRectangle rect = ResolveUIRect(glm::vec2(0.5f), glm::vec2(0.5f), glm::vec2(-100.0f, -25.0f), glm::vec2(100.0f, 25.0f), screen, 1.0f);
		CheckRect(rect, { 300.0f, 275.0f }, { 500.0f, 325.0f });
	}
	SUBCASE("Pinned to the bottom-right corner, scaled")
	{
		const UIRectangle rect = ResolveUIRect(glm::vec2(1.0f), glm::vec2(1.0f), glm::vec2(-120.0f, -60.0f), glm::vec2(-20.0f, -10.0f), screen, 0.5f);
		CheckRect(rect, { 740.0f, 570.0f }, { 790.0f, 595.0f });
	}
	SUBCASE("Stretched across the top with a margin")
	{
		const UIRectangle rect = ResolveUIRect(glm::vec2(0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(10.0f, 10.0f), glm::vec2(-10.0f, 60.0f), screen, 1.0f);
		CheckRect(rect, { 10.0f, 10.0f }, { 790.0f, 60.0f });
	}
	SUBCASE("Nested inside a parent that does not start at the origin")
	{
		UIRectangle parent;
		parent.Min = glm::vec2(100.0f, 50.0f);
		parent.Max = glm::vec2(300.0f, 150.0f);
		const UIRectangle rect = ResolveUIRect(glm::vec2(0.0f), glm::vec2(1.0f), glm::vec2(5.0f), glm::vec2(-5.0f), parent, 2.0f);
		CheckRect(rect, { 110.0f, 60.0f }, { 290.0f, 140.0f });
	}
	SUBCASE("Crossed offsets give an empty rect, not an inverted one")
	{
		const UIRectangle rect = ResolveUIRect(glm::vec2(0.5f), glm::vec2(0.5f), glm::vec2(50.0f), glm::vec2(-50.0f), screen, 1.0f);
		CHECK(rect.GetSize() == glm::vec2(0.0f));
	}
}

TEST_CASE("UI rect rectangles intersect and test points")
{
	UIRectangle a { glm::vec2(0.0f), glm::vec2(100.0f, 50.0f) };
	UIRectangle b { glm::vec2(80.0f, 20.0f), glm::vec2(200.0f, 200.0f) };
	CheckRect(a.Intersect(b), { 80.0f, 20.0f }, { 100.0f, 50.0f });
	UIRectangle far { glm::vec2(500.0f), glm::vec2(600.0f) };
	CHECK(a.Intersect(far).GetSize() == glm::vec2(0.0f));
	CHECK(a.Contains(glm::vec2(0.0f)));
	CHECK(a.Contains(glm::vec2(99.5f, 49.5f)));
	CHECK_FALSE(a.Contains(glm::vec2(100.0f, 10.0f)));	// Max edges belong to the neighbor
	CHECK_FALSE(a.Contains(glm::vec2(-0.1f, 10.0f)));
}

TEST_CASE("UI rect position and size keep the pivot in place")
{
	glm::vec2 offsetMin(-100.0f, -25.0f), offsetMax(100.0f, 25.0f);
	const glm::vec2 centered(0.5f);
	CHECK(GetUIRectPosition(centered, offsetMin, offsetMax) == glm::vec2(0.0f));
	CHECK(GetUIRectSize(offsetMin, offsetMax) == glm::vec2(200.0f, 50.0f));

	SetUIRectSize(centered, offsetMin, offsetMax, glm::vec2(300.0f, 100.0f));
	CHECK(offsetMin == glm::vec2(-150.0f, -50.0f));
	CHECK(offsetMax == glm::vec2(150.0f, 50.0f));

	SetUIRectPosition(centered, offsetMin, offsetMax, glm::vec2(10.0f, -20.0f));
	CHECK(offsetMin == glm::vec2(-140.0f, -70.0f));
	CHECK(offsetMax == glm::vec2(160.0f, 30.0f));
	CHECK(GetUIRectPosition(centered, offsetMin, offsetMax) == glm::vec2(10.0f, -20.0f));

	// A top-left pivot grows the rect to the right and down.
	const glm::vec2 topLeft(0.0f);
	glm::vec2 cornerMin(20.0f, 20.0f), cornerMax(120.0f, 70.0f);
	SetUIRectSize(topLeft, cornerMin, cornerMax, glm::vec2(50.0f, 10.0f));
	CHECK(cornerMin == glm::vec2(20.0f, 20.0f));
	CHECK(cornerMax == glm::vec2(70.0f, 30.0f));
	CHECK(GetUIRectPosition(topLeft, cornerMin, cornerMax) == glm::vec2(20.0f, 20.0f));
}

TEST_CASE("UI layout containers place their children")
{
	const UIRectangle area { glm::vec2(100.0f, 50.0f), glm::vec2(500.0f, 350.0f) };	// 400 by 300
	const auto item = [](float w, float h, UISizing width = UISizing::Fixed, UISizing height = UISizing::Fixed)
	{
		return UILayoutItem { glm::vec2(w, h), width, height };
	};

	SUBCASE("Vertical: one under another, with padding and spacing")
	{
		UILayoutSettings settings;
		settings.Spacing = 10.0f;
		settings.Padding = glm::vec4(20.0f, 30.0f, 20.0f, 30.0f);
		glm::vec2 content;
		const auto rects = ArrangeUILayout(settings, area, { item(100, 40), item(200, 60) }, 1.0f, &content);
		CheckRect(rects[0], { 120.0f, 80.0f }, { 220.0f, 120.0f });
		CheckRect(rects[1], { 120.0f, 130.0f }, { 320.0f, 190.0f });
		CHECK(content == glm::vec2(240.0f, 170.0f));	// 200 wide + padding; 40 + 10 + 60 high + padding
	}
	SUBCASE("Vertical: fill shares what is left, and stretches across")
	{
		UILayoutSettings settings;
		settings.Spacing = 10.0f;
		const auto rects = ArrangeUILayout(settings, area, { item(100, 40), item(0, 0, UISizing::Fill, UISizing::Fill), item(0, 50, UISizing::Fill) }, 1.0f);
		CheckRect(rects[0], { 100.0f, 50.0f }, { 200.0f, 90.0f });
		CheckRect(rects[1], { 100.0f, 100.0f }, { 500.0f, 290.0f });	// 300 - 40 - 50 - 2 * 10 = 190
		CheckRect(rects[2], { 100.0f, 300.0f }, { 500.0f, 350.0f });
	}
	SUBCASE("Horizontal: centered as a group, items centered across, at twice the scale")
	{
		UILayoutSettings settings;
		settings.Direction = UILayoutDirection::Horizontal;
		settings.Spacing = 10.0f;
		settings.MainAlignment = UIAlignment::Center;
		settings.CrossAlignment = UIAlignment::Center;
		const auto rects = ArrangeUILayout(settings, area, { item(50, 20), item(40, 50) }, 2.0f);
		// 100 + 20 + 80 = 200 wide of 400, so it starts 100 in; heights 40 and 100 of 300.
		CheckRect(rects[0], { 200.0f, 180.0f }, { 300.0f, 220.0f });
		CheckRect(rects[1], { 320.0f, 150.0f }, { 400.0f, 250.0f });
	}
	SUBCASE("Horizontal: aligned to the end")
	{
		UILayoutSettings settings;
		settings.Direction = UILayoutDirection::Horizontal;
		settings.MainAlignment = UIAlignment::End;
		settings.CrossAlignment = UIAlignment::End;
		const auto rects = ArrangeUILayout(settings, area, { item(50, 20) }, 1.0f);
		CheckRect(rects[0], { 450.0f, 330.0f }, { 500.0f, 350.0f });
	}
	SUBCASE("Grid: equal columns, rows as tall as their tallest child")
	{
		UILayoutSettings settings;
		settings.Direction = UILayoutDirection::Grid;
		settings.Columns = 3;
		settings.Spacing = 20.0f;
		glm::vec2 content;
		const auto rects = ArrangeUILayout(settings, area,
			{ item(0, 40, UISizing::Fill), item(50, 60), item(0, 30, UISizing::Fill, UISizing::Fill), item(0, 50, UISizing::Fill) }, 1.0f, &content);
		// (400 - 2 * 20) / 3 = 120 per column; row 0 is 60 high, row 1 is 50.
		CheckRect(rects[0], { 100.0f, 50.0f }, { 220.0f, 90.0f });
		CheckRect(rects[1], { 240.0f, 50.0f }, { 290.0f, 110.0f });
		CheckRect(rects[2], { 380.0f, 50.0f }, { 500.0f, 110.0f });	// Fills its row's height
		CheckRect(rects[3], { 100.0f, 130.0f }, { 220.0f, 180.0f });
		CHECK(content.y == doctest::Approx(130.0f));
	}
	SUBCASE("Nothing to place, or no room")
	{
		CHECK(ArrangeUILayout(UILayoutSettings(), area, {}, 1.0f).empty());
		const UIRectangle tiny { glm::vec2(0.0f), glm::vec2(10.0f) };
		UILayoutSettings settings;
		settings.Padding = glm::vec4(20.0f);
		const auto rects = ArrangeUILayout(settings, tiny, { item(0, 0, UISizing::Fill, UISizing::Fill) }, 1.0f);
		CHECK(rects[0].GetSize() == glm::vec2(0.0f));
	}
	for (UILayoutDirection direction : { UILayoutDirection::Vertical, UILayoutDirection::Horizontal, UILayoutDirection::Grid })
		CHECK(UILayoutDirectionFromString(UILayoutDirectionToString(direction)) == direction);
	for (UIAlignment alignment : { UIAlignment::Start, UIAlignment::Center, UIAlignment::End })
		CHECK(UIAlignmentFromString(UIAlignmentToString(alignment)) == alignment);
	CHECK(UISizingFromString("fill") == UISizing::Fill);
	CHECK_FALSE(IsUISizingName("stretch"));
}

TEST_CASE("UI widgets place their parts and snap slider values")
{
	CHECK(SnapUISliderValue(0.37f, 0.0f, 1.0f, 0.25f) == doctest::Approx(0.25f));
	CHECK(SnapUISliderValue(0.38f, 0.0f, 1.0f, 0.25f) == doctest::Approx(0.5f));
	CHECK(SnapUISliderValue(7.0f, 2.0f, 12.0f, 3.0f) == doctest::Approx(8.0f));	// Steps count from min
	CHECK(SnapUISliderValue(99.0f, 0.0f, 10.0f, 0.0f) == doctest::Approx(10.0f));
	CHECK(SnapUISliderValue(5.0f, 10.0f, 0.0f, 0.0f) == doctest::Approx(5.0f));	// Min and max either way round
	CHECK(SnapUISliderValue(std::nanf(""), 0.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));
	CHECK(UISliderKeyStep(0.0f, 100.0f, 0.0f) == doctest::Approx(5.0f));
	CHECK(UISliderKeyStep(0.0f, 100.0f, 2.0f) == doctest::Approx(2.0f));

	// A 220 by 40 slider: a 10 pixel knob keeps the track 10 in from each end.
	const UIRectangle slider { glm::vec2(100.0f, 50.0f), glm::vec2(320.0f, 90.0f) };
	const UISliderGeometry geometry = ComputeUISliderGeometry(slider, 0.5f, 0.0f, 1.0f, 1.0f);
	CHECK(geometry.Track.Min.x == doctest::Approx(110.0f));
	CHECK(geometry.Track.Max.x == doctest::Approx(310.0f));
	CHECK(geometry.Knob == glm::vec2(210.0f, 70.0f));
	CHECK(UISliderValueAt(slider, 210.0f, 0.0f, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.5f));
	CHECK(UISliderValueAt(slider, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
	CHECK(UISliderValueAt(slider, 500.0f, 0.0f, 10.0f, 1.0f, 1.0f) == doctest::Approx(10.0f));
	CHECK(UISliderValueAt(slider, 175.0f, 0.0f, 10.0f, 1.0f, 1.0f) == doctest::Approx(3.0f));	// 3.25 snaps to 3

	const UIRectangle toggle { glm::vec2(0.0f, 0.0f), glm::vec2(300.0f, 40.0f) };
	const UIRectangle box = ComputeUIToggleBox(toggle, 1.0f);
	CHECK(box.GetSize() == glm::vec2(28.0f));
	CHECK(box.Min == glm::vec2(0.0f, 6.0f));
	CHECK(ComputeUIToggleLabelArea(toggle, 1.0f).Min.x == doctest::Approx(40.0f));

	// A dropdown near the bottom opens upward; one with room below opens downward.
	const glm::vec2 screen(800.0f, 600.0f);
	const UIRectangle low { glm::vec2(100.0f, 500.0f), glm::vec2(300.0f, 540.0f) };
	const UIRectangle list = ComputeUIDropdownList(low, 3, screen);
	CHECK(list.Min == glm::vec2(100.0f, 380.0f));
	CHECK(list.Max == glm::vec2(300.0f, 500.0f));
	CHECK(UIDropdownRowAt(list, 3, glm::vec2(150.0f, 385.0f)) == 0);
	CHECK(UIDropdownRowAt(list, 3, glm::vec2(150.0f, 499.0f)) == 2);
	CHECK(UIDropdownRowAt(list, 3, glm::vec2(350.0f, 450.0f)) == -1);
	const UIRectangle high { glm::vec2(100.0f, 50.0f), glm::vec2(300.0f, 90.0f) };
	CHECK(ComputeUIDropdownList(high, 3, screen).Min.y == doctest::Approx(90.0f));
}

TEST_CASE("UI navigation moves to the nearest rect in the pressed direction")
{
	// A 2 by 2 grid of buttons and a wide one under it:
	//   [0] [1]
	//   [2] [3]
	//   [   4   ]
	const auto box = [](float x, float y, float w, float h) { return UIRectangle { glm::vec2(x, y), glm::vec2(x + w, y + h) }; };
	const std::vector<UIRectangle> rects { box(0, 0, 100, 40), box(120, 0, 100, 40), box(0, 60, 100, 40), box(120, 60, 100, 40), box(0, 120, 220, 40) };
	CHECK(FindUINavigationTarget(rects[0], rects, UINavigationDirection::Right, 0) == 1);
	CHECK(FindUINavigationTarget(rects[0], rects, UINavigationDirection::Down, 0) == 2);
	CHECK(FindUINavigationTarget(rects[3], rects, UINavigationDirection::Left, 3) == 2);
	CHECK(FindUINavigationTarget(rects[3], rects, UINavigationDirection::Up, 3) == 1);
	CHECK(FindUINavigationTarget(rects[3], rects, UINavigationDirection::Down, 3) == 4);
	CHECK(FindUINavigationTarget(rects[4], rects, UINavigationDirection::Up, 4) == 2);	// Equal distance: the first
	// Nothing further that way: no move (no wrapping).
	CHECK(FindUINavigationTarget(rects[1], rects, UINavigationDirection::Right, 1) == -1);
	CHECK(FindUINavigationTarget(rects[0], rects, UINavigationDirection::Up, 0) == -1);
	CHECK(FindUINavigationTarget(rects[0], {}, UINavigationDirection::Down) == -1);
}

TEST_CASE("Scenes lay out their UI canvases in hierarchy and sort order")
{
	Ref<Scene> scene = CreateRef<Scene>("UI");
	Entity world = scene->CreateEntity("World");

	// A canvas under a 3D entity still covers the screen.
	Entity hud = scene->CreateEntity("HUD");
	scene->SetParent(hud, world);
	auto& hudCanvas = hud.AddComponent<UICanvasComponent>();
	hudCanvas.ReferenceSize = glm::vec2(400.0f, 300.0f);
	hudCanvas.ScaleMode = UIScaleMode::FitHeight;

	Entity panel = scene->CreateEntity("Panel");
	scene->SetParent(panel, hud);
	auto& panelRect = panel.AddComponent<UIRectComponent>();
	panelRect.AnchorMin = glm::vec2(0.0f);
	panelRect.AnchorMax = glm::vec2(0.0f);
	panelRect.OffsetMin = glm::vec2(10.0f);
	panelRect.OffsetMax = glm::vec2(110.0f, 60.0f);
	panel.AddComponent<UIImageComponent>();

	// A group without a rect passes its parent's rectangle through to its children.
	Entity group = scene->CreateEntity("Group");
	scene->SetParent(group, panel);
	Entity label = scene->CreateEntity("Label");
	scene->SetParent(label, group);
	auto& labelRect = label.AddComponent<UIRectComponent>();
	labelRect.AnchorMin = glm::vec2(0.0f);
	labelRect.AnchorMax = glm::vec2(1.0f);
	labelRect.OffsetMin = glm::vec2(5.0f);
	labelRect.OffsetMax = glm::vec2(-5.0f);
	label.AddComponent<TextComponent>().Text = "Score";

	// A second canvas, drawn first because of its lower sort order.
	Entity background = scene->CreateEntity("Background");
	background.AddComponent<UICanvasComponent>().SortOrder = -1;
	background.AddComponent<UIImageComponent>();

	scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));	// FitHeight: scale 2

	const UIRectangle& panelOnScreen = panel.GetComponent<UIRectComponent>().RuntimeRect;
	CHECK(panelOnScreen.Min == glm::vec2(20.0f, 20.0f));
	CHECK(panelOnScreen.Max == glm::vec2(220.0f, 120.0f));
	const UIRectangle& labelOnScreen = label.GetComponent<UIRectComponent>().RuntimeRect;
	CHECK(labelOnScreen.Min == glm::vec2(30.0f, 30.0f));
	CHECK(labelOnScreen.Max == glm::vec2(210.0f, 110.0f));

	CHECK(scene->IsUIEntity(hud));
	CHECK(scene->IsUIEntity(group));
	CHECK(scene->IsUIEntity(label));
	CHECK_FALSE(scene->IsUIEntity(world));
	CHECK(scene->IsInsideUICanvas(label));
	CHECK_FALSE(scene->IsInsideUICanvas(world));

	std::vector<Entity> order = scene->GetVisibleUIEntities();
	REQUIRE(order.size() == 5);
	CHECK(order[0] == background);
	CHECK(order[1] == hud);
	CHECK(order[2] == panel);
	CHECK(order[3] == group);
	CHECK(order[4] == label);

	SUBCASE("A hidden rect hides everything under it, but is still laid out")
	{
		panel.GetComponent<UIRectComponent>().Visible = false;
		scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));
		CHECK(scene->GetVisibleUIEntities().size() == 2);
		CHECK(scene->IsUIEntity(label));
		CHECK(label.GetComponent<UIRectComponent>().RuntimeRect.Min == glm::vec2(30.0f, 30.0f));
	}
	SUBCASE("A hidden canvas hides its whole UI")
	{
		hud.GetComponent<UICanvasComponent>().Visible = false;
		scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));
		order = scene->GetVisibleUIEntities();
		REQUIRE(order.size() == 1);
		CHECK(order[0] == background);
	}
	SUBCASE("Sort order decides which canvas is on top")
	{
		background.GetComponent<UICanvasComponent>().SortOrder = 5;
		scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));
		CHECK(scene->GetVisibleUIEntities().back() == background);
	}
	SUBCASE("A canvas inside a canvas starts a UI of its own")
	{
		Entity popup = scene->CreateEntity("Popup");
		scene->SetParent(popup, panel);
		popup.AddComponent<UICanvasComponent>().SortOrder = 10;
		Entity button = scene->CreateEntity("Button");
		scene->SetParent(button, popup);
		auto& buttonRect = button.AddComponent<UIRectComponent>();
		buttonRect.AnchorMin = buttonRect.AnchorMax = glm::vec2(0.0f);
		buttonRect.OffsetMin = glm::vec2(0.0f);
		buttonRect.OffsetMax = glm::vec2(50.0f);
		scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));
		// Its rects start from the screen, not from the panel, at its own scale (Expand on 1920 by 1080).
		CHECK(button.GetComponent<UIRectComponent>().RuntimeRect.Min == glm::vec2(0.0f));
		CHECK(button.GetComponent<UIRectComponent>().RuntimeRect.Max.x == doctest::Approx(50.0f * 800.0f / 1920.0f));
		CHECK(scene->GetVisibleUIEntities().back() == button);
	}
	SUBCASE("World text stays world text outside a canvas")
	{
		Entity sign = scene->CreateEntity("Sign");
		sign.AddComponent<TextComponent>();
		scene->UpdateUILayout(glm::vec2(800.0f, 600.0f));
		CHECK_FALSE(scene->IsUIEntity(sign));
	}
}

namespace {

	// A running scene with a menu canvas: a background panel and two focusable buttons stacked in it,
	//   Panel   (100, 100)-(500, 400)
	//   Play    (150, 150)-(450, 200)
	//   Quit    (150, 250)-(450, 300)
	// on an 800 by 600 screen at one reference pixel per screen pixel. UI events are recorded.
	struct UIInputScene
	{
		struct Received
		{
			std::string Name;
			UUID Source = 0;
			UUID Target = 0;
			Scripting::ScriptValueList Values;
		};

		Ref<Scene> SceneRef = CreateRef<Scene>("UI input");
		Entity Canvas, Panel, Play, Quit;
		std::vector<Received> Events;
		Scripting::ScriptEventListenerID Listener = 0;

		UIInputScene()
		{
			Input::ResetState();
			Input::BeginFrame();
			Input::SetViewportRect(glm::vec2(0.0f), glm::vec2(800.0f, 600.0f));
			InputMap::SetActions(InputMap::GetDefaultActions());

			Canvas = SceneRef->CreateEntity("Menu");
			Canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;
			Panel = AddRect("Panel", Canvas, { 100.0f, 100.0f }, { 500.0f, 400.0f });
			Panel.AddComponent<UIImageComponent>();
			Play = AddRect("Play", Panel, { 50.0f, 50.0f }, { 350.0f, 100.0f });
			Play.GetComponent<UIRectComponent>().Focusable = true;
			Quit = AddRect("Quit", Panel, { 50.0f, 150.0f }, { 350.0f, 200.0f });
			Quit.GetComponent<UIRectComponent>().Focusable = true;

			SceneRef->OnViewportResize(800, 600);
			SceneRef->OnRuntimeStart();
			Listener = Scripting::AddScriptEventListener([this](const std::string& name, const Scripting::ScriptValueList& values,
				const Scripting::ScriptEventDelivery& delivery)
			{
				Events.push_back({ name, delivery.Source ? delivery.Source->ID : UUID(0), delivery.Target ? delivery.Target->ID : UUID(0), values });
			});
		}

		~UIInputScene()
		{
			Scripting::RemoveScriptEventListener(Listener);
			SceneRef->OnRuntimeStop();
			Input::ResetState();
			Input::BeginFrame();
			InputMap::SetActions({});
		}

		Entity AddRect(const char* name, Entity parent, glm::vec2 min, glm::vec2 max)
		{
			Entity entity = SceneRef->CreateEntity(name);
			SceneRef->SetParent(entity, parent);
			auto& rect = entity.AddComponent<UIRectComponent>();
			rect.AnchorMin = rect.AnchorMax = glm::vec2(0.0f);
			rect.OffsetMin = min;
			rect.OffsetMax = max;
			return entity;
		}

		// One frame: the given input changes, then the scene's update (the UI runs first in it).
		void Frame(const std::function<void()>& input = {})
		{
			Input::BeginFrame();
			if (input)
				input();
			SceneRef->OnUpdateRuntime(Timestep(1.0f / 60.0f));
		}

		void Click(glm::vec2 position)
		{
			Frame([&] { Input::InjectMousePosition(position); Input::InjectMouseButton(MouseButton::Left, true); });
			Frame([&] { Input::InjectMouseButton(MouseButton::Left, false); });
		}

		void Tap(KeyCode key)
		{
			Frame([&] { Input::InjectKey(key, true); });
			Frame([&] { Input::InjectKey(key, false); });
		}

		// The first value of each 'changed' raised by 'source' (delivered to it), checking each ends with it.
		Scripting::ScriptValueList ChangesFrom(Entity source) const
		{
			Scripting::ScriptValueList changes;
			for (const Received& event : Events)
			{
				if (event.Name != "changed" || event.Source != source.GetUUID() || event.Target != source.GetUUID())
					continue;
				REQUIRE(event.Values.size() == 2);
				CHECK(event.Values[1] == Scripting::ScriptValue(Scripting::ScriptEntity(source)));
				changes.push_back(event.Values[0]);
			}
			return changes;
		}

		// The names of the recorded events raised by 'source', delivered to that entity itself.
		std::vector<std::string> EventsFrom(Entity source) const
		{
			std::vector<std::string> names;
			for (const Received& event : Events)
			{
				if (event.Source == source.GetUUID() && event.Target == source.GetUUID())
					names.push_back(event.Name);
			}
			return names;
		}
	};

	using Names = std::vector<std::string>;

}

TEST_CASE("A game with no UI sees exactly the input it always did")
{
	UIInputScene ui;
	ui.Canvas.GetComponent<UICanvasComponent>().Visible = false;	// No UI on screen at all
	ui.Frame([] {
		Input::InjectMousePosition(glm::vec2(300.0f, 175.0f));
		Input::InjectKey(KeyCode::W, true);
		Input::InjectKey(KeyCode::Enter, true);
		Input::InjectMouseButton(MouseButton::Left, true);
		Input::InjectGamepadButton(GamepadButton::A, true);
	});
	CHECK(Input::GetCapture() == InputCapture());
	CHECK(Input::IsKeyPressed(KeyCode::W));
	CHECK(Input::IsKeyDown(KeyCode::Enter));
	CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
	CHECK(Input::IsGamepadButtonPressed(GamepadButton::A));
	CHECK(InputMap::IsActionPressed("jump"));
	CHECK(InputMap::IsActionPressed("ui_accept"));
	CHECK_FALSE(ui.SceneRef->UIWantsPointer());
	CHECK_FALSE(ui.SceneRef->UIWantsKeyboard());
	ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, false); });
	CHECK(Input::IsMouseButtonReleased(MouseButton::Left));
	CHECK(ui.Events.empty());
	CHECK_FALSE(ui.SceneRef->GetUIFocused());
}

TEST_CASE("The pointer clicks UI, focuses it, and the game does not see it")
{
	UIInputScene ui;

	SUBCASE("Over a button: hovered, and the press is the UI's")
	{
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 175.0f)); });
		CHECK(ui.SceneRef->GetUIHovered() == ui.Play);
		ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK(ui.SceneRef->UIWantsPointer());
		CHECK_FALSE(Input::IsMouseButtonPressed(MouseButton::Left));
		CHECK_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
		CHECK(Input::IsMouseButtonPressed(MouseButton::Left, InputAccess::Raw));
		CHECK(ui.SceneRef->GetUIPressed() == ui.Play);
		CHECK(ui.SceneRef->GetUIFocused() == ui.Play);
		ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, false); });
		CHECK_FALSE(Input::IsMouseButtonReleased(MouseButton::Left));
		CHECK_FALSE(ui.SceneRef->GetUIPressed());
		CHECK(ui.EventsFrom(ui.Play) == Names { "focused", "clicked" });

		// The click climbs from the button through its parents, in that order.
		std::vector<UUID> targets;
		for (const auto& event : ui.Events)
		{
			if (event.Name == "clicked")
				targets.push_back(event.Target);
		}
		CHECK(targets == std::vector<UUID> { ui.Play.GetUUID(), ui.Panel.GetUUID(), ui.Canvas.GetUUID() });
	}
	SUBCASE("A panel stops the pointer but cannot take focus; text and bare rects let it through")
	{
		ui.Click(glm::vec2(300.0f, 350.0f));	// On the panel, below the buttons
		CHECK(ui.EventsFrom(ui.Panel) == Names { "clicked" });
		CHECK_FALSE(ui.SceneRef->GetUIFocused());

		ui.Panel.RemoveComponent<UIImageComponent>();
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 350.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
		CHECK_FALSE(ui.SceneRef->GetUIHovered());
	}
	SUBCASE("Pressing on one thing and letting go on another clicks nothing")
	{
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 175.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 275.0f)); Input::InjectMouseButton(MouseButton::Left, false); });
		CHECK(ui.EventsFrom(ui.Play) == Names { "focused" });
		CHECK(ui.EventsFrom(ui.Quit).empty());
	}
	SUBCASE("A press that starts in the game stays the game's over the UI")
	{
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(700.0f, 500.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 175.0f)); });
		CHECK(Input::IsMouseButtonDown(MouseButton::Left));
		ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, false); });
		CHECK(Input::IsMouseButtonReleased(MouseButton::Left));
		CHECK(ui.Events.empty());
	}
	SUBCASE("Clicking the game clears focus")
	{
		ui.Click(glm::vec2(300.0f, 175.0f));
		ui.Click(glm::vec2(700.0f, 500.0f));
		CHECK(ui.EventsFrom(ui.Play) == Names { "focused", "clicked", "unfocused" });
	}
	SUBCASE("A rect that is not interactable lets the pointer through, with everything under it")
	{
		ui.Panel.GetComponent<UIRectComponent>().Interactable = false;
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 175.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK_FALSE(ui.SceneRef->GetUIHovered());
		CHECK(Input::IsMouseButtonPressed(MouseButton::Left));
		CHECK_FALSE(ui.SceneRef->SetUIFocus(ui.Play));
	}
}

TEST_CASE("Keyboard and gamepad move focus, accept and cancel")
{
	UIInputScene ui;
	// Without focus the arrow keys are the game's.
	ui.Tap(KeyCode::Down);
	CHECK_FALSE(ui.SceneRef->GetUIFocused());

	REQUIRE(ui.SceneRef->SetUIFocus(ui.Play));
	ui.Tap(KeyCode::Down);
	CHECK(ui.SceneRef->GetUIFocused() == ui.Quit);
	ui.Tap(KeyCode::Down);	// Nothing further down: focus stays
	CHECK(ui.SceneRef->GetUIFocused() == ui.Quit);
	ui.Frame([] { Input::InjectGamepadButton(GamepadButton::DPadUp, true); });
	ui.Frame([] { Input::InjectGamepadButton(GamepadButton::DPadUp, false); });
	CHECK(ui.SceneRef->GetUIFocused() == ui.Play);

	// Tab goes round in hierarchy order; Shift+Tab goes back.
	ui.Tap(KeyCode::Tab);
	CHECK(ui.SceneRef->GetUIFocused() == ui.Quit);
	ui.Tap(KeyCode::Tab);
	CHECK(ui.SceneRef->GetUIFocused() == ui.Play);
	ui.Frame([] { Input::InjectKey(KeyCode::LeftShift, true); Input::InjectKey(KeyCode::Tab, true); });
	ui.Frame([] { Input::InjectKey(KeyCode::LeftShift, false); Input::InjectKey(KeyCode::Tab, false); });
	CHECK(ui.SceneRef->GetUIFocused() == ui.Quit);

	ui.Tap(KeyCode::Enter);
	ui.Tap(KeyCode::Escape);
	const Names quit = ui.EventsFrom(ui.Quit);
	REQUIRE(quit.size() >= 3);
	CHECK(Names(quit.end() - 2, quit.end()) == Names { "clicked", "cancelled" });

	// Hiding the focused UI lets focus go.
	ui.Quit.GetComponent<UIRectComponent>().Visible = false;
	ui.Frame();
	CHECK_FALSE(ui.SceneRef->GetUIFocused());
	CHECK(ui.EventsFrom(ui.Quit).back() == "unfocused");

	// Only focusable, shown UI can take focus.
	CHECK_FALSE(ui.SceneRef->SetUIFocus(ui.Panel));
	CHECK_FALSE(ui.SceneRef->SetUIFocus(ui.Quit));
	CHECK(ui.SceneRef->SetUIFocus(Entity()));
}

TEST_CASE("A modal canvas takes every input and keeps focus inside it")
{
	UIInputScene ui;
	REQUIRE(ui.SceneRef->SetUIFocus(ui.Play));

	Entity dialog = ui.SceneRef->CreateEntity("Dialog");
	auto& dialogCanvas = dialog.AddComponent<UICanvasComponent>();
	dialogCanvas.ScaleMode = UIScaleMode::ConstantPixels;
	dialogCanvas.Modal = true;
	Entity yes = ui.AddRect("Yes", dialog, { 600.0f, 500.0f }, { 700.0f, 550.0f });
	yes.GetComponent<UIRectComponent>().Focusable = true;
	yes.AddComponent<UIImageComponent>();

	ui.Frame([] { Input::InjectKey(KeyCode::W, true); Input::InjectGamepadButton(GamepadButton::X, true); });
	// The menu behind the dialog loses focus, and the game gets no keys or buttons.
	CHECK_FALSE(ui.SceneRef->GetUIFocused());
	CHECK(ui.EventsFrom(ui.Play).back() == "unfocused");
	CHECK(ui.SceneRef->UIWantsKeyboard());
	CHECK_FALSE(Input::IsKeyPressed(KeyCode::W));
	CHECK(Input::IsKeyPressed(KeyCode::W, InputAccess::Raw));
	CHECK_FALSE(Input::IsGamepadButtonDown(GamepadButton::X));
	CHECK_FALSE(InputMap::IsActionPressed("move_forward"));
	ui.Frame([] { Input::InjectKey(KeyCode::W, false); Input::InjectGamepadButton(GamepadButton::X, false); });

	// Clicking the menu behind does nothing; the pointer is the dialog's everywhere.
	ui.Click(glm::vec2(300.0f, 175.0f));
	CHECK(ui.EventsFrom(ui.Play) == Names { "focused", "unfocused" });
	CHECK_FALSE(Input::IsMouseButtonReleased(MouseButton::Left));

	// Navigation starts inside the dialog; cancel with nothing focused comes from the dialog itself.
	ui.Tap(KeyCode::Escape);
	CHECK(ui.EventsFrom(dialog) == Names { "cancelled" });
	ui.Tap(KeyCode::Down);
	CHECK(ui.SceneRef->GetUIFocused() == yes);
	CHECK_FALSE(ui.SceneRef->SetUIFocus(ui.Quit));

	// Closing the dialog gives the input back.
	dialogCanvas.Visible = false;
	ui.Frame([] { Input::InjectKey(KeyCode::W, true); });
	CHECK_FALSE(ui.SceneRef->UIWantsKeyboard());
	CHECK(Input::IsKeyPressed(KeyCode::W));
	CHECK_FALSE(ui.SceneRef->GetUIFocused());
}

TEST_CASE("Stopping play gives every input back to the game")
{
	{
		UIInputScene ui;
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 175.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK(Input::GetCapture().Pointer);
	}
	CHECK(Input::GetCapture() == InputCapture());
}

TEST_CASE("Layouts and scroll areas place, clip and scroll a scene's UI")
{
	UIInputScene ui;
	// The fixture's buttons would sit among the list's rows; only the rows take focus here.
	ui.Play.GetComponent<UIRectComponent>().Focusable = false;
	ui.Quit.GetComponent<UIRectComponent>().Focusable = false;
	// A scrolling list in the panel: 6 rows of 60 under 10 of padding and spacing, in 300 of height.
	Entity list = ui.AddRect("List", ui.Panel, { 0.0f, 0.0f }, { 400.0f, 300.0f });
	auto& layout = list.AddComponent<UILayoutComponent>();
	layout.Spacing = 10.0f;
	layout.Padding = glm::vec4(10.0f);
	auto& scroll = list.AddComponent<UIScrollComponent>();
	std::vector<Entity> rows;
	for (int i = 0; i < 6; i++)
	{
		Entity row = ui.AddRect(("Row " + std::to_string(i)).c_str(), list, { 0.0f, 0.0f }, { 0.0f, 60.0f });
		auto& rect = row.GetComponent<UIRectComponent>();
		rect.WidthSizing = UISizing::Fill;
		rect.Focusable = true;
		rows.push_back(row);
	}
	ui.Frame();

	// The layout ignores the rows' own anchors and offsets: full width minus padding, stacked.
	const auto rectOf = [](Entity e) { return e.GetComponent<UIRectComponent>().RuntimeRect; };
	CHECK(rectOf(rows[0]).Min == glm::vec2(110.0f, 110.0f));
	CHECK(rectOf(rows[0]).Max == glm::vec2(490.0f, 170.0f));
	CHECK(rectOf(rows[1]).Min.y == doctest::Approx(180.0f));
	// 6 * 60 + 5 * 10 + 2 * 10 = 430 of content in 300.
	CHECK(scroll.RuntimeContentSize.y == doctest::Approx(430.0f));
	CHECK(scroll.RuntimeViewportSize.y == doctest::Approx(300.0f));

	SUBCASE("The wheel scrolls, and the position stays in range")
	{
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 200.0f)); Input::InjectScroll(glm::vec2(0.0f, -1.0f)); });
		CHECK(scroll.ScrollPosition.y == doctest::Approx(60.0f));
		CHECK_FALSE(Input::GetScrollDelta() != glm::vec2(0.0f));	// The game doesn't see the wheel
		ui.Frame();
		CHECK(rectOf(rows[0]).Min.y == doctest::Approx(50.0f));
		ui.Frame([] { Input::InjectScroll(glm::vec2(0.0f, -10.0f)); });
		CHECK(scroll.ScrollPosition.y == doctest::Approx(130.0f));	// 430 - 300
		scroll.ScrollPosition.y = -50.0f;
		ui.Frame();
		CHECK(scroll.ScrollPosition.y == doctest::Approx(0.0f));
	}
	SUBCASE("Scrolled-out rows can't be pointed at")
	{
		scroll.ScrollPosition.y = 130.0f;
		ui.Frame();
		// Row 1 now spans y 50-110, partly above the list, which starts at 100: above it, it is cut away.
		CHECK(rectOf(rows[1]).Min.y == doctest::Approx(50.0f));
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 80.0f)); });
		CHECK_FALSE(ui.SceneRef->GetUIHovered());
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 105.0f)); });
		CHECK(ui.SceneRef->GetUIHovered() == rows[1]);
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(300.0f, 380.0f)); });
		CHECK(ui.SceneRef->GetUIHovered() == rows[5]);
	}
	SUBCASE("Moving focus to a hidden row scrolls it into view")
	{
		REQUIRE(ui.SceneRef->SetUIFocus(rows[0]));
		for (int i = 0; i < 5; i++)
			ui.Tap(KeyCode::Down);
		CHECK(ui.SceneRef->GetUIFocused() == rows[5]);
		CHECK(scroll.ScrollPosition.y == doctest::Approx(130.0f));
		ui.Frame();
		CHECK(rectOf(rows[5]).Max.y <= 400.0f + 0.01f);
		REQUIRE(ui.SceneRef->SetUIFocus(rows[0]));
		CHECK(scroll.ScrollPosition.y == doctest::Approx(0.0f));
	}
	SUBCASE("Hidden children leave the layout")
	{
		rows[0].GetComponent<UIRectComponent>().Visible = false;
		ui.Frame();
		CHECK(rectOf(rows[1]).Min.y == doctest::Approx(110.0f));
	}
}

TEST_CASE("UI widgets: toggles flip, sliders drag and step, dropdowns open and pick")
{
	using Scripting::ScriptValue;
	using Values = Scripting::ScriptValueList;
	UIInputScene ui;
	// Toggle (150, 315)-(450, 345) and slider (150, 355)-(450, 385) in the panel, under the buttons;
	// dropdown (550, 100)-(750, 140) on the canvas, its list (550, 140)-(750, 260) below it.
	Entity toggle = ui.AddRect("Sound", ui.Panel, { 50.0f, 215.0f }, { 350.0f, 245.0f });
	toggle.AddComponent<UIToggleComponent>();
	Entity slider = ui.AddRect("Volume", ui.Panel, { 50.0f, 255.0f }, { 350.0f, 285.0f });
	slider.AddComponent<UISliderComponent>().Step = 0.25f;
	Entity dropdown = ui.AddRect("Quality", ui.Canvas, { 550.0f, 100.0f }, { 750.0f, 140.0f });
	dropdown.AddComponent<UIDropdownComponent>().Options = { "Low", "Medium", "High" };
	dropdown.AddComponent<TextComponent>().Text = "Low";
	ui.Frame();

	SUBCASE("A toggle flips on each click and says so")
	{
		ui.Click(glm::vec2(300.0f, 330.0f));
		CHECK(toggle.GetComponent<UIToggleComponent>().On);
		CHECK(ui.SceneRef->GetUIFocused() == toggle);
		ui.Click(glm::vec2(300.0f, 330.0f));
		CHECK_FALSE(toggle.GetComponent<UIToggleComponent>().On);
		CHECK(ui.ChangesFrom(toggle) == Values { ScriptValue(true), ScriptValue(false) });
		CHECK(ui.EventsFrom(toggle) == Names { "focused", "clicked", "changed", "clicked", "changed" });

		// ui_accept flips the focused toggle too; a toggle that is not interactable ignores everything.
		ui.Tap(KeyCode::Enter);
		CHECK(toggle.GetComponent<UIToggleComponent>().On);
		toggle.GetComponent<UIRectComponent>().Interactable = false;
		ui.Click(glm::vec2(300.0f, 330.0f));
		CHECK(toggle.GetComponent<UIToggleComponent>().On);
	}
	SUBCASE("A slider follows a drag, snapped to its step, even past its ends")
	{
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(150.0f, 370.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK(slider.GetComponent<UISliderComponent>().Value == 0.0f);
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(310.0f, 370.0f)); });
		CHECK(slider.GetComponent<UISliderComponent>().Value == 0.5f);
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(700.0f, 500.0f)); });
		CHECK(slider.GetComponent<UISliderComponent>().Value == 1.0f);
		ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, false); });
		// No change, no event: the press at 0 sent nothing.
		CHECK(ui.ChangesFrom(slider) == Values { ScriptValue(0.5), ScriptValue(1.0) });
		CHECK(ui.SceneRef->GetUIFocused() == slider);

		// Focused, left and right move it a step instead of moving focus.
		ui.Tap(KeyCode::Left);
		CHECK(slider.GetComponent<UISliderComponent>().Value == 0.75f);
		CHECK(ui.SceneRef->GetUIFocused() == slider);
		ui.Tap(KeyCode::Right);
		ui.Tap(KeyCode::Right);
		CHECK(slider.GetComponent<UISliderComponent>().Value == 1.0f);
		CHECK(ui.ChangesFrom(slider).size() == 4);
	}
	SUBCASE("A dropdown opens on a click, picks the clicked row, and shows it")
	{
		ui.Click(glm::vec2(650.0f, 120.0f));
		CHECK(ui.SceneRef->UIWantsPointer());
		// The open list takes the click: the panel under the cursor hears nothing.
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(650.0f, 240.0f)); Input::InjectMouseButton(MouseButton::Left, true); });
		CHECK_FALSE(Input::IsMouseButtonPressed(MouseButton::Left));
		ui.Frame([] { Input::InjectMouseButton(MouseButton::Left, false); });
		CHECK(dropdown.GetComponent<UIDropdownComponent>().Selected == 2);
		CHECK(dropdown.GetComponent<TextComponent>().Text == "High");
		CHECK(ui.ChangesFrom(dropdown) == Values { ScriptValue(2.0) });

		// A click outside the list closes it and changes nothing.
		ui.Click(glm::vec2(650.0f, 120.0f));
		ui.Click(glm::vec2(20.0f, 20.0f));
		CHECK(dropdown.GetComponent<UIDropdownComponent>().Selected == 2);
		ui.Click(glm::vec2(650.0f, 160.0f));
		CHECK(dropdown.GetComponent<UIDropdownComponent>().Selected == 2);	// That click only opened it
	}
	SUBCASE("The keyboard opens a focused dropdown, moves through it, and picks or cancels")
	{
		ui.SceneRef->SetUIFocus(dropdown);
		ui.Tap(KeyCode::Enter);
		ui.Tap(KeyCode::Down);
		ui.Tap(KeyCode::Down);
		ui.Tap(KeyCode::Down);	// Stops at the last option
		ui.Tap(KeyCode::Up);
		ui.Tap(KeyCode::Enter);
		CHECK(dropdown.GetComponent<UIDropdownComponent>().Selected == 1);
		CHECK(dropdown.GetComponent<TextComponent>().Text == "Medium");

		// Escape closes it unchanged, and the next keys move focus again.
		ui.Tap(KeyCode::Enter);
		ui.Tap(KeyCode::Down);
		ui.Tap(KeyCode::Escape);
		CHECK(dropdown.GetComponent<UIDropdownComponent>().Selected == 1);
		CHECK(ui.ChangesFrom(dropdown) == Values { ScriptValue(1.0) });
		CHECK(ui.SceneRef->GetUIFocused() == dropdown);
		ui.Tap(KeyCode::Tab);
		CHECK(ui.SceneRef->GetUIFocused() != dropdown);
	}
	SUBCASE("Stopping play closes an open dropdown")
	{
		ui.Click(glm::vec2(650.0f, 120.0f));
		ui.Frame([] { Input::InjectMousePosition(glm::vec2(20.0f, 20.0f)); });
		CHECK(ui.SceneRef->UIWantsPointer());	// Open, the list takes the pointer everywhere
		ui.SceneRef->OnRuntimeStop();
		ui.SceneRef->OnRuntimeStart();
		ui.Frame();
		CHECK_FALSE(ui.SceneRef->UIWantsPointer());
	}
}
