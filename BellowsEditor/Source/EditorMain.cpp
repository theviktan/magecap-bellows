// MageCap Bellows editor.
//
// Usage: BellowsEditor [--project <path>] [--run-script <file.py>] [--exit] [--headless]
//                      [--width N] [--height N] [--frames N] [--no-agent-server] [--agent-port N]
//                      [--validation]
//
//   --project          Opens a project (.bproject file or its folder) instead of the project hub.
//   --run-script       Runs a Python automation script once the editor is ready (bellows.editor API).
//   --exit             Closes the editor after --run-script; the exit code reports success (0) or failure (1).
//   --headless         Runs without a window (rendering still happens offscreen); implies a fixed timestep.
//   --no-agent-server  Disables the localhost automation server for this session.

#include "EditorLayer.h"
#include "EditorScriptAPI.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <cstdio>
#include <string>

using namespace Bellows;

namespace {

	void PrintUsage()
	{
		std::printf(
			"MageCap Bellows Editor " BW_VERSION "\n\n"
			"Usage: BellowsEditor [options]\n"
			"  --project <path>       Open a project (.bproject file or folder)\n"
			"  --run-script <file>    Run a Python automation script when the editor is ready\n"
			"  --exit                 Exit after the script (exit code 0 = success)\n"
			"  --headless             Run without a window\n"
			"  --width <n>            Window or offscreen width (default 1600)\n"
			"  --height <n>           Window or offscreen height (default 900)\n"
			"  --frames <n>           Stop after n frames\n"
			"  --no-agent-server      Disable the automation server\n"
			"  --agent-port <n>       Automation server port (default 7420)\n"
			"  --validation           Enable Vulkan validation layers\n");
	}

	uint32_t ParseUnsigned(const ApplicationCommandLineArgs& args, const char* name, uint32_t fallback)
	{
		const std::string value = args.GetValue(name);
		if (value.empty())
			return fallback;
		try
		{
			return static_cast<uint32_t>(std::stoul(value));
		}
		catch (const std::exception&)
		{
			BW_CORE_WARN("Ignoring invalid value '{}' for {}", value, name);
			return fallback;
		}
	}

}

int main(int argc, char** argv)
{
	const ApplicationCommandLineArgs args = ApplicationCommandLineArgs::FromArgv(argc, argv);
	if (args.Has("--help") || args.Has("-h"))
	{
		PrintUsage();
		return 0;
	}

	Log::Specification logSpecification;
	logSpecification.LogFilePath = (EditorSettings::GetConfigDirectory() / "Editor.log").string();
	Log::Init(logSpecification);
	BW_CORE_INFO("MageCap Bellows Editor {}", BW_VERSION);

	EditorOptions options;
	options.ProjectPath = args.GetValue("--project");
	options.StartupScript = args.GetValue("--run-script");
	options.ExitAfterScript = args.Has("--exit");
	options.Headless = args.Has("--headless");
	options.DisableAgentServer = args.Has("--no-agent-server");
	options.AgentServerPort = static_cast<uint16_t>(ParseUnsigned(args, "--agent-port", 0));
	if (!options.StartupScript.empty())
		options.StartupScript = std::filesystem::absolute(options.StartupScript);

	ApplicationSpecification specification;
	specification.Name = "MageCap Bellows Editor";
	specification.CommandLineArgs = args;
	specification.Window.Title = "MageCap Bellows";
	specification.Window.Width = ParseUnsigned(args, "--width", 1600);
	specification.Window.Height = ParseUnsigned(args, "--height", 900);
	specification.Window.StartMaximized = !args.Has("--width") && !args.Has("--height");
	specification.Headless = options.Headless;
	specification.HeadlessWidth = specification.Window.Width;
	specification.HeadlessHeight = specification.Window.Height;
	specification.EnableValidation = args.Has("--validation");
	specification.MaxFrames = ParseUnsigned(args, "--frames", 0);
	if (specification.Headless)
		specification.FixedTimestep = 1.0f / 60.0f;

	int exitCode = 1;
	{
		Application application(specification);
		if (application.IsValid())
		{
			RegisterEditorScriptAPI();
			std::string output;
			if (!ScriptEngine::ExecuteSource("import bellows.editor", output, "<editor>"))
				BW_CORE_ERROR("Could not load the bellows.editor module: {}", output);

			application.PushLayer(new EditorLayer(options));
			exitCode = application.Run();
		}
	}

	Log::Shutdown();
	return exitCode;
}
