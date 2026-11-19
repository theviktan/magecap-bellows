#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Project/ProjectExporter.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <stb_image.h>

#include <cstdlib>

#if !defined(_WIN32)
	#include <sys/wait.h>
#endif

using namespace Bellows;

TEST_CASE("Projects are created, saved and loaded")
{
	const std::filesystem::path directory = BellowsTests::GetTestOutputDirectory() / "Projects" / "Created";
	Ref<Project> project = Project::Create(directory, "Created Game");
	REQUIRE(project);
	CHECK(Project::GetActive() == project);
	CHECK(FileSystem::Exists(directory / "Created Game.bproject"));
	CHECK(FileSystem::Exists(directory / "Assets" / "Scenes" / "Main.bscene"));
	CHECK(FileSystem::Exists(directory / "Assets" / "Scripts"));
	CHECK(AssetManager::GetAssetDirectory() == std::filesystem::absolute(directory / "Assets"));

	project->GetConfig().WindowWidth = 1024;
	project->GetConfig().Fullscreen = true;
	project->GetConfig().BusVolumes["Music"] = 0.3f;
	// New projects start with the default input actions.
	CHECK(project->GetConfig().InputActions == InputMap::GetDefaultActions());
	project->GetConfig().InputActions["dash"] = { InputBinding::Key(KeyCode::LeftShift), InputBinding::Gamepad(GamepadButton::RightBumper) };
	project->GetConfig().InputActions.erase("pause");
	REQUIRE(project->Save());

	Ref<Project> loaded = Project::Load(directory);
	REQUIRE(loaded);
	CHECK(loaded->GetConfig().Name == "Created Game");
	CHECK(loaded->GetConfig().WindowWidth == 1024);
	CHECK(loaded->GetConfig().Fullscreen);
	CHECK(loaded->GetConfig().BusVolumes["Music"] == doctest::Approx(0.3f));
	CHECK(loaded->GetConfig().BusVolumes["Effects"] == doctest::Approx(1.0f));
	CHECK(loaded->GetConfig().InputActions == project->GetConfig().InputActions);
	loaded->ApplyInputSettings();
	CHECK(InputMap::HasAction("dash"));
	CHECK_FALSE(InputMap::HasAction("pause"));
	InputMap::SetActions({});

	// Game and play start use the project's mixer, unmuted.
	AudioEngine::SetBusMuted("Music", true);
	loaded->ApplyAudioSettings();
	CHECK(AudioEngine::GetBusVolume("Music") == doctest::Approx(0.3f));
	CHECK_FALSE(AudioEngine::IsBusMuted("Music"));
	AudioEngine::SetBusVolume("Music", 1.0f);

	Ref<Scene> scene = CreateRef<Scene>();
	REQUIRE(SceneSerializer::Deserialize(*scene, loaded->GetAssetDirectory() / loaded->GetConfig().StartScene));
	CHECK(scene->FindEntityByName("Main Camera"));
	CHECK(scene->FindEntityByName("Sun"));

	CHECK_FALSE(Project::Load(directory / "Missing.bproject"));
	Project::SetActive(nullptr);
}

TEST_CASE("A minimal project file with only a name and an asset folder loads")
{
	// MageCap Forge writes such a project to probe `BellowsEditor --check-script`; keep accepting it.
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "MinimalProject";
	REQUIRE(FileSystem::WriteTextFile(folder / "Probe.bproject", R"({ "Name": "Probe", "AssetDirectory": "Content" })"));
	Ref<Project> project = Project::Load(folder / "Probe.bproject");
	REQUIRE(project);
	CHECK(project->GetConfig().Name == "Probe");
	CHECK(project->GetAssetDirectory() == std::filesystem::absolute(folder) / "Content");
	CHECK(AssetManager::GetAssetDirectory() == project->GetAssetDirectory());
	CHECK(project->GetConfig().InputActions.empty());
	Project::SetActive(nullptr);
}

TEST_CASE("Saving a project writes the file it was loaded from, whatever the project's name")
{
	// Regression: Save used to write '<Name>.bproject', leaving a second project file next to the first.
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "RenamedProject";
	REQUIRE(FileSystem::WriteTextFile(folder / "Game.bproject", R"({ "Name": "My Great Game" })"));
	Ref<Project> project = Project::Load(folder / "Game.bproject");
	REQUIRE(project);
	CHECK(project->GetProjectFilePath() == std::filesystem::absolute(folder) / "Game.bproject");
	project->GetConfig().Name = "Renamed";
	REQUIRE(project->Save());
	CHECK_FALSE(FileSystem::Exists(folder / "My Great Game.bproject"));
	CHECK_FALSE(FileSystem::Exists(folder / "Renamed.bproject"));
	Ref<Project> reloaded = Project::Load(folder);
	REQUIRE(reloaded);
	CHECK(reloaded->GetConfig().Name == "Renamed");
	Project::SetActive(nullptr);
}

