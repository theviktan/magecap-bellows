#pragma once

namespace Bellows {

	// Registers the '_bellows.editor' native module (wrapped by Python/bellows/editor.py), which
	// exposes the editor to Python: the console, automation scripts and AI agents.
	void RegisterEditorScriptAPI();

}
