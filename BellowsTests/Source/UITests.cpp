#include <doctest/doctest.h>

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/UI/UILayout.h"

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
