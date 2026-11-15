#include "TestUtilities.h"

#include "Bellows/Core/Platform.h"

#include <memory>

namespace BellowsTests {

	namespace {

		std::unique_ptr<Bellows::Application> s_Application;

	}

	Bellows::Application& GetTestApplication()
	{
		if (!s_Application)
		{
			Bellows::ApplicationSpecification specification;
			specification.Name = "BellowsTests";
			specification.Headless = true;
			specification.HeadlessWidth = 640;
			specification.HeadlessHeight = 360;
			specification.EnableValidation = true;
			specification.EnableImGui = false;
			specification.FixedTimestep = 1.0f / 60.0f;
			s_Application = std::make_unique<Bellows::Application>(specification);
		}
		return *s_Application;
	}

	void ShutdownTestApplication()
	{
		s_Application.reset();
	}

	std::filesystem::path GetTestOutputDirectory()
	{
		static const std::filesystem::path s_Directory = []
		{
			std::filesystem::path directory = Bellows::Platform::GetExecutableDirectory() / "TestOutput";
			std::error_code error;
			std::filesystem::remove_all(directory, error);
			std::filesystem::create_directories(directory, error);
			return directory;
		}();
		return s_Directory;
	}

	std::filesystem::path GetSourceDirectory()
	{
		return std::filesystem::path(BELLOWS_SOURCE_DIR);
	}

}
