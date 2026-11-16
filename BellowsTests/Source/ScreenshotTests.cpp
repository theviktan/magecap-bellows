#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

using namespace Bellows;

namespace {

	// A 400x200 image: red on the left half, blue on the right.
	Image MakeTestImage()
	{
		Image image;
		image.Width = 400;
		image.Height = 200;
		image.Pixels.resize(400 * 200 * 4);
		for (uint32_t y = 0; y < 200; y++)
		{
			for (uint32_t x = 0; x < 400; x++)
			{
				uint8_t* p = &image.Pixels[(y * 400 + x) * 4];
				p[0] = x < 200 ? 220 : 10;
				p[1] = 20;
				p[2] = x < 200 ? 30 : 230;
				p[3] = 128;		// Screenshots are opaque whatever the alpha
			}
		}
		return image;
	}

	// Loads an image file with stb; returns width, height and channel count as stored.
	struct LoadedImage
	{
		int Width = 0, Height = 0, Channels = 0;
		std::vector<uint8_t> Rgb;
	};

	LoadedImage Load(const std::filesystem::path& path)
	{
		LoadedImage result;
		stbi_uc* pixels = stbi_load(path.string().c_str(), &result.Width, &result.Height, &result.Channels, 3);
		if (pixels)
		{
			result.Rgb.assign(pixels, pixels + static_cast<size_t>(result.Width) * result.Height * 3);
			stbi_image_free(pixels);
		}
		return result;
	}

	// Screenshots go to a test folder, and the host's game view is a known image.
	struct ScreenshotFixture
	{
		std::filesystem::path UserData = BellowsTests::GetTestOutputDirectory() / "UserData";

		ScreenshotFixture()
		{
			std::error_code error;
			std::filesystem::remove_all(UserData, error);
			Platform::SetEnvVar("BELLOWS_USER_DATA_DIR", UserData.string());
			Scripting::SetScreenshotHandler([] { return std::optional<Image>(MakeTestImage()); });
		}

		~ScreenshotFixture()
		{
			Scripting::SetScreenshotHandler(nullptr);
			Platform::SetEnvVar("BELLOWS_USER_DATA_DIR", "");
		}
	};

}

TEST_CASE("Images scale down by averaging the pixels they cover")
{
	const Image image = MakeTestImage();
	const Image half = image.Resized(200, 100);
	REQUIRE(half.IsValid());
	CHECK(half.GetPixel(10, 10) == glm::u8vec4(220, 20, 30, 128));
	CHECK(half.GetPixel(190, 90) == glm::u8vec4(10, 20, 230, 128));
	// Shrinking to one pixel averages everything.
	const Image dot = image.Resized(1, 1);
	CHECK(dot.GetPixel(0, 0) == glm::u8vec4(115, 20, 130, 128));
	CHECK_FALSE(image.Resized(0, 10).IsValid());
}

TEST_CASE("save_screenshot names, scales and writes PNG and JPEG files in the game's user folder")
{
	ScreenshotFixture fixture;
	Ref<Project> project = Project::Create(BellowsTests::GetTestOutputDirectory() / "ScreenshotProject", "Shot Game");
	REQUIRE(project);
	// Characters a folder name can't hold (on Windows) become '_'.
	project->GetConfig().Name = "Shot: Game?";
	const std::filesystem::path folder = fixture.UserData / "MageCapBellows" / "Games" / "Shot_ Game_";
	CHECK(Scripting::GetUserFolder() == folder);

	// "" picks a name under Screenshots/, and max_width scales a wider image down, keeping its shape.
	Ref<Scene> scene = CreateRef<Scene>("Level 1");
	Scripting::SetContextScene(scene.get());
	const std::filesystem::path first = Scripting::SaveScreenshot("", 200);
	const std::filesystem::path second = Scripting::SaveScreenshot("", 200);
	Scripting::SetContextScene(nullptr);
	CHECK(first.parent_path() == folder / "Screenshots");
	CHECK(first.filename().string().rfind("Level 1-", 0) == 0);
	CHECK(first.extension() == ".png");
	CHECK(second != first);
	LoadedImage png = Load(first);
	CHECK(png.Width == 200);
	CHECK(png.Height == 100);
	CHECK(png.Channels == 3);
	REQUIRE(png.Rgb.size() == 200u * 100u * 3u);
	CHECK(png.Rgb[0] == 220);
	CHECK(png.Rgb[(50 * 200 + 150) * 3 + 2] == 230);

	// Relative paths are inside the user folder; max_width 0 keeps the full size; .jpg saves a JPEG.
	const std::filesystem::path relative = Scripting::SaveScreenshot("Reports/bug", 0);
	CHECK(relative == folder / "Reports" / "bug.png");
	CHECK(Load(relative).Width == 400);
	const std::filesystem::path jpeg = Scripting::SaveScreenshot("bug.JPG", 1920);
	const std::optional<std::string> jpegBytes = FileSystem::ReadTextFile(jpeg);
	REQUIRE(jpegBytes);
	CHECK(static_cast<unsigned char>((*jpegBytes)[0]) == 0xFF);
	CHECK(static_cast<unsigned char>((*jpegBytes)[1]) == 0xD8);
	CHECK(Load(jpeg).Width == 400);

	// Absolute paths are used as given.
	const std::filesystem::path absolute = std::filesystem::absolute(BellowsTests::GetTestOutputDirectory() / "Shots" / "absolute.png");
	CHECK(Scripting::SaveScreenshot(absolute.string(), 1920) == absolute);
	CHECK(FileSystem::Exists(absolute));

	// Mistakes are script errors, never crashes.
	CHECK_THROWS_WITH_AS(Scripting::SaveScreenshot("shot.bmp", 1920), doctest::Contains("not '.bmp'"), Scripting::ScriptValueError);
	CHECK_THROWS_AS(Scripting::SaveScreenshot("", -1), Scripting::ScriptValueError);
	REQUIRE(FileSystem::WriteTextFile(folder / "NotAFolder", "x"));
	CHECK_THROWS_WITH_AS(Scripting::SaveScreenshot("NotAFolder/shot.png", 1920), doctest::Contains("Could not write"), Scripting::ScriptValueError);
	Scripting::SetScreenshotHandler(nullptr);
	CHECK_THROWS_WITH_AS(Scripting::SaveScreenshot("", 1920), doctest::Contains("no game image"), Scripting::ScriptValueError);
	Project::SetActive(nullptr);
}

