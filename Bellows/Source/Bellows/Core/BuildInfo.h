#pragma once

#include <string>

namespace Bellows::BuildInfo {

	// The short git commit this build was made from (for example "3f8b735"), or "unknown" for a build
	// outside a git checkout. Tells two builds of the same version apart.
	const char* GetCommit();

	// The engine version with its commit, as shown to people: "0.4.0 (3f8b735)" for the release, and
	// "0.4.0-dev (3f8b735)" for builds on the way to it (BW_VERSION is the version part).
	std::string GetVersionText();

}
