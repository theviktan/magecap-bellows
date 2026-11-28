#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Bellows;

namespace {

	constexpr uint32_t c_Size = 128;

	// Draws a gray cube (entity 7), selected or not, and counts the warm pixels: the outline's and
	// the glow's orange over a neutral picture (the glow is faint, so any warm tint counts).
	uint32_t CountWarmPixels(SceneRenderer& renderer, bool selected, bool glow)
	{
		SceneRendererSettings& settings = renderer.GetSettings();
		settings.ToneMapper = Tonemapper::None;
		settings.BloomEnabled = false;
		settings.SSAOEnabled = false;
		settings.FXAAEnabled = false;
		settings.Vignette = 0.0f;
		settings.ShowSelectionOutline = true;
		settings.ShowSelectionGlow = glow;

		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(camera);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::mat4(1.0f), 7);
			renderer.SetSelectedEntities(selected ? std::vector<uint32_t> { 7 } : std::vector<uint32_t> {});
			renderer.EndScene();
		}
		const Image image = Renderer::ReadbackTexture(renderer.GetFinalImage());
		uint32_t warm = 0;
		for (uint32_t y = 0; y < c_Size; y++)
		{
			for (uint32_t x = 0; x < c_Size; x++)
			{
				const glm::u8vec4 pixel = image.GetPixel(x, y);
				warm += pixel.r > pixel.b + 4 ? 1 : 0;
			}
		}
		return warm;
	}

}

TEST_CASE("[gpu] The selection outline has a faint glow around it that can be turned off")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	const uint32_t none = CountWarmPixels(renderer, false, true);
	const uint32_t outline = CountWarmPixels(renderer, true, false);
	const uint32_t glowing = CountWarmPixels(renderer, true, true);
	CHECK(none == 0);
	CHECK(outline > 50);
	// The glow reaches several pixels past the 2-pixel outline all the way round.
	CHECK(glowing > outline + outline / 2);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
