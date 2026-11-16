#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace Bellows {

	// A value exposed by a script and edited in the inspector. Python script classes declare fields as
	// annotated class attributes (`speed: float = 4.0`); ForgeScript scripts as top-level `set speed to 4.0`.
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

	// Converts 'value' to the type of 'field' when both are numbers (a whole number set on a decimal
	// field, or the other way round, for example from a script's set_field). Whole-number fields round.
	// Returns false when the types cannot be converted; values of the same type are returned unchanged.
	bool ConvertScriptFieldValue(const ScriptFieldValue& value, const ScriptFieldValue& field, ScriptFieldValue& outValue);

}
