#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Scripting/ScriptEngine.h"

int main(int argc, char** argv)
{
	Bellows::Log::Specification logSpecification;
	logSpecification.EnableConsole = std::getenv("BELLOWS_TEST_VERBOSE") != nullptr;
	logSpecification.LogFilePath = "BellowsTests.log";
	Bellows::Log::Init(logSpecification);
	Bellows::Log::SetBreakOnAssert(false);

	// Audio (silent null device) and Python live for the whole run; the shared GPU application reuses them.
	Bellows::AudioEngine::Init(true);
	if (!Bellows::ScriptEngine::Init())
	{
		fprintf(stderr, "Failed to start Python; see BellowsTests.log\n");
		return 1;
	}

	doctest::Context context;
	context.applyCommandLine(argc, argv);
	const int result = context.run();

	BellowsTests::ShutdownTestApplication();
	Bellows::ScriptEngine::Shutdown();
	Bellows::AudioEngine::Shutdown();
	Bellows::Log::Shutdown();
	return result;
}
