#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

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
