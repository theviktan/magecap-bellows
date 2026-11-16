#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript {

	// A script variable set to a plain value at the top of a script, like
	//     set speed to 2.5
	// Hosts show these as editable settings (the Bellows Inspector does) without running the script.
	struct ScriptVariable
	{
		enum class ValueKind
		{
			Number,       // 2.5
			WholeNumber,  // 3     (written without a decimal point)
			Text,         // "Hero"
			Boolean,      // true, false, yes, no
			Vector2,      // vector(1, 2)
			Vector3,      // vector(1, 2, 3)
			Color         // [1, 0.5, 0] or [1, 0.5, 0, 1]
		};

		std::string Name;
		ValueKind Kind = ValueKind::Number;
		double Number = 0.0;
		bool Boolean = false;
		std::string Text;
		double Components[4] = { 0.0, 0.0, 0.0, 1.0 };
		int Line = 0;
	};

	// Finds every "set name to <plain value>" line at the top level of a script (not inside functions,
	// loops or other blocks), in order. Names starting with '_' are private and left out; when a name
	// is set more than once, the first line counts. Does not check the rest of the script for mistakes.
	std::vector<ScriptVariable> FindScriptVariables(std::string_view source);

}
