#include "ScriptCheckCommand.h"

#include "ProjectList.h"
#include "ScriptDocument.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <iostream>
#include <iterator>
#include <string>

namespace Bellows {

	int RunScriptCheckCommand(const ApplicationCommandLineArgs& args)
	{
		const std::string fileName = args.GetValue("--check-script");
		const bool json = args.Has("--json");
		if (fileName.empty())
		{
			std::cerr << "Usage: BellowsEditor --check-script <file.forge|file.py> [--json] [--stdin]" << std::endl;
			return 64;
		}
		const ScriptLanguage language = GetScriptLanguage(fileName);
		if (language == ScriptLanguage::None)
		{
			std::cerr << "'" << fileName << "' is not a script (.forge or .py)." << std::endl;
			return 64;
		}

		std::string source;
		if (args.Has("--stdin"))
		{
			source.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
		}
		else if (const std::optional<std::string> text = FileSystem::ReadTextFile(fileName))
		{
			source = *text;
		}
		else
		{
			std::cerr << "Could not open '" << fileName << "'." << std::endl;
			return 66;
		}

		// The engine logs nothing to the console here: stdout and stderr belong to the result.
		Log::Specification logSpecification;
		logSpecification.EnableConsole = false;
		Log::Init(logSpecification);

		// 'use "Scripts/Helpers"' is relative to the project's asset folder: find the project the
		// script belongs to (the nearest .bproject above it). Outside a project, the script's own folder is used.
		const std::filesystem::path scriptPath = std::filesystem::absolute(fileName).lexically_normal();
		std::filesystem::path assetDirectory = scriptPath.parent_path();
		for (std::filesystem::path folder = scriptPath.parent_path(); !folder.empty(); folder = folder.parent_path())
		{
			if (!FindProjects(folder, 0).empty())
			{
				if (Ref<Project> project = Project::Load(folder))
					assetDirectory = project->GetAssetDirectory();
				break;
			}
			if (folder == folder.parent_path())
				break;
		}
		// Checking a Python script only compiles it, which imports nothing, so its project's files
		// stay off Python's path: checking a script never runs any of the project's code.
		bool pythonReady = true;
		if (language == ScriptLanguage::Python)
		{
			AssetManager::SetAssetDirectory({});
			pythonReady = ScriptEngine::Init();
		}
		else
		{
			AssetManager::SetAssetDirectory(assetDirectory);
			SetForgeScriptCheckSetup(SetUpEngineForgeScriptCheck);
		}

		int exitCode = 0;
		if (!pythonReady)
		{
			std::cerr << "Python could not be started, so the script was not checked." << std::endl;
			exitCode = 70;
		}
		else
		{
			const std::vector<ScriptDiagnostic> diagnostics = CheckScript(language, source, fileName);
			if (json)
			{
				std::cout << FormatScriptDiagnosticsJson(fileName, source, diagnostics) << std::endl;
			}
			else
			{
				for (const ScriptDiagnostic& diagnostic : diagnostics)
					std::cerr << FormatScriptDiagnostic(fileName, diagnostic) << std::endl;
				if (diagnostics.empty())
					std::cout << fileName << ": no mistakes found." << std::endl;
			}
			exitCode = diagnostics.empty() ? 0 : 65;
		}

		SetForgeScriptCheckSetup(nullptr);
		Project::SetActive(nullptr);
		if (ScriptEngine::IsInitialized())
			ScriptEngine::Shutdown();
		Log::Shutdown();
		return exitCode;
	}

}
