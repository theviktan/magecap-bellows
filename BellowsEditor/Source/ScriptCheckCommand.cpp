#include "ScriptCheckCommand.h"

#include "ScriptDocument.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
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

		bool pythonReady = true;
		if (language == ScriptLanguage::Python)
			pythonReady = ScriptEngine::Init();
		else
			SetForgeScriptCheckSetup(Scripting::RegisterForgeScriptApi);

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
		if (ScriptEngine::IsInitialized())
			ScriptEngine::Shutdown();
		Log::Shutdown();
		return exitCode;
	}

}
