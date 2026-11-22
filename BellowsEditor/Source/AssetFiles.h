#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Bellows {

	// What the Content panel does to asset files: rename, move and delete. Each keeps the asset's
	// companion files with it (a model's .bimport import settings), keeps every file inside the
	// project's asset folder, and deletes to the Recycle Bin or Trash. UI-free, so it is unit tested.
	namespace AssetFiles {

		// Files that belong to an asset and go wherever it goes: today, a model's .bimport.
		std::vector<std::filesystem::path> GetCompanionFiles(const std::filesystem::path& asset);
		// True for files the Content panel doesn't list because they travel with another asset.
		bool IsCompanionFile(const std::filesystem::path& path);

		// Why a new name for an asset can't be used, or nothing when it can. A name has no folder in it:
		// renaming never moves an asset to another folder (or out of the project).
		std::optional<std::string> CheckNewName(const std::string& name);

		// Each returns false and sets 'outError' to why when it changes nothing.
		bool Rename(const std::filesystem::path& asset, const std::string& newName, std::string& outError);
		// Moves an asset (a file or a folder) into another folder of the project's assets.
		bool Move(const std::filesystem::path& asset, const std::filesystem::path& folder, std::string& outError);
		// Moves an asset and its companion files to the Recycle Bin or Trash. When the asset went but a
		// companion file couldn't, it returns true with 'outError' saying which stayed.
		bool Delete(const std::filesystem::path& asset, std::string& outError);

	}

}
