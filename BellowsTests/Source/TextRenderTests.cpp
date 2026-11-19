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

	struct TextScene
	{
		Ref<Scene> SceneRef = CreateRef<Scene>("Text");
		Entity Label;

		TextScene()
		{
			Label = SceneRef->CreateEntity("Label");
			TextComponent& text = Label.AddComponent<TextComponent>();
			text.Text = "H";
			text.Size = 3.0f;
			text.Color = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);
		}

		TextComponent& Text() { return Label.GetComponent<TextComponent>(); }
	};

	struct Rendered
	{
		Image Color;
		uint32_t RedPixels = 0;
		uint32_t GreenPixels = 0;
		glm::uvec2 RedPixel { 0 };	// The reddest pixel: inside a stroke, not on its soft edge
	};

	Rendered Render(SceneRenderer& renderer, Scene& scene)
	{
		renderer.GetSettings().SSAOEnabled = false;
		renderer.GetSettings().BloomEnabled = false;
		renderer.GetSettings().Vignette = 0.0f;
		renderer.GetSettings().FXAAEnabled = false;
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera());

		Rendered result;
		result.Color = Renderer::ReadbackTexture(renderer.GetFinalImage());
		int reddest = -1;
		for (uint32_t y = 0; y < c_Size; y++)
		{
			for (uint32_t x = 0; x < c_Size; x++)
			{
				const glm::u8vec4 pixel = result.Color.GetPixel(x, y);
				if (pixel.r > 200 && pixel.g < 80 && pixel.b < 80)
				{
					result.RedPixels++;
					if (pixel.r - pixel.g > reddest)
					{
						reddest = pixel.r - pixel.g;
						result.RedPixel = glm::uvec2(x, y);
					}
				}
				if (pixel.g > 200 && pixel.r < 80 && pixel.b < 80)
					result.GreenPixels++;
			}
		}
		return result;
	}

}

TEST_CASE("[gpu] World text draws in its color, can be picked, and hides behind the scene")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	TextScene scene;

	const Rendered plain = Render(renderer, *scene.SceneRef);
	plain.Color.SavePNG(BellowsTests::GetTestOutputDirectory() / "Text_World.png");
	INFO("red pixels: " << plain.RedPixels);
	// A 3 m tall H seen from 5 m covers a good part of the image, in exactly its color.
	CHECK(plain.RedPixels > 1500);
	CHECK(plain.Color.GetPixel(plain.RedPixel.x, plain.RedPixel.y).r == doctest::Approx(255).epsilon(0.02));
	// The H's two legs and the gap between them: the very middle column at the top is empty.
	const glm::u8vec4 corner = plain.Color.GetPixel(4, 4);
	CHECK(corner.r < 120);

	// Picking finds the text's entity on a letter, and nothing beside it.
	CHECK(renderer.ReadEntityID(plain.RedPixel.x, plain.RedPixel.y) == scene.Label.GetRenderID());
	CHECK(renderer.ReadEntityID(4, 4) == 0);

	// An outline grows a green rim around the letter.
	scene.Text().OutlineWidth = 0.08f;
	scene.Text().OutlineColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
	const Rendered outlined = Render(renderer, *scene.SceneRef);
	outlined.Color.SavePNG(BellowsTests::GetTestOutputDirectory() / "Text_Outline.png");
	CHECK(plain.GreenPixels == 0);
	CHECK(outlined.GreenPixels > 300);
	CHECK(outlined.RedPixels > plain.RedPixels / 2);

	// A cube between the camera and the text hides it.
	scene.Text().OutlineWidth = 0.0f;
	Entity wall = scene.SceneRef->CreateEntity("Wall");
	wall.GetTransform().Translation = glm::vec3(0.0f, 0.0f, 2.0f);
	wall.GetTransform().Scale = glm::vec3(4.0f, 4.0f, 0.2f);
	wall.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Cube";
	scene.SceneRef->OnUpdateEditor(Timestep(0.0f));
	const Rendered hidden = Render(renderer, *scene.SceneRef);
	CHECK(hidden.RedPixels == 0);

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Text facing the camera stays readable from any side")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	TextScene scene;
	// Turned to face away from the camera, flat text is seen edge-on and mirrored...
	scene.Label.GetTransform().SetRotationEuler(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));
	const Rendered edgeOn = Render(renderer, *scene.SceneRef);
	CHECK(edgeOn.RedPixels < 100);

	// ...but facing the camera it is drawn whole.
	scene.Text().FaceCamera = true;
	const Rendered facing = Render(renderer, *scene.SceneRef);
	CHECK(facing.RedPixels > 1500);

	// Invisible text draws nothing.
	scene.Text().Visible = false;
	CHECK(Render(renderer, *scene.SceneRef).RedPixels == 0);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
