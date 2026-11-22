#pragma once

// ForgeScript — include this one header to embed ForgeScript in your engine.

#include "ForgeScript/API/Native.h"
#include "ForgeScript/Compiler/Compiler.h"
#include "ForgeScript/Core/Object.h"
#include "ForgeScript/Core/Value.h"
#include "ForgeScript/VM/VM.h"

// The language's version, the one place it is set (CMake reads it from here). The minor number goes
// up with new language or tool features, the patch number with fixes; CHANGELOG.md lists each version.
// forgescript --version, the language server's serverInfo and MageCap Bellows' Install.json show it.
#ifndef FORGESCRIPT_VERSION
	#define FORGESCRIPT_VERSION "1.7.0"
#endif
