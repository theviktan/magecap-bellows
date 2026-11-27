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

	constexpr uint32_t c_Size = 128;

	uint32_t CountRedPixels(SceneRenderer& renderer, Scene& scene, bool editorView)
	{
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, camera, {}, editorView);
		const Image color = Renderer::ReadbackTexture(renderer.GetFinalImage());
		uint32_t red = 0;
		for (uint32_t y = 0; y < c_Size; y++)
		{
			for (uint32_t x = 0; x < c_Size; x++)
			{
				const glm::u8vec4 pixel = color.GetPixel(x, y);
				red += pixel.r > 200 && pixel.g < 80 && pixel.b < 80 ? 1 : 0;
			}
		}
		return red;
	}

}

TEST_CASE("[gpu] A group hidden in the editor isn't drawn in the editor's view, and is in the game's")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	renderer.GetSettings().SSAOEnabled = false;
	renderer.GetSettings().BloomEnabled = false;
	renderer.GetSettings().FXAAEnabled = false;
	renderer.GetSettings().Vignette = 0.0f;

	Scene scene("Hidden Group");
	Entity sign = scene.CreateEntity("Sign");
	TextComponent& text = scene.CreateChildEntity(sign, "Label").AddComponent<TextComponent>();
	text.Text = "H";
	text.Size = 3.0f;
	text.Color = glm::vec4(1.0f, 0.1f, 0.1f, 1.0f);
	SceneGroup& signs = scene.CreateGroup("Signs");
	const UUID signsID = signs.ID;
	scene.SetEntityGroup(sign, signsID);

	const uint32_t shown = CountRedPixels(renderer, scene, true);
	CHECK(shown > 100);
	scene.FindGroup(signsID)->HiddenInEditor = true;
	CHECK(CountRedPixels(renderer, scene, true) == 0);
	CHECK(CountRedPixels(renderer, scene, false) == shown);
	scene.FindGroup(signsID)->HiddenInEditor = false;
	CHECK(CountRedPixels(renderer, scene, true) == shown);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
