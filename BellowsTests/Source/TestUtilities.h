#pragma once

#include "Bellows/Core/Application.h"

#include <filesystem>

namespace BellowsTests {

	// Shared headless application for GPU tests ([gpu] test cases). Created on first use.
	Bellows::Application& GetTestApplication();
	void ShutdownTestApplication();

	// Folder for test output (screenshots, temporary projects). Cleared once per test run.
	std::filesystem::path GetTestOutputDirectory();

	// Root of the source repository (for test assets).
	std::filesystem::path GetSourceDirectory();

}
