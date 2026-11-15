#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "TestFramework.h"

namespace ForgeTest {

	ScriptRun RunScript(const std::string& source, const RunOptions& options)
	{
		ForgeScript::VMConfig config;
		if (options.StressGC)
			config.InitialGCThreshold = 1;

		ScriptRun run;
		{
			ForgeScript::VM vm(config);
			vm.SetPrintHandler([&run](const std::string& text) { run.Output += text + "\n"; });
			vm.SetErrorHandler([&run](const ForgeScript::ScriptError& error) { run.Errors.push_back(error.ToString()); });
			if (options.Setup)
				options.Setup(vm);

			ForgeScript::LoadResult result = vm.Load(source, "test.forge");
			run.Success = result.Success;
			for (double time = 0; time < options.SimulateSeconds && vm.GetTaskCount() > 0; time += 1.0 / 60.0)
				vm.Update(1.0 / 60.0);
		}
		return run;
	}

}
