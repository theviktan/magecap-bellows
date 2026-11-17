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
//   float                     -> the number its shortest text means (0.1f is 0.1, see ForgeScriptNumber)

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

	// An engine float as a ForgeScript number: the number its shortest decimal text means, so a float
	// that holds 0.1 reads as 0.1 in scripts ('say', comparisons, the debugger) and not as its exact
	// value 0.100000001490116. The float a script gets back from it is the same float.
	double ForgeScriptNumber(float value);

	// The ForgeScript value for an entity (what an entity script gets as 'self').
	ForgeScript::Value MakeForgeScriptEntity(ForgeScript::VM& vm, const ScriptEntity& entity);

	// Script event values (see EmitScriptEvent). A ForgeScript value that other languages cannot read
	// (a function, a kind's object, a map with non-text keys, lists nested more than 32 deep) becomes
	// nothing, and a description of it is added to 'outUnsendable'.
	ScriptValue ToScriptValue(ForgeScript::VM& vm, const ForgeScript::Value& value, std::vector<std::string>& outUnsendable);
	ForgeScript::Value FromScriptValue(ForgeScript::VM& vm, const ScriptValue& value);

}