TEST_CASE("Project input actions skip bindings they cannot read")
{
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "InputProject";
	REQUIRE(FileSystem::WriteTextFile(folder / "Input.bproject", R"({
		"Name": "Input",
		"Input": { "Actions": { "jump": ["Key:Space", "Key:NoSuchKey", 7, "gamepad:a"], "broken": "Key:W" } }
	})"));
	Ref<Project> project = Project::Load(folder / "Input.bproject");
	REQUIRE(project);
	const InputActionMap& actions = project->GetConfig().InputActions;
	REQUIRE(actions.count("jump") == 1);
	CHECK(actions.at("jump") == std::vector<InputBinding> { InputBinding::Key(KeyCode::Space), InputBinding::Gamepad(GamepadButton::A) });
	// An action whose inputs are not a list is kept, with no inputs, so it can be fixed in Project Settings.
	CHECK(actions.at("broken").empty());
	Project::SetActive(nullptr);
}

TEST_CASE("Export sanitizes names and refuses unsafe output folders")
{
	CHECK(ProjectExporter::SanitizeName("My Game!") == "My Game");
	CHECK(ProjectExporter::SanitizeName("../../evil") == "evil");
	CHECK(ProjectExporter::SanitizeName("???") == "Game");

	const std::filesystem::path directory = BellowsTests::GetTestOutputDirectory() / "Projects" / "Unsafe";
	Ref<Project> project = Project::Create(directory, "Unsafe");
	REQUIRE(project);
	ExportOptions options;
	options.OutputDirectory = directory / "Assets" / "Build";
	CHECK_FALSE(ProjectExporter::Export(*project, options).Success);
	options.OutputDirectory = directory;
	CHECK_FALSE(ProjectExporter::Export(*project, options).Success);
	Project::SetActive(nullptr);
}

TEST_CASE("[gpu] Exported games run standalone with a bundled Python runtime")
{
	BellowsTests::GetTestApplication();

	const std::filesystem::path directory = BellowsTests::GetTestOutputDirectory() / "Projects" / "Exported";
	Ref<Project> project = Project::Create(directory, "Export Test");
	REQUIRE(project);

	// A script that moves the cube, checks itself and quits with a status code.
	REQUIRE(FileSystem::WriteTextFile(project->GetAssetDirectory() / "Scripts" / "Checker.py", R"PY(
import bellows

class Checker(bellows.Script):
    def on_create(self):
        self.frames = 0
        self.cube = bellows.find_entity("Cube")

    def on_update(self, dt):
        self.frames += 1
        self.cube.rotate((0.0, 90.0 * dt, 0.0))
        if self.frames == 20:
            import json, math
            ok = self.cube is not None and bellows.scene_name() == "Main" and math.isclose(dt, 1.0 / 60.0, rel_tol=1e-4)
            bellows.quit(0 if ok else 3)
)PY"));

	Ref<Scene> scene = CreateRef<Scene>();
	const std::filesystem::path scenePath = project->GetAssetDirectory() / project->GetConfig().StartScene;
	REQUIRE(SceneSerializer::Deserialize(*scene, scenePath));
	scene->CreateEntity("Checker").AddComponent<ScriptComponent>().ScriptPath = "Scripts/Checker.py";
	scene->CreateEntity("Title").AddComponent<TextComponent>().Text = "Exported";
	REQUIRE(SceneSerializer::Serialize(*scene, scenePath));

	ExportOptions options;
	options.OutputDirectory = BellowsTests::GetTestOutputDirectory() / "Exports" / "ExportTest";
	const ExportResult result = ProjectExporter::Export(*project, options);
	for (const std::string& message : result.Messages)
		MESSAGE(message);
	REQUIRE(result.Success);
	CHECK(FileSystem::Exists(result.ExecutablePath));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Export Test.bproject"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Assets" / "Scripts" / "Checker.py"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Shaders" / "PBR.frag.spv"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "ThirdPartyNotices.md"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Resources" / "Fonts" / "Inter" / "Inter-Regular.ttf"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Resources" / "Fonts" / "Inter" / "OFL.txt"));
	CHECK(FileSystem::Exists(options.OutputDirectory / "Python" / "bellows" / "__init__.py"));
	CHECK(FileSystem::Exists(options.OutputDirectory / ScriptEngine::BundledRuntimeFolder));
	Project::SetActive(nullptr);

	// Run the exported game in its own process, without access to the build's Python.
	const std::filesystem::path screenshot = options.OutputDirectory / "Screenshot.png";
	const std::string command = "\"" + result.ExecutablePath.string() + "\" --headless --width 320 --height 180 --frames 120 --screenshot \""
		+ screenshot.string() + "\"";
	const int status = std::system(command.c_str());
#if defined(BW_PLATFORM_WINDOWS)
	const int exitCode = status;
#else
	const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
	CHECK(exitCode == 0);

	// The game quit itself after 20 frames, so it never reached the screenshot frame.
	CHECK_FALSE(FileSystem::Exists(screenshot));

	// A second run without the checker script renders and saves a screenshot.
	std::filesystem::remove(options.OutputDirectory / "Assets" / "Scripts" / "Checker.py");
	const int secondStatus = std::system(command.c_str());
#if defined(BW_PLATFORM_WINDOWS)
	CHECK(secondStatus == 2);	// The scene still references the deleted script: reported as a script error
#else
	CHECK((WIFEXITED(secondStatus) ? WEXITSTATUS(secondStatus) : -1) == 2);
#endif
	REQUIRE(FileSystem::Exists(screenshot));
	int width = 0, height = 0, channels = 0;
	stbi_uc* pixels = stbi_load(screenshot.string().c_str(), &width, &height, &channels, 4);
	REQUIRE(pixels);
	CHECK(width == 320);
	CHECK(height == 180);
	stbi_image_free(pixels);
}
