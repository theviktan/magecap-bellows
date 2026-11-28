#pragma once

#include <filesystem>
#include <string>

namespace ForgeScript {

	// Where 'use' looks for scripts, the same rule as the MageCap Bellows engine and editor (Sync Board
	// contract "use resolution"): inside a project, a folder with a .bproject file found by walking up
	// from the script, paths start at the project's asset folder ("AssetDirectory" in that file,
	// "Assets" when it is missing). Anywhere else they start next to the script.
	std::filesystem::path FindUseFolder(const std::filesystem::path& scriptPath);

	// The asset folder of the project a script is in (as FindUseFolder finds it), or an empty path when
	// the script isn't in a project. In a project every script shares the engine's events.
	std::filesystem::path FindProjectAssetFolder(const std::filesystem::path& scriptPath);

	// The file 'use "name"' means, from 'useFolder': ".forge" is added when the name has no extension.
	// Empty when 'use' may not load it (CheckUsePath), or when the file it reaches, following links, is
	// outside useFolder.
	std::filesystem::path ResolveUsePath(const std::filesystem::path& useFolder, const std::string& name);

	// Whether 'file', following links, is 'folder' or inside it.
	bool IsInsideFolder(const std::filesystem::path& folder, const std::filesystem::path& file);

	bool ReadTextFile(const std::filesystem::path& path, std::string& outText);

	// Paths and UTF-8 text. ForgeScript's text (scripts, URIs, JSON) is UTF-8, but path(std::string) and
	// path::string() use the ANSI code page on Windows, which breaks names such as C:\Users\José.
	std::filesystem::path PathFromUtf8(const std::string& text);
	std::string PathToUtf8(const std::filesystem::path& path);
	std::string GenericPathToUtf8(const std::filesystem::path& path);

}
