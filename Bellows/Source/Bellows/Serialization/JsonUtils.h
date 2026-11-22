#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace Bellows {

	using Json = nlohmann::ordered_json;

	// Collects the values JsonRead could not use on this thread while it is alive: a key that is
	// present but null, of the wrong kind, or a number that is not finite (NaN is saved as null).
	// Loaders create one around a unit of data (an entity, the scene settings) and report what it
	// found, so a bad value is never dropped silently. Without one, JsonRead stays quiet.
	class JsonReadIssues
	{
	public:
		struct Issue
		{
			const Json* Object = nullptr;	// The object that holds the key
			std::string Key;
		};

		JsonReadIssues() : m_Previous(s_Current) { s_Current = this; }
		~JsonReadIssues() { s_Current = m_Previous; }
		JsonReadIssues(const JsonReadIssues&) = delete;
		JsonReadIssues& operator=(const JsonReadIssues&) = delete;

		const std::vector<Issue>& GetIssues() const { return m_Issues; }
		bool IsEmpty() const { return m_Issues.empty(); }

		static void Report(const Json& object, const char* key)
		{
			if (s_Current)
				s_Current->m_Issues.push_back({ &object, key });
		}

	private:
		std::vector<Issue> m_Issues;
		JsonReadIssues* m_Previous;
		static inline thread_local JsonReadIssues* s_Current = nullptr;
	};

	namespace Detail {

		template<typename T>
		bool IsFiniteValue(const T& value)
		{
			if constexpr (std::is_floating_point_v<T>)
				return std::isfinite(value);
			else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
			{
				for (typename T::length_type i = 0; i < T::length(); i++)
				{
					if (!std::isfinite(value[i]))
						return false;
				}
				return true;
			}
			else
				return true;
		}

	}

	// Reads 'key' from 'object' into 'value' when present and usable; leaves 'value' untouched otherwise.
	// A missing key is normal (older files). A key that is present but null, of the wrong kind, or not
	// a finite number is reported to the active JsonReadIssues.
	template<typename T>
	void JsonRead(const Json& object, const char* key, T& value)
	{
		if (!object.is_object())
			return;
		auto it = object.find(key);
		if (it == object.end())
			return;
		if (it->is_null())
		{
			JsonReadIssues::Report(object, key);
			return;
		}
		try
		{
			T read = it->template get<T>();
			if (!Detail::IsFiniteValue(read))
			{
				JsonReadIssues::Report(object, key);
				return;
			}
			value = std::move(read);
		}
		catch (const Json::exception&)
		{
			JsonReadIssues::Report(object, key);
		}
	}

}

// glm <-> JSON arrays
namespace glm {

	inline void to_json(nlohmann::ordered_json& json, const vec2& v) { json = { v.x, v.y }; }
	inline void to_json(nlohmann::ordered_json& json, const vec3& v) { json = { v.x, v.y, v.z }; }
	inline void to_json(nlohmann::ordered_json& json, const vec4& v) { json = { v.x, v.y, v.z, v.w }; }
	inline void to_json(nlohmann::ordered_json& json, const quat& q) { json = { q.w, q.x, q.y, q.z }; }

	// at() throws for anything but a long enough array, so JsonRead reports it and keeps the old value
	// (a short array used to leave the vector uninitialized).
	inline void from_json(const nlohmann::ordered_json& json, vec2& v)
	{
		v = vec2(json.at(0).get<float>(), json.at(1).get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, vec3& v)
	{
		v = vec3(json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, vec4& v)
	{
		v = vec4(json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>(), json.at(3).get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, quat& q)
	{
		q = quat(json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>(), json.at(3).get<float>());
	}

}
