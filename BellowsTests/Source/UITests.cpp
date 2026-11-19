#include <doctest/doctest.h>

#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/UI/UILayout.h"

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
			Listener = Scripting::AddScriptEventListener([this](const std::string& name, const Scripting::ScriptValueList&,
				const Scripting::ScriptEventDelivery& delivery)
			{
				Events.push_back({ name, delivery.Source ? delivery.Source->ID : UUID(0), delivery.Target ? delivery.Target->ID : UUID(0) });
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
