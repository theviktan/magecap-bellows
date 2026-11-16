#pragma once

// The engine API for ForgeScript. The same API description that builds the Python module
// (ScriptApiDefinition.h) is turned into ForgeScript types and functions here, with the same names:
// entity.transform.position, rigid_body.add_force(...), create_entity("Box") and so on.
//
// How values look in ForgeScript:
//   Vec3                      -> vector            Vec2 -> vector with z = 0
//   colors                    -> a list [r, g, b, a] (a list of 3 numbers or a vector also works)
//   Quat                      -> a list [w, x, y, z]
//   entity ids (64-bit)       -> text, because numbers cannot hold every 64-bit id exactly
//   keys and mouse buttons    -> names: "W", "space", "left"
//   None / optional values    -> nothing

#include "Bellows/Scripting/ScriptApi.h"

#include "ForgeScript/ForgeScript.h"

#include <string>
#include <vector>

namespace Bellows::Scripting {

	// An engine phrase: English words for a common engine call, registered for ForgeScript only, next to
	// the shared API (save screenshot to "bug.png" means save_screenshot("bug.png")).
	struct ForgeScriptPhraseInfo
	{
		std::string Pattern;    // words, with each value in parentheses: "save screenshot to (path)"
		std::string Doc;
		std::string Returns;    // the type of what it gives back, like the API description's "returns"
	};

	// Every engine phrase RegisterForgeScriptApi adds, for tools (forgescript-api.json's "phrases").
	std::vector<ForgeScriptPhraseInfo> GetForgeScriptPhrases();

	// Adds every engine function, class and phrase to a ForgeScript virtual machine. Call once per VM.
	void RegisterForgeScriptApi(ForgeScript::VM& vm);

	// The ForgeScript value for an entity (what an entity script gets as 'self').
	ForgeScript::Value MakeForgeScriptEntity(ForgeScript::VM& vm, const ScriptEntity& entity);

}
