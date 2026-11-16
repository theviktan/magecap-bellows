#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript { class VM; }

namespace Bellows {

	// Editor-side knowledge about script files: languages, templates, outlines and syntax checks.
	// Kept free of UI code so it can be unit-tested.

	enum class ScriptLanguage
	{
		None = 0,
		ForgeScript,
		Python
	};

	enum class ScriptTemplate
	{
		Default = 0,	// A ready-to-edit script with the common events
		Empty			// The smallest script that still attaches to an entity
	};

	// The language of a file, from its extension (.forge or .py). None for anything else.
	ScriptLanguage GetScriptLanguage(const std::filesystem::path& path);
	const char* GetScriptLanguageName(ScriptLanguage language);
	// ".forge" or ".py" (empty for None).
	const char* GetScriptExtension(ScriptLanguage language);

	// "my player 2" -> "MyPlayer2". Never empty and never starts with a digit.
	std::string MakeScriptClassName(std::string_view name);

	// The text of a new script. 'name' is the script's display name (usually the file stem).
	std::string MakeScriptSource(ScriptLanguage language, ScriptTemplate scriptTemplate, std::string_view name);

	// One navigable definition in a script, shown in the Script Editor's outline.
	struct ScriptOutlineItem
	{
		enum class Kind
		{
			Function = 0,	// ForgeScript "function", Python "def"
			Event,			// ForgeScript "when ..."
			Type,			// ForgeScript "kind", Python "class"
			Phrase,			// ForgeScript "phrase ..."
			Heading			// Markdown "# ..." in guides
		};

		Kind ItemKind = Kind::Function;
		std::string Name;
		size_t Line = 0;		// 1-based
		int Depth = 0;			// Indentation level: methods inside a kind or class are 1
	};

	std::vector<ScriptOutlineItem> ParseScriptOutline(ScriptLanguage language, std::string_view source);
	// The headings of a Markdown document (the guides opened from the Help menu). Depth is the level minus one.
	std::vector<ScriptOutlineItem> ParseMarkdownOutline(std::string_view source);

	// A mistake found without running the script.
	struct ScriptDiagnostic
	{
		std::string Message;
		size_t Line = 0;		// 1-based; 0 when unknown
		size_t Column = 0;		// 1-based; 0 when unknown
	};

	// "Scripts/Player.forge:12:5: message", the format of `forgescript --check` (tools parse it).
	std::string FormatScriptDiagnostic(const std::string& fileName, const ScriptDiagnostic& diagnostic);
	// The same mistakes as JSON for editors and tools:
	// { "file": ..., "diagnostics": [ { "line", "column", "endColumn", "severity", "message" } ] }.
	// endColumn is one past the last character of the line, since the checker reports where a mistake starts.
	std::string FormatScriptDiagnosticsJson(const std::string& fileName, std::string_view source, const std::vector<ScriptDiagnostic>& diagnostics);

	// Compiles the script without running it and reports its mistakes: every one ForgeScript can find
	// (sorted by line, including 'use' scripts that cannot be found), or Python's first.
	// Python needs the script engine to be initialized; without it Python scripts are not checked.
	std::vector<ScriptDiagnostic> CheckScript(ScriptLanguage language, std::string_view source, const std::string& fileName);

	// Called on the virtual machine used for checking ForgeScript, so it knows the engine's
	// functions and phrases. Set by whoever registers the engine API (by default: nothing extra).
	using ForgeScriptCheckSetup = void(*)(ForgeScript::VM& vm);
	void SetForgeScriptCheckSetup(ForgeScriptCheckSetup setup);

	// The setup the editor and `BellowsEditor --check-script` use: the engine API, and 'use' scripts
	// read from the project's asset folder (AssetManager's), exactly like the game does.
	void SetUpEngineForgeScriptCheck(ForgeScript::VM& vm);

}
