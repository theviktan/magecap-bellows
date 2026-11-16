#pragma once

#include "Bellows/Core/Application.h"

namespace Bellows {

	// BellowsEditor --check-script <file> [--json] [--stdin]
	//
	// Checks a ForgeScript or Python script for mistakes the way the Script Editor does - ForgeScript
	// with the engine's functions and phrases known - without opening a window. For editors and
	// tools such as MageCap Forge.
	//   Mistakes go to stderr as "file:line:column: message" (the format of `forgescript --check`),
	//   or to stdout as JSON with --json (see FormatScriptDiagnosticsJson).
	//   --stdin reads the script text from standard input; <file> still names it, so unsaved text
	//   can be checked.
	//   'use' scripts are read from the Assets folder of the project above <file> (nearest .bproject).
	// Exit codes: 0 no mistakes, 65 mistakes found, 66 the file could not be read, 64 bad usage.
	int RunScriptCheckCommand(const ApplicationCommandLineArgs& args);

}
