#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/UI/UIStyle.h"
#include "Bellows/UI/UIWidgets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <functional>

using namespace Bellows;

namespace {

	constexpr uint32_t c_Size = 256;

	RenderCamera FrontCamera()
	{
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
		return camera;
	}

	Entity CreateRect(Scene& scene, const char* name, Entity parent, glm::vec2 min, glm::vec2 max)
	{
		Entity entity = scene.CreateEntity(name);
		scene.SetParent(entity, parent);
		auto& rect = entity.AddComponent<UIRectComponent>();
		rect.AnchorMin = rect.AnchorMax = glm::vec2(0.0f);
		rect.OffsetMin = min;
		rect.OffsetMax = max;
		return entity;
	}

	// A canvas at one reference pixel per screen pixel, with:
	//   Panel   (20, 20)-(120, 100): blue with a 4 px orange border
	//   Pill    (140, 20)-(240, 120): purple with 30 px rounded corners
	//   Label   (20, 140)-(236, 236): green "Hi" under the panel's sibling group
	struct UIScene
	{
		Ref<Scene> SceneRef = CreateRef<Scene>("UI");
		Entity Canvas, Panel, Pill, Label;

		UIScene()
		{
			// World text behind the UI, to show the UI is drawn over the scene.
			Entity sign = SceneRef->CreateEntity("Sign");
			TextComponent& signText = sign.AddComponent<TextComponent>();
			signText.Text = "H";
			signText.Size = 3.0f;
			signText.Color = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);

			Canvas = SceneRef->CreateEntity("Canvas");
			Canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;

			Panel = CreateRect(*SceneRef, "Panel", Canvas, { 20.0f, 20.0f }, { 120.0f, 100.0f });
			UIImageComponent& panelImage = Panel.AddComponent<UIImageComponent>();
			panelImage.Color = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
			panelImage.BorderWidth = 4.0f;
			panelImage.BorderColor = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f);

			Pill = CreateRect(*SceneRef, "Pill", Canvas, { 140.0f, 20.0f }, { 240.0f, 120.0f });
			UIImageComponent& pillImage = Pill.AddComponent<UIImageComponent>();
			pillImage.Color = glm::vec4(0.6f, 0.2f, 0.8f, 1.0f);
			pillImage.CornerRadius = 30.0f;

			Label = CreateRect(*SceneRef, "Label", Canvas, { 20.0f, 140.0f }, { 236.0f, 236.0f });
			TextComponent& text = Label.AddComponent<TextComponent>();
			text.Text = "Hi";
			text.Size = 64.0f;
			text.Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		}
	};

	Image Render(SceneRenderer& renderer, Scene& scene)
	{
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera());
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	}

	bool Near(const glm::u8vec4& pixel, const glm::vec4& color, int tolerance = 2)
	{
		for (int channel = 0; channel < 3; channel++)
		{
			if (std::abs(static_cast<int>(pixel[channel]) - static_cast<int>(std::round(color[channel] * 255.0f))) > tolerance)
				return false;
		}
		return true;
	}

	bool IsGreen(const glm::u8vec4& pixel)
	{
		return pixel.g > 200 && pixel.r < 80 && pixel.b < 80;
	}

	uint32_t CountGreen(const Image& image, glm::uvec2 min, glm::uvec2 max)
	{
		uint32_t count = 0;
		for (uint32_t y = min.y; y < max.y; y++)
		{
			for (uint32_t x = min.x; x < max.x; x++)
				count += IsGreen(image.GetPixel(x, y)) ? 1 : 0;
		}
		return count;
	}

}

