#pragma once

#include "Bellows/Scripting/ScriptApi.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Bellows::Scripting {

	// Game data files that scripts read from the project's Assets folder: item lists, dialogue, level
	// tables (load_json, load_text, ForgeScript's 'the data in'). Unlike save_data, they are read-only
	// and ship with the game: Export Game copies the whole Assets folder.
	//
	// A path is relative to the Assets folder, with / or \ ("Data/items.json"). Full paths, '..', and
	// links that lead outside the folder are refused, so a script can only read the game's own files.
	// A file may be at most MaxDataFileBytes, and JSON nested at most MaxJsonDepth deep. A file is read
	// again only when it changes on disk. Mistakes raise ScriptValueError naming the file (and the
	// line, for JSON).
	constexpr uintmax_t MaxDataFileBytes = 16 * 1024 * 1024;
	constexpr int MaxJsonDepth = 256;

	// The file a data path names, checked as above.
	std::filesystem::path ResolveDataFile(const std::string& path);

	// The file's text (a UTF-8 byte order mark is left out).
	std::string LoadDataText(const std::string& path);

	// The file read as JSON: objects become maps (in file order), arrays lists, and null nothing.
	ScriptValue LoadDataJson(const std::string& path);

	// Forgets every file read so far (the next read goes to disk).
	void ClearDataFileCache();

	// About how much memory the files read so far take: their text and the values read from it.
	// It stays at most MaxDataFileCacheBytes.
	constexpr uintmax_t MaxDataFileCacheBytes = 64 * 1024 * 1024;
	uintmax_t GetDataFileCacheBytes();

}
