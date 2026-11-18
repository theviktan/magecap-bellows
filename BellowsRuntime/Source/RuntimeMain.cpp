// MageCap Bellows runtime: plays an exported (or any) project without editing features.
//
// Usage: <Game> [--project <path>] [--scene <asset path>] [--headless] [--width N] [--height N]
//               [--frames N] [--screenshot <file.png>] [--validation]
//               [--scene-file <path>] [--title <text>] [--editor]
//
// The last three are what the editor's Play in a game window passes: the scene being edited (saved
// to a file outside the assets, so unsaved changes play), the window title, and --editor: a console
// log with levels that the editor reads into its Console (even in shipping builds), and F8 to stop.

#include "RuntimeLayer.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Scripting/ScriptEngine.h"

using namespace Bellows;

int main(int argc, char** argv)
{
	const ApplicationCommandLineArgs args = ApplicationCommandLineArgs::FromArgv(argc, argv);

	Log::Specification logSpecification;
	logSpecification.LogFilePath = (Platform::GetExecutableDirectory() / "Game.log").string();
#ifdef BW_DIST
	// Shipped games log to Game.log only; headless (automated) runs also print to the console.
	logSpecification.EnableConsole = args.Has("--headless");
#endif
	if (args.Has("--editor"))
	{
		logSpecification.EnableConsole = true;
		logSpecification.ConsoleIncludesLevel = true;
	}
	Log::Init(logSpecification);

	// The project file sits next to the executable in exported builds.
	std::filesystem::path projectPath = args.GetValue("--project");
	if (projectPath.empty())
		projectPath = Platform::GetExecutableDirectory();

	Ref<Project> project = Project::Load(projectPath);
	if (!project)
	{
		BW_CORE_CRITICAL("No project found (looked in '{}')", projectPath.string());
		Log::Shutdown();
		return 1;
	}

	const ProjectConfig& config = project->GetConfig();
	ApplicationSpecification specification;
	specification.Name = config.Name;
	specification.CommandLineArgs = args;
	specification.Window.Title = args.GetValue("--title", config.Name);
	// Next to the engine's programs; an exported game has no Resources folder and keeps the program's icon.
	specification.Window.IconPath = (Platform::GetExecutableDirectory() / "Resources" / "Icons" / "Bellows.png").string();
	specification.Window.Width = static_cast<uint32_t>(std::stoul(args.GetValue("--width", std::to_string(config.WindowWidth))));
	specification.Window.Height = static_cast<uint32_t>(std::stoul(args.GetValue("--height", std::to_string(config.WindowHeight))));
	specification.Window.Fullscreen = config.Fullscreen && !args.Has("--windowed");
	specification.VSync = config.VSync;
	specification.Headless = args.Has("--headless");
	specification.HeadlessWidth = specification.Window.Width;
	specification.HeadlessHeight = specification.Window.Height;
	specification.EnableValidation = args.Has("--validation");
	specification.EnableImGui = false;
	specification.MaxFrames = std::stoull(args.GetValue("--frames", "0"));
	if (specification.Headless)
		specification.FixedTimestep = 1.0f / 60.0f;

	RuntimeOptions options;
	options.SceneOverride = args.GetValue("--scene");
	options.SceneFile = args.GetValue("--scene-file");
	options.StartedByEditor = args.Has("--editor");
	options.ScreenshotPath = args.GetValue("--screenshot");
	if (!options.ScreenshotPath.empty())
		options.ScreenshotFrame = specification.MaxFrames > 0 ? specification.MaxFrames : 1;

	int exitCode = 1;
	{
		Application application(specification);
		if (application.IsValid())
		{
			application.PushLayer(new RuntimeLayer(project, options));
			exitCode = application.Run();
			// Automated runs fail when scripts raised errors.
			if (exitCode == 0 && specification.Headless && ScriptEngine::GetErrorCount() > 0)
			{
				BW_CORE_ERROR("{} script error(s) occurred", ScriptEngine::GetErrorCount());
				exitCode = 2;
			}
		}
	}

	Project::SetActive(nullptr);
	Log::Shutdown();
	return exitCode;
}
