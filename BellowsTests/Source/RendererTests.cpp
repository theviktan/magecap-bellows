#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Renderer/Texture.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Bellows;

namespace {

	RenderCamera MakeCamera(const glm::vec3& position, const glm::vec3& target, float aspect)
	{
		RenderCamera camera;
		camera.Position = position;
		camera.View = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
		camera.NearClip = 0.1f;
		camera.FarClip = 200.0f;
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), aspect, camera.NearClip, camera.FarClip);
		return camera;
	}

}

TEST_CASE("[gpu] Renderer initializes with all shared resources")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	CHECK(Renderer::IsInitialized());
	CHECK(Renderer::GetBRDFLut() != nullptr);
	CHECK(Renderer::GetWhiteTexture() != nullptr);
	CHECK(Renderer::GetShader("PBR.frag") != nullptr);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] SceneRenderer renders a lit, shadowed cube")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(320, 180);

	Ref<Environment> environment = Environment::CreateProcedural(ProceduralSkySettings {}, EnvironmentQuality { 64, 16, 32, 32 });
	REQUIRE(environment);

	Ref<Material> red = CreateRef<Material>("Red");
	red->EditProperties().AlbedoColor = glm::vec4(0.8f, 0.05f, 0.05f, 1.0f);
	red->EditProperties().Roughness = 0.4f;

	for (int frame = 0; frame < 2; frame++)
	{
		renderer.BeginScene(MakeCamera(glm::vec3(2.5f, 2.0f, 3.5f), glm::vec3(0.0f, 0.3f, 0.0f), 320.0f / 180.0f));
		EnvironmentSettings settings;
		settings.EnvironmentMap = environment;
		renderer.SetEnvironment(settings);
		DirectionalLightData sun;
		sun.Direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
		sun.Intensity = 3.0f;
		renderer.SubmitDirectionalLight(sun);
		renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 7, red);
		renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), 8);
		renderer.EndScene();
	}

	Image image = Renderer::ReadbackTexture(renderer.GetFinalImage());
	REQUIRE(image.IsValid());
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_Cube.png");

	// The cube sits in the middle of the frame and must be clearly red.
	const glm::vec4 center = image.GetAverageColor(150, 80, 20, 20);
	CHECK(center.r > 0.25f);
	CHECK(center.r > center.g * 2.0f);
	CHECK(center.r > center.b * 2.0f);

	// The top of the frame shows the sky (blue-ish, not black).
	const glm::vec4 sky = image.GetAverageColor(0, 0, 320, 10);
	CHECK(sky.b > 0.2f);

	// Entity picking reads back the submitted IDs.
	CHECK(renderer.ReadEntityID(160, 90) == 7u);
	CHECK(renderer.ReadEntityID(10, 170) == 8u);

	CHECK(renderer.GetStatistics().DrawCalls > 0);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A mirrored mesh (a negative scale) shows its outside, lit as the unmirrored one")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	Ref<Environment> environment = Environment::CreateProcedural(ProceduralSkySettings {}, EnvironmentQuality { 64, 16, 32, 32 });
	REQUIRE(environment);
	Ref<Material> red = CreateRef<Material>("Red");
	red->EditProperties().AlbedoColor = glm::vec4(0.8f, 0.05f, 0.05f, 1.0f);

	// The cube and floor are symmetric about x = 0, so mirroring them in x changes nothing a viewer
	// can see, as long as their triangles are seen from the right side.
	auto render = [&](float mirror, SceneRenderer& renderer)
	{
		renderer.SetViewportSize(320, 180);
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(MakeCamera(glm::vec3(2.5f, 2.0f, 3.5f), glm::vec3(0.0f, 0.3f, 0.0f), 320.0f / 180.0f));
			EnvironmentSettings settings;
			settings.EnvironmentMap = environment;
			renderer.SetEnvironment(settings);
			DirectionalLightData sun;
			sun.Direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
			sun.Intensity = 3.0f;
			renderer.SubmitDirectionalLight(sun);
			const glm::mat4 flip = glm::scale(glm::mat4(1.0f), glm::vec3(mirror, 1.0f, 1.0f));
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)) * flip, 7, red);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)) * flip, 8);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};

	SceneRenderer plainRenderer, mirroredRenderer;
	const Image plain = render(1.0f, plainRenderer);
	const Image mirrored = render(-1.0f, mirroredRenderer);
	REQUIRE(plain.IsValid());
	REQUIRE(mirrored.IsValid());
	mirrored.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_MirroredCube.png");

	CHECK(mirroredRenderer.ReadEntityID(160, 90) == 7u);
	CHECK(mirroredRenderer.ReadEntityID(10, 170) == 8u);		// The floor isn't culled away
	for (const glm::ivec2& at : { glm::ivec2(150, 80), glm::ivec2(120, 60), glm::ivec2(180, 100), glm::ivec2(10, 160) })
	{
		const glm::vec4 a = plain.GetAverageColor(at.x, at.y, 12, 12);
		const glm::vec4 b = mirrored.GetAverageColor(at.x, at.y, 12, 12);
		INFO("at ", at.x, ", ", at.y, ": ", a.r, " ", a.g, " ", a.b, " against ", b.r, " ", b.g, " ", b.b);
		CHECK(glm::length(glm::vec3(a) - glm::vec3(b)) < 0.03f);
	}
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] An unlit material shows its color whatever the light; a lit one changes with it")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	Ref<Material> lit = CreateRef<Material>("Lit");
	lit->EditProperties().AlbedoColor = glm::vec4(0.1f, 0.5f, 0.1f, 1.0f);
	Ref<Material> unlit = CreateRef<Material>("Unlit");
	unlit->EditProperties().AlbedoColor = glm::vec4(0.1f, 0.5f, 0.1f, 1.0f);
	unlit->EditProperties().Unlit = true;

	// The unlit cube (left) and the lit one (right), seen from the front so both show a top and a side.
	auto render = [&](float sunIntensity, SceneRenderer& renderer)
	{
		renderer.SetViewportSize(320, 180);
		// Vignette and bloom change a color by where it is on screen; off, so one flat color reads the same everywhere.
		renderer.GetSettings().Vignette = 0.0f;
		renderer.GetSettings().BloomEnabled = false;
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(MakeCamera(glm::vec3(0.0f, 2.2f, 4.0f), glm::vec3(0.0f, 0.4f, 0.0f), 320.0f / 180.0f));
			DirectionalLightData sun;
			sun.Direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.4f));
			sun.Intensity = sunIntensity;
			renderer.SubmitDirectionalLight(sun);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(-0.9f, 0.5f, 0.0f)), 1, unlit);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.9f, 0.5f, 0.0f)), 2, lit);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};

	SceneRenderer dimRenderer, brightRenderer;
	const Image dim = render(0.5f, dimRenderer);
	const Image bright = render(6.0f, brightRenderer);
	REQUIRE(dim.IsValid());
	REQUIRE(bright.IsValid());
	bright.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_Unlit.png");

	// Find each cube's top and front by entity ID: 6 rows in from its highest or lowest row on screen,
	// in the middle of that row (an edge pixel blends with the background).
	auto find = [&](SceneRenderer& renderer, uint32_t id, bool top)
	{
		for (int y = top ? 0 : 179; top ? y < 180 : y >= 0; y += top ? 1 : -1)
		{
			const int row = y + (top ? 6 : -6);
			int first = -1, last = -1;
			for (int x = 0; x < 320; x++)
			{
				if (renderer.ReadEntityID(x, row) == id)
				{
					first = first < 0 ? x : first;
					last = x;
				}
			}
			if (renderer.ReadEntityID((first + last) / 2, y) == id && first >= 0)
				return glm::ivec2((first + last) / 2, row);
		}
		return glm::ivec2(-1);
	};
	const glm::ivec2 unlitTop = find(brightRenderer, 1, true);
	const glm::ivec2 unlitFront = find(brightRenderer, 1, false);
	const glm::ivec2 litTop = find(brightRenderer, 2, true);
	REQUIRE(unlitTop.x >= 0);
	REQUIRE(unlitFront.x >= 0);
	REQUIRE(litTop.x >= 0);
	CHECK(brightRenderer.ReadEntityID(unlitTop.x, unlitTop.y) == 1u);
	CHECK(brightRenderer.ReadEntityID(unlitFront.x, unlitFront.y) == 1u);
	CHECK(brightRenderer.ReadEntityID(litTop.x, litTop.y) == 2u);

	auto color = [](const Image& image, glm::ivec2 at) { return glm::vec3(image.GetAverageColor(at.x - 2, at.y - 2, 4, 4)); };
	// Unlit: the same under both suns, and its top and front are one flat color.
	INFO("unlit top ", color(dim, unlitTop).g, " / ", color(bright, unlitTop).g, ", front ", color(bright, unlitFront).g);
	CHECK(glm::length(color(dim, unlitTop) - color(bright, unlitTop)) < 0.01f);
	CHECK(glm::length(color(bright, unlitTop) - color(bright, unlitFront)) < 0.01f);
	CHECK(color(bright, unlitTop).g > color(bright, unlitTop).r + 0.1f);	// Green, as its albedo
	// Lit: the bright sun makes its top much brighter.
	INFO("lit top ", color(dim, litTop).g, " / ", color(bright, litTop).g);
	CHECK(color(bright, litTop).g > color(dim, litTop).g + 0.1f);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A pixelated material keeps each texture pixel sharp; a smooth one blends them")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	// A 2x2 palette (red, green / blue, white) as the glow of a black floor, so lighting can't color it.
	const uint8_t pixels[] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	const Ref<Texture2D> palette = Texture2D::CreateFromRGBA8(2, 2, pixels, TextureColorSpace::Linear, "Palette");
	REQUIRE(palette);

	auto render = [&](TextureFilter filter)
	{
		Ref<Material> material = CreateRef<Material>("Palette");
		material->EditProperties().AlbedoColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		material->EditProperties().EmissiveColor = glm::vec3(1.0f);
		material->EditProperties().Filter = filter;
		material->SetEmissiveMap(palette);

		SceneRenderer renderer;
		renderer.SetViewportSize(320, 320);
		SceneRendererSettings& settings = renderer.GetSettings();
		settings.BloomEnabled = false;
		settings.SSAOEnabled = false;
		settings.FXAAEnabled = false;
		settings.Vignette = 0.0f;
		for (int frame = 0; frame < 2; frame++)
		{
			// Straight down at a 2 m floor that fills the view.
			RenderCamera camera;
			camera.Position = glm::vec3(0.0f, 5.0f, 0.0f);
			camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
			camera.NearClip = 0.1f;
			camera.FarClip = 20.0f;
			camera.Projection = Math::OrthographicProjection(-1.0f, 1.0f, -1.0f, 1.0f, camera.NearClip, camera.FarClip);
			renderer.BeginScene(camera);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)), 1, material);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};

	const Image pixelated = render(TextureFilter::Nearest);
	const Image smooth = render(TextureFilter::Linear);
	REQUIRE(pixelated.IsValid());
	REQUIRE(smooth.IsValid());
	pixelated.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_Pixelated.png");
	smooth.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_Smooth.png");

	// Pixelated: a spot just beside the middle is the same color as its cell's middle.
	const glm::vec4 cellMiddle = pixelated.GetAverageColor(70, 70, 20, 20);
	const glm::vec4 nearEdge = pixelated.GetAverageColor(150, 70, 6, 20);
	INFO("pixelated: middle ", cellMiddle.r, " ", cellMiddle.g, " ", cellMiddle.b, ", by the edge ", nearEdge.r, " ", nearEdge.g, " ", nearEdge.b);
	CHECK(glm::length(glm::vec3(cellMiddle) - glm::vec3(nearEdge)) < 0.02f);
	CHECK(glm::length(glm::vec3(cellMiddle)) > 0.3f);
	// Across the edge, the next cell starts at once.
	CHECK(glm::length(glm::vec3(pixelated.GetAverageColor(164, 70, 6, 20)) - glm::vec3(nearEdge)) > 0.3f);

	// Smooth: the same spot is already blending toward the next cell.
	const glm::vec4 smoothEdge = smooth.GetAverageColor(150, 70, 6, 20);
	INFO("smooth by the edge ", smoothEdge.r, " ", smoothEdge.g, " ", smoothEdge.b);
	CHECK(glm::length(glm::vec3(smooth.GetAverageColor(70, 70, 20, 20)) - glm::vec3(smoothEdge)) > 0.05f);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Directional light casts shadows onto the ground")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	// Top-down camera; the sun comes from +X so the cube's shadow falls towards -X.
	auto render = [](bool shadows)
	{
		SceneRenderer renderer;
		renderer.SetViewportSize(256, 256);
		renderer.GetSettings().ShadowsEnabled = shadows;
		renderer.GetSettings().SSAOEnabled = false;
		renderer.GetSettings().Vignette = 0.0f;
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(MakeCamera(glm::vec3(0.0f, 12.0f, 0.01f), glm::vec3(0.0f), 1.0f));
			renderer.SetEnvironment(EnvironmentSettings {});
			DirectionalLightData sun;
			sun.Direction = glm::normalize(glm::vec3(-1.0f, -1.0f, 0.0f));
			sun.Intensity = 3.0f;
			renderer.SubmitDirectionalLight(sun);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 1);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), 2);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};

	const Image lit = render(false);
	const Image shadowed = render(true);
	REQUIRE(lit.IsValid());
	REQUIRE(shadowed.IsValid());
	shadowed.SavePNG(BellowsTests::GetTestOutputDirectory() / "SceneRenderer_Shadow.png");

	const RenderCamera camera = MakeCamera(glm::vec3(0.0f, 12.0f, 0.01f), glm::vec3(0.0f), 1.0f);
	auto groundBrightness = [&camera](const Image& image, float worldX)
	{
		glm::vec2 screen(0.0f);
		REQUIRE(Math::WorldToScreenPoint(glm::vec3(worldX, 0.0f, 0.0f), camera.Projection * camera.View, glm::vec2(256.0f), screen));
		return image.GetAverageColor(static_cast<uint32_t>(screen.x) - 4, static_cast<uint32_t>(screen.y) - 4, 8, 8).g;
	};

	// World x = -1 on the ground lies in the cube's shadow; x = +2.5 is in full sunlight.
	const float litValue = groundBrightness(lit, -1.0f);
	const float shadowValue = groundBrightness(shadowed, -1.0f);
	INFO("unshadowed " << litValue << ", shadowed " << shadowValue);
	CHECK(shadowValue < litValue * 0.7f);
	CHECK(groundBrightness(shadowed, 2.5f) == doctest::Approx(groundBrightness(lit, 2.5f)).epsilon(0.05));
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
