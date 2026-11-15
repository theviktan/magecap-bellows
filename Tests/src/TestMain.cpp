#include "TestFramework.h"

#include <cstring>
#include <iostream>

namespace ForgeTest {

	static int s_Failures = 0;
	static bool s_CurrentFailed = false;

	std::vector<TestCase>& Registry()
	{
		static std::vector<TestCase> s_Registry;
		return s_Registry;
	}

	void ReportFailure(const char* file, int line, const std::string& message)
	{
		s_Failures++;
		s_CurrentFailed = true;
		std::cout << "    FAILED " << file << ":" << line << "\n      " << message << std::endl;
	}

	ScriptRun RunScript(const std::string& source, const RunOptions& options)
	{
		Forge::VMConfig config;
		if (options.StressGC)
			config.InitialGCThreshold = 1;

		ScriptRun run;
		{
			Forge::VM vm(config);
			vm.SetPrintHandler([&run](const std::string& text) { run.Output += text + "\n"; });
			vm.SetErrorHandler([&run](const Forge::ScriptError& error) { run.Errors.push_back(error.ToString()); });
			if (options.Setup)
				options.Setup(vm);

			Forge::LoadResult result = vm.Load(source, "test.forge");
			run.Success = result.Success;
			for (double time = 0; time < options.SimulateSeconds && vm.GetTaskCount() > 0; time += 1.0 / 60.0)
				vm.Update(1.0 / 60.0);
		}
		return run;
	}

}

int main(int argc, char** argv)
{
	const char* filter = argc > 1 ? argv[1] : nullptr;
	int run = 0;
	int failedTests = 0;

	for (const ForgeTest::TestCase& test : ForgeTest::Registry())
	{
		if (filter && std::strstr(test.Name, filter) == nullptr)
			continue;
		ForgeTest::s_CurrentFailed = false;
		std::cout << "[ RUN  ] " << test.Name << std::endl;
		try
		{
			test.Fn();
		}
		catch (const std::exception& error)
		{
			ForgeTest::ReportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + error.what());
		}
		run++;
		if (ForgeTest::s_CurrentFailed)
			failedTests++;
		std::cout << (ForgeTest::s_CurrentFailed ? "[ FAIL ] " : "[  OK  ] ") << test.Name << std::endl;
	}

	std::cout << "\n" << run - failedTests << " of " << run << " tests passed";
	if (ForgeTest::s_Failures > 0)
		std::cout << " (" << ForgeTest::s_Failures << " failed checks)";
	std::cout << "." << std::endl;
	return failedTests == 0 ? 0 : 1;
}
