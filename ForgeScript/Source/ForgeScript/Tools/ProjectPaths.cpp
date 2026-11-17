#include "ForgeScript/Tools/ProjectPaths.h"

#include "ForgeScript/Tools/Json.h"

#include <fstream>
#include <sstream>

namespace ForgeScript {

	namespace {

		std::string ReadAssetDirectory(const std::filesystem::path& projectFile)
		{
			std::string text;
			Json project;
			std::string error;
			if (!ReadTextFile(projectFile, text) || !Json::Parse(text, project, error))
				return "Assets";
			const std::string& folder = project.GetString("AssetDirectory");
			return folder.empty() ? std::string("Assets") : folder;
		}

	}

	std::filesystem::path FindUseFolder(const std::filesystem::path& scriptPath)
	{
		std::error_code error;
		const std::filesystem::path script = std::filesystem::absolute(scriptPath, error).lexically_normal();
		const std::filesystem::path scriptFolder = script.parent_path();
		for (std::filesystem::path folder = scriptFolder; !folder.empty(); folder = folder.parent_path())
		{
			for (const auto& entry : std::filesystem::directory_iterator(folder, error))
			{
				if (entry.path().extension() == ".bproject")
					return folder / ReadAssetDirectory(entry.path());
			}
			if (folder == folder.parent_path())
				break;
		}
		return scriptFolder.empty() ? std::filesystem::path(".") : scriptFolder;
	}

	std::filesystem::path ResolveUsePath(const std::filesystem::path& useFolder, const std::string& name)
	{
		std::filesystem::path candidate = useFolder / name;
		if (!candidate.has_extension())
			candidate += ".forge";
		return candidate;
	}

	bool ReadTextFile(const std::filesystem::path& path, std::string& outText)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		std::stringstream buffer;
		buffer << file.rdbuf();
		outText = buffer.str();
		return true;
	}

}
