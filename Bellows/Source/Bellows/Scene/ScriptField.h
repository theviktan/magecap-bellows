#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace Bellows {

	// A value exposed by a Python script class and edited in the inspector.
	// Script classes declare fields as annotated class attributes, e.g. `speed: float = 4.0`.
	using ScriptFieldValue = std::variant<bool, int64_t, double, std::string, glm::vec2, glm::vec3, glm::vec4>;

	enum class ScriptFieldType : uint8_t
	{
		Bool = 0,
		Int,
		Float,
		String,
		Vec2,
		Vec3,
		Vec4,
	};

	inline ScriptFieldType GetScriptFieldType(const ScriptFieldValue& value)
	{
		return static_cast<ScriptFieldType>(value.index());
	}

	const char* ScriptFieldTypeToString(ScriptFieldType type);

}
