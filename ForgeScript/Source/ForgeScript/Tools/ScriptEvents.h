#pragma once

#include <set>
#include <string>
#include <vector>

namespace ForgeScript {

	// One 'trigger' line's event: its name, and where the name is written (1-based, on one line, the end
	// not included), so a checker can point at it and replace it.
	struct EventTrigger
	{
		std::string Name;
		int Line = 0;
		int Column = 0;
		int EndColumn = 0;
		bool Quoted = false; // written as text, 'trigger "door opened"'
	};

	// The events a script listens for ('when door opened', 'when "Start" is clicked', 'wait for hit') and
	// the ones it triggers, read the way the compiler reads event names: the text in quotes, or the
	// words up to 'with', 'by', 'then' or 'do'. 'when my health changes' and the other field handlers
	// inside a kind aren't events, so they're left out. Works on any text, mistakes and all.
	struct ScriptEvents
	{
		std::set<std::string> Listened;
		std::vector<EventTrigger> Triggers;
	};

	ScriptEvents FindScriptEvents(const std::string& source);

	// Every piece of quoted text in a Python script, for the events its on_event may compare names with.
	std::set<std::string> FindPythonTexts(const std::string& source);

}