TEST_CASE("Scripts save screenshots in Python and ForgeScript")
{
	ScreenshotFixture fixture;
	std::string output;
	REQUIRE(ScriptEngine::ExecuteSource(
		"import os\n"
		"path = bellows.save_screenshot('python.png', max_width=100)\n"
		"print(os.path.isfile(path), path.endswith('python.png'), bellows.user_folder() in path)", output));
	CHECK(output.find("True True True") != std::string::npos);
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.save_screenshot('x.gif')", output));
	CHECK(output.find("ValueError") != std::string::npos);

	ForgeScript::VM machine;
	std::string printed;
	machine.SetPrintHandler([&](const std::string& text) { printed += text + "\n"; });
	Scripting::RegisterForgeScriptApi(machine);
	machine.Load(
		"set shot to save_screenshot(\"forge.png\", 50)\n"
		"say shot.ends_with(\"forge.png\")\n"
		"try\n\tsave_screenshot(\"x.gif\")\ncatch p\n\tsay p\nend\n",
		"shot.forge");
	CHECK_EQ(printed, std::string("true\nScreenshots are saved as .png, .jpg or .jpeg, not '.gif'\n"));
	CHECK(Load(fixture.UserData / "MageCapBellows" / "Games" / "Untitled" / "forge.png").Width == 50);
}

TEST_CASE("[gpu] save_screenshot writes the renderer's final image")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	ScreenshotFixture fixture;

	SceneRenderer renderer;
	renderer.SetViewportSize(320, 180);
	Ref<Material> green = CreateRef<Material>("Green");
	green->EditProperties().AlbedoColor = glm::vec4(0.05f, 0.8f, 0.05f, 1.0f);
	RenderCamera camera;
	camera.Position = glm::vec3(0.0f, 1.0f, 3.0f);
	camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 320.0f / 180.0f, 0.1f, 100.0f);
	renderer.BeginScene(camera);
	DirectionalLightData sun;
	sun.Direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
	sun.Intensity = 3.0f;
	renderer.SubmitDirectionalLight(sun);
	renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 1, green);
	renderer.EndScene();

	Scripting::SetScreenshotHandler([&renderer] { return std::optional<Image>(Renderer::ReadbackTexture(renderer.GetFinalImage())); });
	const Image onScreen = Renderer::ReadbackTexture(renderer.GetFinalImage());
	const std::filesystem::path full = Scripting::SaveScreenshot("gpu.png", 0);
	const std::filesystem::path small = Scripting::SaveScreenshot("gpu-small.png", 160);

	const LoadedImage saved = Load(full);
	REQUIRE(saved.Width == 320);
	REQUIRE(saved.Height == 180);
	// The file holds exactly the pixels on screen, alpha dropped; the cube in the middle is green.
	bool same = true;
	for (uint32_t i = 0; i < 320u * 180u && same; i++)
		same = saved.Rgb[i * 3] == onScreen.Pixels[i * 4] && saved.Rgb[i * 3 + 1] == onScreen.Pixels[i * 4 + 1] && saved.Rgb[i * 3 + 2] == onScreen.Pixels[i * 4 + 2];
	CHECK(same);
	const glm::vec4 center = onScreen.GetAverageColor(150, 80, 20, 20);
	CHECK(center.g > center.r * 2.0f);
	const LoadedImage scaled = Load(small);
	CHECK(scaled.Width == 160);
	CHECK(scaled.Height == 90);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
