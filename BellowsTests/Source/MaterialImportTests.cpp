#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <tinyexr.h>

using namespace Bellows;

namespace {

	// An 8x4 OpenEXR image: the left half 0.5, the right half 4.0 (brighter than white, like a sky's sun).
	std::filesystem::path WriteTestEXR(const std::string& name, int components)
	{
		std::vector<float> pixels(8 * 4 * static_cast<size_t>(components));
		for (int y = 0; y < 4; y++)
		{
			for (int x = 0; x < 8; x++)
			{
				for (int c = 0; c < components; c++)
					pixels[(static_cast<size_t>(y) * 8 + x) * components + c] = c == 3 ? 1.0f : (x < 4 ? 0.5f : 4.0f);
			}
		}
		const std::filesystem::path path = BellowsTests::GetTestOutputDirectory() / name;
		const char* error = nullptr;
		const int result = SaveEXR(pixels.data(), 8, 4, components, 0, path.string().c_str(), &error);
		if (error)
			FreeEXRErrorMessage(error);
		REQUIRE(result == TINYEXR_SUCCESS);
		return path;
	}

	Ref<Texture2D> MakeGray(uint8_t value)
	{
		const uint8_t pixel[4] = { value, value, value, 255 };
		return Texture2D::CreateFromRGBA8(1, 1, pixel, TextureColorSpace::Linear, "Gray");
	}

}

TEST_CASE("OpenEXR files are textures that can also be skies")
{
	CHECK(AssetManager::GetAssetType("Textures/Rock_Normal.exr") == AssetType::Texture);
	CHECK(AssetManager::GetAssetType("Skies/Sunset.EXR") == AssetType::Texture);
	CHECK(AssetManager::CanUseAs("Skies/Sunset.exr", AssetType::Environment));
	CHECK(AssetManager::CanUseAs("Skies/Sunset.hdr", AssetType::Environment));
	CHECK(AssetManager::CanUseAs("Skies/Sunset.hdr", AssetType::Texture));
	CHECK(AssetManager::CanUseAs("Textures/Rock.png", AssetType::Texture));
	CHECK_FALSE(AssetManager::CanUseAs("Textures/Rock.png", AssetType::Environment));
	CHECK_FALSE(AssetManager::CanUseAs("Scripts/Rock.py", AssetType::Texture));
}

