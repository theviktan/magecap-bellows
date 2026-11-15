#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <string>

namespace Bellows {

	using Json = nlohmann::ordered_json;

	// Reads 'key' from 'object' into 'value' when present and convertible; leaves 'value' untouched otherwise.
	template<typename T>
	void JsonRead(const Json& object, const char* key, T& value)
	{
		if (!object.is_object())
			return;
		auto it = object.find(key);
		if (it == object.end() || it->is_null())
			return;
		try
		{
			value = it->template get<T>();
		}
		catch (const Json::exception&)
		{
		}
	}

}

// glm <-> JSON arrays
namespace glm {

	inline void to_json(nlohmann::ordered_json& json, const vec2& v) { json = { v.x, v.y }; }
	inline void to_json(nlohmann::ordered_json& json, const vec3& v) { json = { v.x, v.y, v.z }; }
	inline void to_json(nlohmann::ordered_json& json, const vec4& v) { json = { v.x, v.y, v.z, v.w }; }
	inline void to_json(nlohmann::ordered_json& json, const quat& q) { json = { q.w, q.x, q.y, q.z }; }

	inline void from_json(const nlohmann::ordered_json& json, vec2& v)
	{
		if (json.is_array() && json.size() >= 2)
			v = vec2(json[0].get<float>(), json[1].get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, vec3& v)
	{
		if (json.is_array() && json.size() >= 3)
			v = vec3(json[0].get<float>(), json[1].get<float>(), json[2].get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, vec4& v)
	{
		if (json.is_array() && json.size() >= 4)
			v = vec4(json[0].get<float>(), json[1].get<float>(), json[2].get<float>(), json[3].get<float>());
	}

	inline void from_json(const nlohmann::ordered_json& json, quat& q)
	{
		if (json.is_array() && json.size() >= 4)
			q = quat(json[0].get<float>(), json[1].get<float>(), json[2].get<float>(), json[3].get<float>());
	}

}
