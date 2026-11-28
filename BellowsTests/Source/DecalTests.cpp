#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Renderer/ShaderInterop.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <functional>

using namespace Bellows;

TEST_CASE("Decals are saved and loaded, and their values are held to their ranges")
{
	Scene scene("Decals");
	Entity mark = scene.CreateEntity("Footprint");
	DecalComponent& decal = mark.AddComponent<DecalComponent>();
	decal.TexturePath = "Textures/Footprint.png";
	decal.Color = glm::vec4(0.5f, 0.25f, 1.0f, 0.75f);
	decal.Size = glm::vec3(0.4f, 0.2f, 0.8f);
	decal.MaxAngle = 30.0f;
	decal.SortOrder = 5;

	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity loadedMark = loaded.TryGetEntityByUUID(mark.GetUUID());
	REQUIRE(loadedMark);
	const DecalComponent& read = loadedMark.GetComponent<DecalComponent>();
	CHECK(read.TexturePath == "Textures/Footprint.png");
	CHECK(read.Color == glm::vec4(0.5f, 0.25f, 1.0f, 0.75f));
	CHECK(read.Size == glm::vec3(0.4f, 0.2f, 0.8f));
	CHECK(read.MaxAngle == 30.0f);
	CHECK(read.SortOrder == 5);
	CHECK(SceneSerializer::SerializeToJson(loaded)["Entities"].dump() == json["Entities"].dump());

	DecalComponent wild;
	wild.Size = glm::vec3(0.0f, std::nanf(""), 5000.0f);
	wild.MaxAngle = 120.0f;
	wild.SortOrder = -99999;
	wild.Color = glm::vec4(2.0f, -1.0f, 0.5f, 1.0f);
	wild.Clamp();
	CHECK(wild.Size == glm::vec3(DecalComponent::MinSize, 1.0f, DecalComponent::MaxSize));
	CHECK(wild.MaxAngle == DecalComponent::MaxMaxAngle);
	CHECK(wild.SortOrder == -DecalComponent::MaxSortOrder);
	CHECK(wild.Color == glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
}

namespace {

	constexpr uint32_t c_Width = 160, c_Height = 120;

	RenderCamera TopCamera()
	{
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 6.0f, 4.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), static_cast<float>(c_Width) / c_Height, 0.1f, 100.0f);
		return camera;
	}

	// The pixel a world point lands on (y down from the top row).
	glm::uvec2 PixelOf(const RenderCamera& camera, const glm::vec3& point)
	{
		const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(point, 1.0f);
		const glm::vec2 ndc = glm::vec2(clip) / clip.w;
		return glm::uvec2(static_cast<uint32_t>((ndc.x * 0.5f + 0.5f) * c_Width), static_cast<uint32_t>((0.5f - ndc.y * 0.5f) * c_Height));
	}

	struct DecalScene
	{
		float SunIntensity = 2.0f;
		std::function<void(SceneRenderer&)> Decals;
	};

	// Owned by each test (never a static: GPU resources must go before the device does at exit).
	Ref<Material> MakeGray()
	{
		Ref<Material> material = CreateRef<Material>("Gray");
		material->EditProperties().AlbedoColor = glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
		material->EditProperties().Roughness = 1.0f;
		material->EditProperties().Metallic = 0.0f;
		return material;
	}

	// A gray floor with a gray block on it, lit from straight above, drawn plainly (no tonemapping
	// curve or post effects), with whatever decals the test submits.
	Image Draw(SceneRenderer& renderer, const Ref<Material>& gray, const DecalScene& setup)
	{
		SceneRendererSettings& settings = renderer.GetSettings();
		settings.ToneMapper = Tonemapper::None;
		settings.BloomEnabled = false;
		settings.SSAOEnabled = false;
		settings.FXAAEnabled = false;
		settings.ShadowsEnabled = false;
		settings.Vignette = 0.0f;
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(TopCamera());
			DirectionalLightData sun;
			sun.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
			sun.Intensity = setup.SunIntensity;
			sun.CastShadows = false;
			renderer.SubmitDirectionalLight(sun);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(20.0f)), 1, gray);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 2, gray);
			if (setup.Decals)
				setup.Decals(renderer);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	}

	glm::mat4 Box(const glm::vec3& center, const glm::vec3& size)
	{
		return glm::scale(glm::translate(glm::mat4(1.0f), center), size);
	}

	bool IsRed(const glm::u8vec4& pixel) { return pixel.r > 100 && pixel.r > pixel.g * 3 && pixel.r > pixel.b * 3; }
	bool IsGray(const glm::u8vec4& pixel) { return std::abs(int(pixel.r) - int(pixel.g)) < 8 && std::abs(int(pixel.g) - int(pixel.b)) < 8 && pixel.r > 60; }

}

