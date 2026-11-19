#include "bwpch.h"
#include "Bellows/Scripting/ScriptApi.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>

namespace Bellows::Scripting {

	namespace {

		constexpr size_t c_MaxNameLength = 64;
		// A vector is saved as { "__vector": [x, y, z] }, so it comes back as a vector, not a list.
		constexpr const char* c_VectorKey = "__vector";
		// Saves nest at most this deep: a list inside itself can't be built from scripts, but a save
		// file edited by hand can be, and reading it must not run out of stack.
		constexpr int c_MaxDepth = 256;

		std::filesystem::path SavePath(const std::string& name, const char* suffix = ".json")
		{
			if (name.empty() || name.size() > c_MaxNameLength)
				throw ScriptValueError("A save name has 1 to 64 characters, not " + std::to_string(name.size()));
			for (unsigned char c : name)
			{
				if (!std::isalnum(c) && c != ' ' && c != '-' && c != '_')
					throw ScriptValueError("A save name can only have letters, digits, spaces, - and _: '" + name + "' has '" + std::string(1, static_cast<char>(c)) + "'");
			}
			if (name.front() == ' ' || name.back() == ' ')
				throw ScriptValueError("A save name can't start or end with a space: '" + name + "'");

			// Windows gives these names to devices in every folder, so a file can't be called that.
			std::string upper = name;
			std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			static const std::array<const char*, 4> c_Devices = { "CON", "PRN", "AUX", "NUL" };
			const bool device = std::find(c_Devices.begin(), c_Devices.end(), upper) != c_Devices.end() ||
				(upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) && upper[3] >= '1' && upper[3] <= '9');
			if (device)
				throw ScriptValueError("'" + name + "' can't be a save name: Windows keeps it for a device");

			return GetUserFolder() / "Saves" / std::filesystem::path(name + suffix);
		}

		Json ToJson(const ScriptValue& value, int depth)
		{
			if (depth > c_MaxDepth)
				throw ScriptValueError("This value is nested too deeply to save (more than 256 lists or maps inside each other)");
			return std::visit([depth](const auto& data) -> Json
			{
				using T = std::decay_t<decltype(data)>;
				if constexpr (std::is_same_v<T, std::monostate>)
					return nullptr;
				else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>)
					return data;
				else if constexpr (std::is_same_v<T, double>)
				{
					if (!std::isfinite(data))
						throw ScriptValueError("A number that isn't finite (infinity, or not a number) can't be saved");
					return data;
				}
				else if constexpr (std::is_same_v<T, glm::vec3>)
					return Json { { c_VectorKey, { data.x, data.y, data.z } } };
				else if constexpr (std::is_same_v<T, ScriptEntity>)
					throw ScriptValueError("An entity can't be saved: entities only exist while the game plays. Save its name, or the values you need from it");
				else if constexpr (std::is_same_v<T, ScriptValueList>)
				{
					Json list = Json::array();
					for (const ScriptValue& item : data)
						list.push_back(ToJson(item, depth + 1));
					return list;
				}
				else
				{
					Json map = Json::object();
					for (const auto& [key, item] : data)
					{
						if (key == c_VectorKey)
							throw ScriptValueError(std::string("A map key can't be '") + c_VectorKey + "' in saved data: it marks saved vectors");
						map[key] = ToJson(item, depth + 1);
					}
					return map;
				}
			}, value.Data);
		}

		// Throws std::runtime_error with a reason for JSON that isn't a save.
		ScriptValue FromJson(const Json& json, int depth)
		{
			if (depth > c_MaxDepth)
				throw std::runtime_error("lists or maps nested more than 256 deep");
			switch (json.type())
			{
				case Json::value_t::null:
					return ScriptValue();
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
					ScriptValueList list;
					for (const Json& item : json)
						list.push_back(FromJson(item, depth + 1));
					return list;
				}
				case Json::value_t::object:
				{
					if (json.size() == 1 && json.contains(c_VectorKey))
					{
						const Json& xyz = json[c_VectorKey];
						if (!xyz.is_array() || xyz.size() != 3 || !xyz[0].is_number() || !xyz[1].is_number() || !xyz[2].is_number())
							throw std::runtime_error("a saved vector that isn't three numbers");
						return glm::vec3(xyz[0].get<float>(), xyz[1].get<float>(), xyz[2].get<float>());
					}
					ScriptValueMap map;
					for (const auto& [key, item] : json.items())
						map.emplace_back(key, FromJson(item, depth + 1));
					return map;
				}
				default:
					throw std::runtime_error("a value saves can't hold");
			}
		}

	}

	bool SaveData(const std::string& name, const ScriptValue& value)
	{
		const std::filesystem::path path = SavePath(name);
		const std::string text = ToJson(value, 0).dump(1, '\t');
		if (FileSystem::WriteTextFile(path, text))
			return true;
		BW_WARN("Couldn't save '{}' to {}: the folder isn't writable or the disk is full", name, path.string());
		return false;
	}

	ScriptValue LoadData(const std::string& name, const ScriptValue& fallback)
	{
		const std::filesystem::path path = SavePath(name);
		std::error_code error;
		if (!std::filesystem::exists(path, error))
			return fallback;

		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			// Unreadable (in use, or no permission) is not damaged: leave the file alone for next time.
			BW_WARN("Couldn't read the save '{}' ({}); using the default instead", name, path.string());
			return fallback;
		}

		std::string reason;
		try
		{
			return FromJson(Json::parse(*text), 0);
		}
		catch (const Json::exception& exception)
		{
			reason = exception.what();
		}
		catch (const std::runtime_error& exception)
		{
			reason = exception.what();
		}

		// Keep what is left of it where the next save won't overwrite it, so it can be repaired by hand.
		const std::filesystem::path damaged = SavePath(name, ".damaged.json");
		std::filesystem::rename(path, damaged, error);
		if (error)
			BW_WARN("The save '{}' is damaged ({}) and couldn't be moved aside; using the default instead", name, reason);
		else
			BW_WARN("The save '{}' is damaged ({}); it was kept as {} and the default is used instead", name, reason, damaged.string());
		return fallback;
	}

	bool HasData(const std::string& name)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(SavePath(name), error);
	}

	bool DeleteData(const std::string& name)
	{
		std::error_code error;
		return std::filesystem::remove(SavePath(name), error);
	}

	double GetRealTime()
	{
		const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
		return std::chrono::duration<double>(sinceEpoch).count();
	}

}
