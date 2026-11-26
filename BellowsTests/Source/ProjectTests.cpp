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
	// A project with no input settings still gets the actions the game UI navigates with, and nothing else.
	CHECK(project->GetConfig().InputActions == InputMap::GetDefaultUIActions());
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
	// UI actions saved before they existed are added; a UI action the project rebound keeps its inputs.
	CHECK(actions.at("ui_accept") == InputMap::GetDefaultUIActions().at("ui_accept"));
	Project::SetActive(nullptr);

	REQUIRE(FileSystem::WriteTextFile(folder / "Input.bproject", R"({
		"Name": "Input",
		"Input": { "Actions": { "ui_accept": ["Key:Space"] } }
	})"));
	project = Project::Load(folder / "Input.bproject");
	REQUIRE(project);
	CHECK(project->GetConfig().InputActions.at("ui_accept") == std::vector<InputBinding> { InputBinding::Key(KeyCode::Space) });
	CHECK(project->GetConfig().InputActions.count("ui_cancel") == 1);
	Project::SetActive(nullptr);
}

TEST_CASE("Export sanitizes names and refuses unsafe output folders")
{
	CHECK(ProjectExporter::SanitizeName("My Game!") == "My Game");
	CHECK(ProjectExporter::SanitizeName("../../evil") == "evil");
	CHECK(ProjectExporter::SanitizeName("???") == "Game");
	// Accented letters become plain ones instead of disappearing, and other characters still go.
	CHECK(ProjectExporter::SanitizeName("T\xC3\xABst") == "Test");
	CHECK(ProjectExporter::SanitizeName("Stra\xC3\x9F" "e \xC5\x81\xC3\xB3" "d\xC5\xBA") == "Strasse Lodz");
	CHECK(ProjectExporter::SanitizeName("\xF0\x9F\x8E\xAE Arcade") == "Arcade");
	CHECK(ProjectExporter::SanitizeName("Bad\xFF\xC3") == "Bad");
	// Windows' device names can't be folders or programs there.
	CHECK(ProjectExporter::SanitizeName("CON") == "CON Game");
	CHECK(ProjectExporter::SanitizeName("lpt1") == "lpt1 Game");
	CHECK(ProjectExporter::SanitizeName("Console") == "Console");

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

TEST_CASE("A new project's file is named after its cleaned name and stays in its folder")
{
	const std::filesystem::path root = BellowsTests::GetTestOutputDirectory() / "Projects" / "FileNames";
	std::error_code error;
	std::filesystem::remove_all(root, error);
	const std::filesystem::path directory = root / "A" / "B";

	Ref<Project> project = Project::Create(directory, "../../Escape");
	REQUIRE(project);
	CHECK(project->GetConfig().Name == "../../Escape");
	CHECK(project->GetProjectFilePath() == std::filesystem::absolute(directory) / "Escape.bproject");
	CHECK(std::filesystem::is_regular_file(directory / "Escape.bproject"));
	CHECK_FALSE(std::filesystem::exists(root / "Escape.bproject"));

	project = Project::Create(root / "Accents", "T\xC3\xABst: Part 2?");
	REQUIRE(project);
	CHECK(project->GetProjectFilePath().filename() == "Test Part 2.bproject");
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
	CHECK(FileSystem::ReadTextFile(options.OutputDirectory / "Bellows-LICENSE.txt").value_or("").find("Apache License") != std::string::npos);
	CHECK(FileSystem::ReadTextFile(options.OutputDirectory / "Bellows-NOTICE.txt").value_or("").find("Copyright 2026 MageCap Games") != std::string::npos);
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