TEST_CASE("[gpu] Decals color the surfaces in their box, lit like them, and skip turned-away and see-through ones")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Width, c_Height);
	const Ref<Material> gray = MakeGray();
	const RenderCamera camera = TopCamera();
	const glm::uvec2 floorSpot = PixelOf(camera, { 0.0f, 0.0f, 2.0f });
	const glm::uvec2 blockTop = PixelOf(camera, { 0.0f, 1.0f, 0.0f });
	const glm::uvec2 blockFront = PixelOf(camera, { 0.0f, 0.5f, 0.5f });
	const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);

	SUBCASE("a plain red decal on the floor, and nowhere else")
	{
		const Image plain = Draw(renderer, gray, {});
		REQUIRE(IsGray(plain.GetPixel(floorSpot.x, floorSpot.y)));
		const Image marked = Draw(renderer, gray, { 2.0f, [&](SceneRenderer& r) { r.SubmitDecal(Box({ 0.0f, 0.0f, 2.0f }, glm::vec3(1.0f)), red); } });
		CHECK(renderer.GetStatistics().Decals == 1);
		CHECK(IsRed(marked.GetPixel(floorSpot.x, floorSpot.y)));
		CHECK(marked.GetPixel(blockTop.x, blockTop.y) == plain.GetPixel(blockTop.x, blockTop.y));
		CHECK(marked.GetPixel(2, c_Height - 2) == plain.GetPixel(2, c_Height - 2));
		marked.SavePNG(BellowsTests::GetTestOutputDirectory() / "Decals_Floor.png");
	}

	SUBCASE("lit like the surface: less light, a darker decal")
	{
		const auto marked = [&](float sun)
		{
			return Draw(renderer, gray, { sun, [&](SceneRenderer& r) { r.SubmitDecal(Box({ 0.0f, 0.0f, 2.0f }, glm::vec3(1.0f)), red); } })
				.GetPixel(floorSpot.x, floorSpot.y);
		};
		const glm::u8vec4 bright = marked(2.0f), dim = marked(0.5f);
		CHECK(IsRed(bright));
		CHECK(dim.r + 30 < bright.r);
	}

	SUBCASE("a decal over the block marks its top but not its upright sides")
	{
		const Image plain = Draw(renderer, gray, {});
		const Image marked = Draw(renderer, gray, { 2.0f, [&](SceneRenderer& r) { r.SubmitDecal(Box({ 0.0f, 0.5f, 0.0f }, glm::vec3(1.4f)), red); } });
		CHECK(IsRed(marked.GetPixel(blockTop.x, blockTop.y)));
		CHECK(marked.GetPixel(blockFront.x, blockFront.y) == plain.GetPixel(blockFront.x, blockFront.y));
	}

	SUBCASE("an image, top toward the decal's -Z")
	{
		// Two pixels: green on top, blue below.
		const uint8_t pixels[] = { 0, 255, 0, 255, 0, 0, 255, 255 };
		Ref<Texture2D> image = Texture2D::CreateFromRGBA8(1, 2, pixels, TextureColorSpace::SRGB, "DecalTestImage");
		const Image marked = Draw(renderer, gray, { 2.0f, [&](SceneRenderer& r) { r.SubmitDecal(Box({ 0.0f, 0.0f, 2.0f }, glm::vec3(1.0f)), glm::vec4(1.0f), image); } });
		const glm::uvec2 top = PixelOf(camera, { 0.0f, 0.0f, 1.7f }), bottom = PixelOf(camera, { 0.0f, 0.0f, 2.3f });
		const glm::u8vec4 topPixel = marked.GetPixel(top.x, top.y), bottomPixel = marked.GetPixel(bottom.x, bottom.y);
		CHECK(topPixel.g > topPixel.b + 60);
		CHECK(bottomPixel.b > bottomPixel.g + 60);
		marked.SavePNG(BellowsTests::GetTestOutputDirectory() / "Decals_Image.png");
	}

	SUBCASE("higher sort orders land on top, whatever order they're submitted in")
	{
		const glm::vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);
		const Image marked = Draw(renderer, gray, { 2.0f, [&](SceneRenderer& r)
		{
			r.SubmitDecal(Box({ 0.0f, 0.0f, 2.0f }, glm::vec3(1.0f)), red, nullptr, 60.0f, 1);
			r.SubmitDecal(Box({ 0.0f, 0.0f, 2.0f }, glm::vec3(1.0f)), blue, nullptr, 60.0f, 0);
		} });
		CHECK(IsRed(marked.GetPixel(floorSpot.x, floorSpot.y)));
	}

	SUBCASE("more images than the atlas holds: the rest aren't drawn, and the frame stays clean")
	{
		std::vector<Ref<Texture2D>> images;
		for (uint32_t i = 0; i < ShaderInterop::MaxDecalTextures + 2; i++)
		{
			const uint8_t pixel[] = { static_cast<uint8_t>(i * 10), 0, 0, 255 };
			images.push_back(Texture2D::CreateFromRGBA8(1, 1, pixel, TextureColorSpace::SRGB, "DecalTestImage" + std::to_string(i)));
		}
		Draw(renderer, gray, { 2.0f, [&](SceneRenderer& r)
		{
			for (size_t i = 0; i < images.size(); i++)
				r.SubmitDecal(Box({ static_cast<float>(i) * 0.1f, 0.0f, 2.0f }, glm::vec3(0.1f)), glm::vec4(1.0f), images[i]);
		} });
		CHECK(renderer.GetStatistics().Decals == ShaderInterop::MaxDecalTextures);
	}

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A scene draws its decals, but not those of a group hidden in the editor's view")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Width, c_Height);
	Scene scene("Decals");
	Entity mark = scene.CreateEntity("Mark");
	mark.AddComponent<DecalComponent>().Color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	scene.CreateEntity("Faded").AddComponent<DecalComponent>().Color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

	scene.OnRender(renderer, TopCamera());
	CHECK(renderer.GetStatistics().Decals == 1);

	SceneGroup& group = scene.CreateGroup("Marks");
	const UUID groupID = group.ID;
	scene.SetEntityGroup(mark, groupID);
	scene.FindGroup(groupID)->HiddenInEditor = true;
	scene.OnRender(renderer, TopCamera(), {}, true);
	CHECK(renderer.GetStatistics().Decals == 0);
	scene.OnRender(renderer, TopCamera(), {}, false);
	CHECK(renderer.GetStatistics().Decals == 1);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
