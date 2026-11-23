#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"

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