TEST_CASE("Materials save and load separate roughness and metallic maps")
{
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "MaterialMaps";
	AssetManager::SetAssetDirectory(assets);
	Ref<Material> material = CreateRef<Material>("Rock");
	material->RoughnessMapPath = "Textures/Rock_Roughness.png";
	material->MetallicMapPath = "Textures/Rock_Metalness.exr";
	REQUIRE(AssetManager::SaveMaterial(material, "Materials/Rock.bmat"));
	Ref<Material> loaded = AssetManager::LoadMaterialFile(assets / "Materials" / "Rock.bmat");
	REQUIRE(loaded);
	CHECK(loaded->RoughnessMapPath == "Textures/Rock_Roughness.png");
	CHECK(loaded->MetallicMapPath == "Textures/Rock_Metalness.exr");
	CHECK(loaded->Clone()->MetallicMapPath == "Textures/Rock_Metalness.exr");

	// Files from before these slots existed still load, with both slots empty.
	REQUIRE(FileSystem::WriteTextFile(assets / "Materials" / "Old.bmat", R"({ "Name": "Old", "Roughness": 0.3 })"));
	Ref<Material> old = AssetManager::LoadMaterialFile(assets / "Materials" / "Old.bmat");
	REQUIRE(old);
	CHECK(old->RoughnessMapPath.empty());
	CHECK(old->MetallicMapPath.empty());
	CHECK(old->GetProperties().Roughness == doctest::Approx(0.3f));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Materials save and load Unlit; older files are lit")
{
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "MaterialUnlit";
	AssetManager::SetAssetDirectory(assets);
	Ref<Material> material = CreateRef<Material>("Flat");
	CHECK_FALSE(material->GetProperties().Unlit);
	material->EditProperties().Unlit = true;
	REQUIRE(AssetManager::SaveMaterial(material, "Materials/Flat.bmat"));
	Ref<Material> loaded = AssetManager::LoadMaterialFile(assets / "Materials" / "Flat.bmat");
	REQUIRE(loaded);
	CHECK(loaded->GetProperties().Unlit);

	REQUIRE(FileSystem::WriteTextFile(assets / "Materials" / "Old.bmat", R"({ "Name": "Old", "Metallic": 0.5 })"));
	Ref<Material> old = AssetManager::LoadMaterialFile(assets / "Materials" / "Old.bmat");
	REQUIRE(old);
	CHECK_FALSE(old->GetProperties().Unlit);
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Materials save and load their texture filter and wrap modes")
{
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "MaterialSampling";
	AssetManager::SetAssetDirectory(assets);
	Ref<Material> material = CreateRef<Material>("Palette");
	material->EditProperties().Filter = TextureFilter::Nearest;
	material->EditProperties().WrapU = TextureWrap::Clamp;
	material->EditProperties().WrapV = TextureWrap::Mirror;
	REQUIRE(AssetManager::SaveMaterial(material, "Materials/Palette.bmat"));
	Ref<Material> loaded = AssetManager::LoadMaterialFile(assets / "Materials" / "Palette.bmat");
	REQUIRE(loaded);
	CHECK(loaded->GetProperties().Filter == TextureFilter::Nearest);
	CHECK(loaded->GetProperties().WrapU == TextureWrap::Clamp);
	CHECK(loaded->GetProperties().WrapV == TextureWrap::Mirror);

	// Older files, and values this version doesn't know, keep smooth and repeating.
	REQUIRE(FileSystem::WriteTextFile(assets / "Materials" / "Odd.bmat", R"({ "Name": "Odd", "TextureFilter": "Bicubic", "WrapU": 3, "WrapV": "Mirror" })"));
	Ref<Material> odd = AssetManager::LoadMaterialFile(assets / "Materials" / "Odd.bmat");
	REQUIRE(odd);
	CHECK(odd->GetProperties().Filter == TextureFilter::Linear);
	CHECK(odd->GetProperties().WrapU == TextureWrap::Repeat);
	CHECK(odd->GetProperties().WrapV == TextureWrap::Mirror);

	for (TextureFilter filter : { TextureFilter::Linear, TextureFilter::Nearest })
		CHECK(TextureFilterFromString(TextureFilterToString(filter)) == filter);
	for (TextureWrap wrap : { TextureWrap::Repeat, TextureWrap::Clamp, TextureWrap::Mirror })
		CHECK(TextureWrapFromString(TextureWrapToString(wrap)) == wrap);
	CHECK_FALSE(TextureFilterFromString("nearest"));
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("[gpu] OpenEXR images load as 8-bit material maps or as float skies")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	const std::filesystem::path rgba = WriteTestEXR("Test_RGBA.exr", 4);
	const std::filesystem::path gray = WriteTestEXR("Test_Gray.exr", 1);

	// As a material map: 8 bits with mips; values above 1 are clipped.
	Ref<Texture2D> map = Texture2D::LoadFromFile(rgba, TextureColorSpace::Linear);
	REQUIRE(map);
	CHECK(map->GetWidth() == 8);
	CHECK(map->GetHeight() == 4);
	CHECK_FALSE(map->IsHDR());
	CHECK(map->GetMipCount() == 4);
	Image pixels = Renderer::ReadbackTexture(map->GetHandle());
	REQUIRE(pixels.IsValid());
	CHECK(pixels.GetPixel(0, 0) == glm::u8vec4(128, 128, 128, 255));
	CHECK(pixels.GetPixel(7, 3) == glm::u8vec4(255, 255, 255, 255));

	// A single-channel EXR (how roughness maps often come) reads as gray; color maps are sRGB-encoded.
	Image grayPixels = Renderer::ReadbackTexture(Texture2D::LoadFromFile(gray, TextureColorSpace::SRGB)->GetHandle());
	CHECK(grayPixels.GetPixel(0, 0).r == grayPixels.GetPixel(0, 0).g);
	CHECK(grayPixels.GetPixel(0, 0).r == 188);

	// As a sky: full float range, and the environment builds from it.
	Ref<Texture2D> sky = Texture2D::LoadFromFile(rgba, TextureColorSpace::Linear, true);
	REQUIRE(sky);
	CHECK(sky->IsHDR());
	CHECK(sky->GetMipCount() == 1);
	CHECK(Environment::LoadFromFile(rgba, EnvironmentQuality { 32, 8, 16, 16 }));

	// Not an image at all: an error, no crash.
	REQUIRE(FileSystem::WriteTextFile(BellowsTests::GetTestOutputDirectory() / "Broken.exr", "v/1\x01 but not really"));
	CHECK_FALSE(Texture2D::LoadFromFile(BellowsTests::GetTestOutputDirectory() / "Broken.exr", TextureColorSpace::Linear));
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Separate roughness and metallic maps change the shading")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	Ref<Environment> environment = Environment::CreateProcedural(ProceduralSkySettings {}, EnvironmentQuality { 64, 16, 32, 32 });
	REQUIRE(environment);

	auto render = [&](const Ref<Material>& material)
	{
		SceneRenderer renderer;
		renderer.SetViewportSize(160, 90);
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 1.0f, 3.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 160.0f / 90.0f, 0.1f, 100.0f);
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(camera);
			EnvironmentSettings settings;
			settings.EnvironmentMap = environment;
			renderer.SetEnvironment(settings);
			DirectionalLightData sun;
			sun.Direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.6f));
			sun.Intensity = 3.0f;
			renderer.SubmitDirectionalLight(sun);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Sphere), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 1, material);
			renderer.EndScene();
		}
		return Renderer::ReadbackTexture(renderer.GetFinalImage()).GetAverageColor(65, 30, 30, 30);
	};

	// A red metal; a black metallic map turns it back into plastic, which keeps its red diffuse color.
	Ref<Material> metal = CreateRef<Material>("Metal");
	metal->EditProperties().AlbedoColor = glm::vec4(0.9f, 0.1f, 0.1f, 1.0f);
	metal->EditProperties().Metallic = 1.0f;
	metal->EditProperties().Roughness = 0.6f;
	const glm::vec4 metallic = render(metal);
	Ref<Material> plastic = metal->Clone();
	plastic->SetMetallicMap(MakeGray(0));
	const glm::vec4 notMetallic = render(plastic);
	CHECK(glm::length(glm::vec3(metallic) - glm::vec3(notMetallic)) > 0.05f);

	// A black roughness map makes the surface mirror-smooth; a white one leaves it as set.
	Ref<Material> rough = plastic->Clone();
	rough->SetRoughnessMap(MakeGray(255));
	Ref<Material> smooth = plastic->Clone();
	smooth->SetRoughnessMap(MakeGray(0));
	CHECK(glm::length(glm::vec3(render(rough)) - glm::vec3(notMetallic)) < 0.01f);
	CHECK(glm::length(glm::vec3(render(smooth)) - glm::vec3(notMetallic)) > 0.01f);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