TEST_CASE("[gpu] Screen UI draws over the scene in its exact colors and can be picked")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	UIScene scene;
	// Strong bloom and exposure change everything in the scene, but never the UI.
	scene.SceneRef->GetSettings().Rendering.BloomEnabled = true;
	scene.SceneRef->GetSettings().Rendering.Exposure = 3.0f;

	const Image image = Render(renderer, *scene.SceneRef);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_Screen.png");

	// The panel covers the world text's middle, in its own color; its border runs inside its edge.
	CHECK(Near(image.GetPixel(70, 60), glm::vec4(0.2f, 0.4f, 0.8f, 1.0f)));
	CHECK(Near(image.GetPixel(21, 60), glm::vec4(1.0f, 0.6f, 0.0f, 1.0f)));
	CHECK(Near(image.GetPixel(118, 60), glm::vec4(1.0f, 0.6f, 0.0f, 1.0f)));
	CHECK(Near(image.GetPixel(70, 98), glm::vec4(1.0f, 0.6f, 0.0f, 1.0f)));
	CHECK_FALSE(Near(image.GetPixel(70, 104), glm::vec4(1.0f, 0.6f, 0.0f, 1.0f), 40));

	// The pill's corners are cut round; its middle and the middle of its edges are filled.
	const glm::vec4 purple(0.6f, 0.2f, 0.8f, 1.0f);
	CHECK(Near(image.GetPixel(190, 70), purple));
	CHECK(Near(image.GetPixel(141, 70), purple));
	CHECK_FALSE(Near(image.GetPixel(142, 22), purple, 40));
	CHECK_FALSE(Near(image.GetPixel(238, 118), purple, 40));

	// The label's text lands in its rectangle and nowhere else.
	const uint32_t greenInside = CountGreen(image, { 20, 140 }, { 236, 236 });
	INFO("green pixels in the label: " << greenInside);
	CHECK(greenInside > 400);
	CHECK(CountGreen(image, { 0, 0 }, { c_Size, 140 }) == 0);

	// Picking finds the UI on top of the world.
	CHECK(renderer.ReadEntityID(70, 60) == scene.Panel.GetRenderID());
	CHECK(renderer.ReadEntityID(190, 70) == scene.Pill.GetRenderID());
	CHECK(renderer.ReadEntityID(142, 22) != scene.Pill.GetRenderID());

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Screen UI scales with its canvas, hides, clips and can be switched off")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	UIScene scene;
	const glm::vec4 blue(0.2f, 0.4f, 0.8f, 1.0f);

	SUBCASE("A 128 by 128 reference on a 256 pixel screen doubles everything")
	{
		UICanvasComponent& canvas = scene.Canvas.GetComponent<UICanvasComponent>();
		canvas.ScaleMode = UIScaleMode::Expand;
		canvas.ReferenceSize = glm::vec2(128.0f);
		const Image image = Render(renderer, *scene.SceneRef);
		// The panel now spans (40, 40)-(240, 200), its border 8 pixels wide.
		CHECK(Near(image.GetPixel(140, 120), blue));
		CHECK(Near(image.GetPixel(45, 120), glm::vec4(1.0f, 0.6f, 0.0f, 1.0f)));
		CHECK(Near(image.GetPixel(52, 120), blue));
	}
	SUBCASE("Hiding a rect hides it and what is under it")
	{
		Entity badge = CreateRect(*scene.SceneRef, "Badge", scene.Panel, { 10.0f, 10.0f }, { 30.0f, 30.0f });
		badge.AddComponent<UIImageComponent>().Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		CHECK(IsGreen(Render(renderer, *scene.SceneRef).GetPixel(40, 40)));
		scene.Panel.GetComponent<UIRectComponent>().Visible = false;
		const Image image = Render(renderer, *scene.SceneRef);
		CHECK_FALSE(Near(image.GetPixel(70, 60), blue, 40));
		CHECK_FALSE(IsGreen(image.GetPixel(40, 40)));
	}
	SUBCASE("Clipped text stays inside its rectangle")
	{
		UIRectComponent& rect = scene.Label.GetComponent<UIRectComponent>();
		rect.OffsetMax = glm::vec2(60.0f, 236.0f);	// Narrower than "Hi"
		TextComponent& text = scene.Label.GetComponent<TextComponent>();
		text.Wrap = false;
		text.Alignment = TextAlignment::Left;
		text.Text = "Hi there";
		CHECK(CountGreen(Render(renderer, *scene.SceneRef), { 60, 140 }, { c_Size, 236 }) > 0);
		text.Overflow = TextOverflow::Clip;
		const Image image = Render(renderer, *scene.SceneRef);
		CHECK(CountGreen(image, { 20, 140 }, { 60, 236 }) > 100);
		CHECK(CountGreen(image, { 61, 140 }, { c_Size, 236 }) == 0);
	}
	SUBCASE("The editor can switch the UI off")
	{
		renderer.GetSettings().ShowUI = false;
		const Image image = Render(renderer, *scene.SceneRef);
		CHECK_FALSE(Near(image.GetPixel(70, 60), blue, 40));
		CHECK(CountGreen(image, { 0, 0 }, { c_Size, c_Size }) == 0);
		CHECK(renderer.ReadEntityID(70, 60) != scene.Panel.GetRenderID());
	}
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] 9-slice images keep their borders, and scroll areas clip and show a scroll bar")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	// A 16 by 16 texture: a 4 pixel red frame around a blue middle.
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "UISliceProject";
	Image frame;
	frame.Width = frame.Height = 16;
	frame.Pixels.resize(16 * 16 * 4);
	for (uint32_t y = 0; y < 16; y++)
	{
		for (uint32_t x = 0; x < 16; x++)
		{
			const bool edge = x < 4 || y < 4 || x >= 12 || y >= 12;
			uint8_t* pixel = &frame.Pixels[(y * 16 + x) * 4];
			pixel[0] = edge ? 255 : 0;
			pixel[1] = 0;
			pixel[2] = edge ? 0 : 255;
			pixel[3] = 255;
		}
	}
	REQUIRE(frame.SavePNG(assets / "Frame.png"));
	AssetManager::SetAssetDirectory(assets);

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Ref<Scene> scene = CreateRef<Scene>("UI slices");
	Entity canvas = scene->CreateEntity("Canvas");
	canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;

	// (20, 20)-(220, 100), sliced 4 pixels in from each side.
	Entity sliced = CreateRect(*scene, "Sliced", canvas, { 20.0f, 20.0f }, { 220.0f, 100.0f });
	auto& image = sliced.AddComponent<UIImageComponent>();
	image.TexturePath = "Frame.png";
	image.SliceBorder = glm::vec4(4.0f);

	// A list at (20, 130)-(220, 230) holding a 400 high green block, so it scrolls.
	Entity list = CreateRect(*scene, "List", canvas, { 20.0f, 130.0f }, { 220.0f, 230.0f });
	list.AddComponent<UIScrollComponent>();
	Entity block = CreateRect(*scene, "Block", list, { 0.0f, 0.0f }, { 180.0f, 400.0f });
	block.AddComponent<UIImageComponent>().Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

	Image result = Render(renderer, *scene);
	result.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_Slices.png");

	const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f), blue(0.0f, 0.0f, 1.0f, 1.0f);
	// The border stays 4 pixels wide; stretched whole it would be 50 wide on the left and right.
	CHECK(Near(result.GetPixel(21, 60), red, 8));
	CHECK(Near(result.GetPixel(30, 60), blue, 8));
	CHECK(Near(result.GetPixel(120, 22), red, 8));
	CHECK(Near(result.GetPixel(120, 30), blue, 8));
	CHECK(Near(result.GetPixel(218, 98), red, 8));

	// The block is cut at the list's bottom edge, and a scroll bar thumb shows on its right.
	CHECK(IsGreen(result.GetPixel(100, 225)));
	CHECK_FALSE(IsGreen(result.GetPixel(100, 240)));
	const glm::u8vec4 thumb = result.GetPixel(213, 150);
	CHECK(thumb.r > 100);	// The light thumb over the dark background, not green
	CHECK_FALSE(IsGreen(thumb));

	// Scrolled to the end, the block's bottom is in view, and the thumb has moved down.
	list.GetComponent<UIScrollComponent>().ScrollPosition.y = 1000.0f;
	result = Render(renderer, *scene);
	CHECK(list.GetComponent<UIScrollComponent>().ScrollPosition.y == doctest::Approx(300.0f));
	CHECK(IsGreen(result.GetPixel(100, 135)));
	CHECK(result.GetPixel(213, 220).r > 100);

	AssetManager::SetAssetDirectory({});
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Widgets draw their parts, the focus ring and an open dropdown list")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Ref<Scene> scene = CreateRef<Scene>("Widgets");
	Entity canvas = scene->CreateEntity("Canvas");
	canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;

	Entity button = CreateRect(*scene, "Button", canvas, { 10.0f, 10.0f }, { 110.0f, 40.0f });
	button.AddComponent<UIImageComponent>();
	button.AddComponent<UIButtonComponent>();
	Entity disabled = CreateRect(*scene, "Disabled", canvas, { 130.0f, 10.0f }, { 230.0f, 40.0f });
	disabled.AddComponent<UIImageComponent>();
	disabled.AddComponent<UIButtonComponent>();
	disabled.GetComponent<UIRectComponent>().Interactable = false;
	Entity off = CreateRect(*scene, "Off", canvas, { 10.0f, 60.0f }, { 110.0f, 90.0f });
	off.AddComponent<UIToggleComponent>();
	Entity on = CreateRect(*scene, "On", canvas, { 130.0f, 60.0f }, { 230.0f, 90.0f });
	on.AddComponent<UIToggleComponent>().On = true;
	Entity slider = CreateRect(*scene, "Slider", canvas, { 10.0f, 110.0f }, { 230.0f, 130.0f });
	slider.AddComponent<UISliderComponent>().Value = 0.5f;
	Entity dropdown = CreateRect(*scene, "Dropdown", canvas, { 140.0f, 150.0f }, { 250.0f, 175.0f });
	dropdown.AddComponent<UIImageComponent>();
	dropdown.AddComponent<UIDropdownComponent>().Options = { "Low", "Medium", "High" };
	dropdown.AddComponent<TextComponent>().Size = 14.0f;

	// Open the dropdown from the keyboard, as a player would.
	InputMap::SetActions(InputMap::GetDefaultActions());
	Input::ResetState();
	scene->OnViewportResize(c_Size, c_Size);
	scene->OnRuntimeStart();
	REQUIRE(scene->SetUIFocus(dropdown));
	Input::BeginFrame();
	Input::InjectKey(KeyCode::Enter, true);
	scene->OnUpdateRuntime(Timestep(1.0f / 60.0f));
	Input::BeginFrame();
	Input::InjectKey(KeyCode::Enter, false);
	scene->OnUpdateRuntime(Timestep(1.0f / 60.0f));

	const Image image = Render(renderer, *scene);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_Widgets.png");

	// A button colors its image by state.
	CHECK(Near(image.GetPixel(60, 25), UIColors::NavyLight));
	CHECK_FALSE(Near(image.GetPixel(180, 25), UIColors::NavyLight, 6));

	// A toggle's box is filled with the accent only when it is on.
	const UIRectangle offBox = ComputeUIToggleBox(UIRectangle { { 10.0f, 60.0f }, { 110.0f, 90.0f } }, 1.0f);
	const UIRectangle onBox = ComputeUIToggleBox(UIRectangle { { 130.0f, 60.0f }, { 230.0f, 90.0f } }, 1.0f);
	const glm::vec2 offCenter = (offBox.Min + offBox.Max) * 0.5f;
	const glm::vec2 onCenter = (onBox.Min + onBox.Max) * 0.5f;
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(offCenter.x), static_cast<uint32_t>(offCenter.y)), UIColors::Navy));
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(onCenter.x), static_cast<uint32_t>(onCenter.y)), UIColors::Accent));

	// A slider at half: filled up to the knob, plain after it, the knob off-white.
	const UISliderGeometry geometry = ComputeUISliderGeometry(UIRectangle { { 10.0f, 110.0f }, { 230.0f, 130.0f } }, 0.5f, 0.0f, 1.0f, 1.0f);
	const uint32_t trackY = static_cast<uint32_t>(geometry.Knob.y);
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(geometry.Knob.x - 40.0f), trackY), UIColors::Accent));
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(geometry.Knob.x + 40.0f), trackY), UIColors::Slate));
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(geometry.Knob.x), trackY), UIColors::OffWhite));

	// The open dropdown shows pressed, has the focus ring, and its list (one row per option) is drawn
	// under it with the selected row highlighted.
	CHECK(Near(image.GetPixel(160, 162), UIColors::Accent));
	CHECK(Near(image.GetPixel(138, 162), UIColors::Accent, 12));
	const UIRectangle list = ComputeUIDropdownList(UIRectangle { { 140.0f, 150.0f }, { 250.0f, 175.0f } }, 3, glm::vec2(c_Size));
	CHECK(list.Min.y == doctest::Approx(175.0f));
	CHECK(Near(image.GetPixel(248, 187), UIColors::Navy, 6));	// Row 0, right of its highlight
	CHECK(Near(image.GetPixel(240, 187), UIColors::Slate, 6));	// Row 0's highlight
	CHECK(Near(image.GetPixel(240, 212), UIColors::Navy, 6));	// Row 1
	CHECK(dropdown.GetComponent<TextComponent>().Text == "Low");
	CHECK(renderer.ReadEntityID(200, 212) == dropdown.GetRenderID());

	scene->OnRuntimeStop();
	Input::ResetState();
	Input::BeginFrame();
	InputMap::SetActions({});
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Text inputs draw their text, placeholder, selection, caret and password bullets")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Ref<Scene> scene = CreateRef<Scene>("Text inputs");
	Entity canvas = scene->CreateEntity("Canvas");
	canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;

	const auto addField = [&](const char* name, glm::vec2 min, glm::vec2 max, const char* text) -> Entity
	{
		Entity field = CreateRect(*scene, name, canvas, min, max);
		UIImageComponent& image = field.AddComponent<UIImageComponent>();
		image.Color = UIColors::Navy;
		TextComponent& label = field.AddComponent<TextComponent>();
		label.Text = text;
		label.Size = 24.0f;
		label.Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		field.AddComponent<UITextInputComponent>();
		return field;
	};
	Entity empty = addField("Empty", { 10.0f, 10.0f }, { 246.0f, 50.0f }, "");
	empty.GetComponent<UITextInputComponent>().Placeholder = "Name";
	Entity edited = addField("Edited", { 10.0f, 70.0f }, { 246.0f, 110.0f }, "hello world");
	Entity secret = addField("Secret", { 10.0f, 130.0f }, { 246.0f, 170.0f }, "abc");
	secret.GetComponent<UITextInputComponent>().Password = true;

	// Focus the edited field and select "world" with Shift+Ctrl+Left, as a player would.
	InputMap::SetActions(InputMap::GetDefaultActions());
	Input::ResetState();
	scene->OnViewportResize(c_Size, c_Size);
	scene->OnRuntimeStart();
	REQUIRE(scene->SetUIFocus(edited));
	const auto frame = [&](const std::function<void()>& input)
	{
		Input::BeginFrame();
		input();
		scene->OnUpdateRuntime(Timestep(1.0f / 60.0f));
	};
	frame([] { Input::InjectKey(KeyCode::LeftShift, true); Input::InjectKey(KeyCode::LeftControl, true); Input::InjectKey(KeyCode::Left, true); });
	frame([] { Input::InjectKey(KeyCode::LeftShift, false); Input::InjectKey(KeyCode::LeftControl, false); Input::InjectKey(KeyCode::Left, false); });
	REQUIRE(edited.GetComponent<UITextInputComponent>().RuntimeSelection.Begin() == 6);

	const Image image = Render(renderer, *scene);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_TextInputs.png");

	// The placeholder is drawn dimmed: green, but well short of full green.
	uint32_t dim = 0, bright = 0;
	for (uint32_t y = 10; y < 50; y++)
	{
		for (uint32_t x = 10; x < 246; x++)
		{
			const glm::u8vec4 pixel = image.GetPixel(x, y);
			dim += pixel.g > 60 && pixel.g < 170 && pixel.r < 60 ? 1 : 0;
			bright += IsGreen(pixel) ? 1 : 0;
		}
	}
	CHECK(dim > 40);
	CHECK(bright == 0);

	// The edited field's text is in full color, the selection band behind "world" shows orange, and the
	// caret (in the text's color) stands at the selection's start.
	CHECK(CountGreen(image, { 10, 70 }, { 246, 110 }) > 100);
	uint32_t band = 0;
	for (uint32_t x = 10; x < 246; x++)
		band += image.GetPixel(x, 80).r > 90 && image.GetPixel(x, 80).b < 90 ? 1 : 0;
	CHECK(band > 20);

	// The password shows three bullets, not its letters: three separate green blobs.
	uint32_t runs = 0;
	bool inRun = false;
	for (uint32_t x = 10; x < 246; x++)
	{
		bool any = false;
		for (uint32_t y = 140; y < 160; y++)
			any |= IsGreen(image.GetPixel(x, y));
		runs += any && !inRun ? 1 : 0;
		inRun = any;
	}
	CHECK(runs == 3);
	CHECK(renderer.ReadEntityID(120, 90) == edited.GetRenderID());

	scene->OnRuntimeStop();
	Input::ResetState();
	Input::BeginFrame();
	InputMap::SetActions({});
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A canvas's theme and a rect's style class restyle the widgets they draw")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "UIThemeProject";
	REQUIRE(FileSystem::WriteTextFile(assets / "Themes" / "Loud.btheme", R"({
		"Accent": "#00FF00",
		"Toggle": { "Normal": { "Background": "#FF00FF" } },
		"Dropdown": { "Normal": { "Background": "#FFFF00" } },
		"Classes": { "cold": { "Toggle": { "Normal": { "Background": "#0000FF" } } } }
	})"));
	AssetManager::SetAssetDirectory(assets);
	AssetManager::Invalidate("Themes/Loud.btheme");

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Ref<Scene> scene = CreateRef<Scene>("Themes");
	Entity canvas = scene->CreateEntity("Canvas");
	UICanvasComponent& canvasComponent = canvas.AddComponent<UICanvasComponent>();
	canvasComponent.ScaleMode = UIScaleMode::ConstantPixels;
	canvasComponent.Theme = "Themes/Loud.btheme";
	Entity plain = CreateRect(*scene, "Plain", canvas, { 10.0f, 10.0f }, { 110.0f, 40.0f });
	plain.AddComponent<UIToggleComponent>().On = false;
	Entity cold = CreateRect(*scene, "Cold", canvas, { 10.0f, 60.0f }, { 110.0f, 90.0f });
	cold.AddComponent<UIToggleComponent>().On = true;
	cold.GetComponent<UIRectComponent>().StyleClass = "cold";
	Entity dropdown = CreateRect(*scene, "Dropdown", canvas, { 10.0f, 110.0f }, { 200.0f, 140.0f });
	dropdown.AddComponent<UIImageComponent>();
	dropdown.AddComponent<UIDropdownComponent>().Options = { "One" };

	const Image image = Render(renderer, *scene);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_Themes.png");

	const auto boxCenter = [](glm::vec2 min, glm::vec2 max)
	{
		const UIRectangle box = ComputeUIToggleBox(UIRectangle { min, max }, 1.0f);
		const glm::vec2 center = (box.Min + box.Max) * 0.5f;
		return glm::uvec2(center);
	};
	const glm::uvec2 plainBox = boxCenter({ 10.0f, 10.0f }, { 110.0f, 40.0f });
	const glm::uvec2 coldBox = boxCenter({ 10.0f, 60.0f }, { 110.0f, 90.0f });
	const UIRectangle coldRect = ComputeUIToggleBox(UIRectangle { { 10.0f, 60.0f }, { 110.0f, 90.0f } }, 1.0f);
	CHECK(Near(image.GetPixel(plainBox.x, plainBox.y), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)));
	CHECK(Near(image.GetPixel(coldBox.x, coldBox.y), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)));	// The check is the theme's accent
	CHECK(Near(image.GetPixel(static_cast<uint32_t>(coldRect.Min.x) + 4, coldBox.y), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)));	// The class's box
	CHECK(Near(image.GetPixel(100, 125), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)));

	// Without a theme the canvas draws with the default again.
	canvasComponent.Theme.clear();
	const Image plainImage = Render(renderer, *scene);
	CHECK(Near(plainImage.GetPixel(plainBox.x, plainBox.y), UIColors::Navy));

	AssetManager::SetAssetDirectory({});
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Theme classes and theme-colored labels: a primary button, and a disabled label dims")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Ref<Scene> scene = CreateRef<Scene>("Classes");
	Entity canvas = scene->CreateEntity("Canvas");
	canvas.AddComponent<UICanvasComponent>().ScaleMode = UIScaleMode::ConstantPixels;
	const auto addButton = [&](const char* name, glm::vec2 min, glm::vec2 max) -> Entity
	{
		Entity button = CreateRect(*scene, name, canvas, min, max);
		button.AddComponent<UIImageComponent>();
		button.AddComponent<UIButtonComponent>();
		TextComponent& label = button.AddComponent<TextComponent>();
		label.Text = "OK";
		label.Size = 48.0f;
		label.ThemeColor = true;
		return button;
	};
	Entity primary = addButton("Primary", { 10.0f, 10.0f }, { 246.0f, 80.0f });
	primary.GetComponent<UIRectComponent>().StyleClass = "primary";
	Entity off = addButton("Off", { 10.0f, 100.0f }, { 246.0f, 170.0f });
	off.GetComponent<UIRectComponent>().Interactable = false;
	off.GetComponent<TextComponent>().ThemeColor = false;
	off.GetComponent<TextComponent>().Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

	const Image image = Render(renderer, *scene);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "UI_ThemeClasses.png");

	// The primary button is the accent, and its label navy (dark pixels on the orange).
	CHECK(Near(image.GetPixel(20, 20), UIColors::Accent));
	uint32_t navyText = 0;
	for (uint32_t y = 10; y < 80; y++)
	{
		for (uint32_t x = 10; x < 246; x++)
			navyText += Near(image.GetPixel(x, y), UIColors::Navy, 12) ? 1 : 0;
	}
	CHECK(navyText > 100);
	// The disabled button's own green label is drawn at half strength: no full-green pixel.
	uint32_t fullGreen = 0, someGreen = 0;
	for (uint32_t y = 100; y < 170; y++)
	{
		for (uint32_t x = 10; x < 246; x++)
		{
			const glm::u8vec4 pixel = image.GetPixel(x, y);
			fullGreen += pixel.g > 240 && pixel.r < 60 ? 1 : 0;
			someGreen += pixel.g > 100 && pixel.g > pixel.r + 40 ? 1 : 0;
		}
	}
	CHECK(fullGreen == 0);
	CHECK(someGreen > 100);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
