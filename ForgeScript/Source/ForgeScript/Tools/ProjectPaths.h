#pragma once

#include <filesystem>
#include <string>

namespace ForgeScript {

	// Where 'use' looks for scripts, the same rule as the MageCap Bellows engine and editor (Sync Board
	// contract "use resolution"): inside a project, a folder with a .bproject file found by walking up
	// from the script, paths start at the project's asset folder ("AssetDirectory" in that file,
	// "Assets" when it is missing). Anywhere else they start next to the script.
	std::filesystem::path FindUseFolder(const std::filesystem::path& scriptPath);

	// The file 'use "name"' means, from 'useFolder': ".forge" is added when the name has no extension.
	std::filesystem::path ResolveUsePath(const std::filesystem::path& useFolder, const std::string& name);

	bool ReadTextFile(const std::filesystem::path& path, std::string& outText);

	// Paths and UTF-8 text. ForgeScript's text (scripts, URIs, JSON) is UTF-8, but path(std::string) and
	// path::string() use the ANSI code page on Windows, which breaks names such as C:\Users\José.
	std::filesystem::path PathFromUtf8(const std::string& text);
	std::string PathToUtf8(const std::filesystem::path& path);
	std::string GenericPathToUtf8(const std::filesystem::path& path);

}
