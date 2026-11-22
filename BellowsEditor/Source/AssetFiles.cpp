#include "AssetFiles.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/Platform.h"

#include <algorithm>
#include <system_error>

namespace Bellows::AssetFiles {

	namespace {

		// 'path' is the asset folder or something in it, after resolving "..", "." and links.
		bool IsInsideAssets(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path root = std::filesystem::weakly_canonical(AssetManager::GetAssetDirectory(), error);
			if (error || root.empty())
				return false;
			const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
			if (error)
				return false;
			return std::mismatch(root.begin(), root.end(), resolved.begin(), resolved.end()).first == root.end();
		}

		// Moves 'asset' to 'target' with its companion files, or changes nothing.
		bool MoveWithCompanions(const std::filesystem::path& asset, const std::filesystem::path& target, std::string& outError)
		{
			std::error_code error;
			if (!std::filesystem::exists(asset, error))
			{
				outError = "'" + asset.filename().string() + "' doesn't exist";
				return false;
			}
			if (!IsInsideAssets(asset) || !IsInsideAssets(target))
			{
				outError = "assets stay in the project's asset folder";
				return false;
			}
			std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves = { { asset, target } };
			for (const std::filesystem::path& companion : GetCompanionFiles(asset))
				moves.emplace_back(companion, MeshImportSettings::GetFilePath(target));
			for (const auto& [from, to] : moves)
			{
				if (std::filesystem::exists(to, error))
				{
					outError = "'" + to.filename().string() + "' already exists";
					return false;
				}
			}

			for (size_t i = 0; i < moves.size(); i++)
			{
				std::filesystem::rename(moves[i].first, moves[i].second, error);
				if (error)
				{
					outError = error.message();
					// Put back what moved, so the asset and its settings stay together.
					for (size_t undo = i; undo-- > 0;)
					{
						std::error_code ignored;
						std::filesystem::rename(moves[undo].second, moves[undo].first, ignored);
					}
					return false;
				}
			}
			return true;
		}

	}

	std::vector<std::filesystem::path> GetCompanionFiles(const std::filesystem::path& asset)
	{
		std::vector<std::filesystem::path> companions;
		std::error_code error;
		if (std::filesystem::is_regular_file(asset, error))
		{
			const std::filesystem::path settings = MeshImportSettings::GetFilePath(asset);
			if (std::filesystem::is_regular_file(settings, error))
				companions.push_back(settings);
		}
		return companions;
	}

	bool IsCompanionFile(const std::filesystem::path& path)
	{
		return path.extension() == ".bimport";
	}

	std::optional<std::string> CheckNewName(const std::string& name)
	{
		if (name.empty() || name == "." || name == "..")
			return "a name can't be empty, '.' or '..'";
		// No folders (either slash) and nothing Windows can't have in a file name, so a project stays
		// usable on both systems.
		if (name.find_first_of("/\\:*?\"<>|") != std::string::npos)
			return "a name can't have / \\ : * ? \" < > or | in it";
		if (std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 32; }))
			return "a name can't have control characters in it";
		if (name.back() == ' ' || name.back() == '.')
			return "a name can't end with a space or a dot";
		return std::nullopt;
	}

	bool Rename(const std::filesystem::path& asset, const std::string& newName, std::string& outError)
	{
		if (const std::optional<std::string> problem = CheckNewName(newName))
		{
			outError = *problem;
			return false;
		}
		const std::filesystem::path target = asset.parent_path() / std::filesystem::path(reinterpret_cast<const char8_t*>(newName.c_str()));
		if (target == asset)
			return true;
		return MoveWithCompanions(asset, target, outError);
	}

	bool Move(const std::filesystem::path& asset, const std::filesystem::path& folder, std::string& outError)
	{
		std::error_code error;
		if (!std::filesystem::is_directory(folder, error))
		{
			outError = "'" + folder.filename().string() + "' isn't a folder";
			return false;
		}
		// A folder can't go inside itself.
		const std::filesystem::path resolvedAsset = std::filesystem::weakly_canonical(asset, error);
		const std::filesystem::path resolvedFolder = std::filesystem::weakly_canonical(folder, error);
		if (std::mismatch(resolvedAsset.begin(), resolvedAsset.end(), resolvedFolder.begin(), resolvedFolder.end()).first == resolvedAsset.end())
		{
			outError = "a folder can't go inside itself";
			return false;
		}
		return MoveWithCompanions(asset, folder / asset.filename(), outError);
	}

	bool Delete(const std::filesystem::path& asset, std::string& outError)
	{
		std::error_code error;
		if (!IsInsideAssets(asset) || std::filesystem::equivalent(asset, AssetManager::GetAssetDirectory(), error))
		{
			outError = "only assets in the project's asset folder can be deleted";
			return false;
		}
		const std::vector<std::filesystem::path> companions = GetCompanionFiles(asset);
		if (!Platform::MoveToTrash(asset, outError))
			return false;
		for (const std::filesystem::path& companion : companions)
		{
			std::string companionError;
			if (!Platform::MoveToTrash(companion, companionError))
				outError = "'" + companion.filename().string() + "' stayed: " + companionError;
		}
		return true;
	}

}
