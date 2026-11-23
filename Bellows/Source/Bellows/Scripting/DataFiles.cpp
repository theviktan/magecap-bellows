#include "bwpch.h"
#include "Bellows/Scripting/DataFiles.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Bellows::Scripting {

	namespace {

		struct CachedFile
		{
			std::filesystem::file_time_type WriteTime;
			uintmax_t Size = 0;
			std::string Text;
			std::optional<ScriptValue> Json;	// Filled the first time the file is read as JSON
			uintmax_t JsonBytes = 0;			// About how much memory Json takes
			uintmax_t GetBytes() const { return Text.size() + JsonBytes; }
		};

		// Keyed by the file's full path; the text and the values read from it both count. Dropped whole
		// when it grows past MaxDataFileCacheBytes, which only a game reading many large files at once reaches.
		std::unordered_map<std::string, CachedFile> s_Cache;
		uintmax_t s_CachedBytes = 0;
		std::unordered_set<std::string> s_UncachedWarned;	// Files too big to keep, warned about once

		const char* c_Example = "like \"Data/items.json\"";

		bool IsInside(const std::filesystem::path& path, const std::filesystem::path& folder)
		{
			auto pathIt = path.begin();
			for (auto folderIt = folder.begin(); folderIt != folder.end(); ++folderIt, ++pathIt)
			{
				if (pathIt == path.end() || *pathIt != *folderIt)
					return false;
			}
			return true;
		}

		CachedFile& ReadFile(const std::string& path)
		{
			const std::filesystem::path file = ResolveDataFile(path);
			// Each call gets its own error, so a failed size can't pass for a huge file.
			std::error_code sizeError;
			std::error_code timeError;
			const uintmax_t size = std::filesystem::file_size(file, sizeError);
			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(file, timeError);
			if (sizeError || timeError)
				throw ScriptValueError("Can't read the data file '" + path + "': " + (sizeError ? sizeError : timeError).message());
			if (size > MaxDataFileBytes)
				throw ScriptValueError("The data file '" + path + "' is " + std::to_string(size / (1024 * 1024)) + " MB; data files can be at most 16 MB");

			const std::string key = file.generic_string();
			if (auto it = s_Cache.find(key); it != s_Cache.end() && it->second.WriteTime == writeTime && it->second.Size == size)
				return it->second;

			std::ifstream stream(file, std::ios::binary);
			std::string text(static_cast<size_t>(size), '\0');
			if (!stream || !stream.read(text.data(), static_cast<std::streamsize>(size)))
				throw ScriptValueError("Can't read the data file '" + path + "'");
			if (text.starts_with("\xEF\xBB\xBF"))
				text.erase(0, 3);

			if (auto it = s_Cache.find(key); it != s_Cache.end())
			{
				s_CachedBytes -= it->second.GetBytes();
				s_Cache.erase(it);
			}
			if (s_CachedBytes + text.size() > MaxDataFileCacheBytes)
			{
				s_Cache.clear();
				s_CachedBytes = 0;
			}
			s_CachedBytes += text.size();
			return s_Cache[key] = CachedFile { writeTime, size, std::move(text), std::nullopt };
		}

		// The memory a value read from JSON takes, near enough to keep the cache's size honest.
		uintmax_t EstimateBytes(const ScriptValue& value)
		{
			uintmax_t bytes = sizeof(ScriptValue);
			if (const std::string* text = std::get_if<std::string>(&value.Data))
				bytes += text->capacity();
			else if (const ScriptValueList* items = std::get_if<ScriptValueList>(&value.Data))
			{
				for (const ScriptValue& item : *items)
					bytes += EstimateBytes(item);
			}
			else if (const ScriptValueMap* entries = std::get_if<ScriptValueMap>(&value.Data))
			{
				for (const auto& [key, item] : *entries)
					bytes += sizeof(std::string) + key.capacity() + EstimateBytes(item);
			}
			return bytes;
		}

		ScriptValue ToScriptValue(const Json& json, const std::string& path, int depth)
		{
			if (depth > MaxJsonDepth)
				throw ScriptValueError("The data file '" + path + "' is nested more than " + std::to_string(MaxJsonDepth) + " deep");
			switch (json.type())
			{
				case Json::value_t::null:
					return {};
				case Json::value_t::boolean:
					return json.get<bool>();
				case Json::value_t::number_integer:
				case Json::value_t::number_unsigned:
				case Json::value_t::number_float:
					return json.get<double>();
				case Json::value_t::string:
					return json.get<std::string>();
				case Json::value_t::array:
				{
					ScriptValueList items;
					items.reserve(json.size());
					for (const Json& item : json)
						items.push_back(ToScriptValue(item, path, depth + 1));
					return items;
				}
				case Json::value_t::object:
				{
					ScriptValueMap entries;
					entries.reserve(json.size());
					for (auto it = json.begin(); it != json.end(); ++it)
						entries.emplace_back(it.key(), ToScriptValue(it.value(), path, depth + 1));
					return entries;
				}
				default:
					throw ScriptValueError("The data file '" + path + "' holds a value JSON scripts can't read");
			}
		}

		// "parse error at line 3, column 5: syntax error ..." becomes "line 3, column 5: syntax error ...".
		std::string DescribeParseError(const std::string& message)
		{
			const size_t at = message.find("at line ");
			if (at != std::string::npos)
				return message.substr(at + 3);
			const size_t colon = message.find(": ");
			return colon == std::string::npos ? message : message.substr(colon + 2);
		}

	}

	std::filesystem::path ResolveDataFile(const std::string& path)
	{
		if (path.empty())
			throw ScriptValueError(std::string("A data file needs a path inside the Assets folder, ") + c_Example);
		// \ separates folders on every system (so '..\' is caught on Linux too). Script text is UTF-8;
		// built from char8_t, the path keeps accented names on Windows too.
		std::u8string text(path.begin(), path.end());
		std::replace(text.begin(), text.end(), u8'\\', u8'/');
		const std::filesystem::path relative(text);
		if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
			throw ScriptValueError("'" + path + "' is a full path; data files are named from the Assets folder, " + c_Example);
		for (const std::filesystem::path& part : relative)
		{
			if (part == "..")
				throw ScriptValueError("'" + path + "' leaves the Assets folder ('..'); data files are read from inside it, " + c_Example);
		}

		const std::filesystem::path& assets = AssetManager::GetAssetDirectory();
		const std::filesystem::path file = assets / relative;
		std::error_code error;
		if (!std::filesystem::exists(file, error))
			throw ScriptValueError("There's no data file '" + path + "' in the Assets folder");
		if (!std::filesystem::is_regular_file(file, error))
			throw ScriptValueError("'" + path + "' is a folder, not a data file");
		// A link inside Assets may lead anywhere: the file it ends at must be inside too.
		const std::filesystem::path real = std::filesystem::canonical(file, error);
		const std::filesystem::path realAssets = std::filesystem::canonical(assets, error);
		if (error || !IsInside(real, realAssets))
			throw ScriptValueError("'" + path + "' leads outside the Assets folder; data files are read from inside it");
		return real;
	}

	std::string LoadDataText(const std::string& path)
	{
		return ReadFile(path).Text;
	}

	ScriptValue LoadDataJson(const std::string& path)
	{
		CachedFile& file = ReadFile(path);
		if (!file.Json)
		{
			Json json;
			try
			{
				// Too deep is refused while parsing, before a huge tree is built.
				json = Json::parse(file.Text, [&path](int depth, Json::parse_event_t, Json&)
				{
					if (depth > MaxJsonDepth)
						throw ScriptValueError("The data file '" + path + "' is nested more than " + std::to_string(MaxJsonDepth) + " deep");
					return true;
				});
			}
			catch (const Json::parse_error& error)
			{
				throw ScriptValueError("The data file '" + path + "' isn't valid JSON: " + DescribeParseError(error.what()));
			}
			ScriptValue value = ToScriptValue(json, path, 0);
			const uintmax_t bytes = EstimateBytes(value);
			// A file whose values can't be kept even alone is read each time, and leaves the others be.
			if (file.GetBytes() + bytes > MaxDataFileCacheBytes)
			{
				if (s_UncachedWarned.insert(path).second)
					BW_CORE_WARN("The data file '{}' is too big to keep in memory once read, so it is read again each time", path);
				return value;
			}
			if (s_CachedBytes + bytes > MaxDataFileCacheBytes)
			{
				// Make room by dropping every other file.
				std::erase_if(s_Cache, [&file](const auto& entry) { return &entry.second != &file; });
				s_CachedBytes = file.GetBytes();
			}
			file.JsonBytes = bytes;
			s_CachedBytes += bytes;
			file.Json = std::move(value);
		}
		return *file.Json;
	}

	void ClearDataFileCache()
	{
		s_Cache.clear();
		s_CachedBytes = 0;
	}

	uintmax_t GetDataFileCacheBytes()
	{
		return s_CachedBytes;
	}

}
