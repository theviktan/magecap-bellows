#include "bwpch.h"
#include "Bellows/Scene/Components.h"

#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	const char* ScriptFieldTypeToString(ScriptFieldType type)
	{
		switch (type)
		{
			case ScriptFieldType::Bool:		return "bool";
			case ScriptFieldType::Int:		return "int";
			case ScriptFieldType::Float:	return "float";
			case ScriptFieldType::String:	return "str";
			case ScriptFieldType::Vec2:		return "Vec2";
			case ScriptFieldType::Vec3:		return "Vec3";
			case ScriptFieldType::Vec4:		return "Vec4";
		}
		return "float";
	}

	bool ConvertScriptFieldValue(const ScriptFieldValue& value, const ScriptFieldValue& field, ScriptFieldValue& outValue)
	{
		if (value.index() == field.index())
		{
			outValue = value;
			return true;
		}
		if (std::holds_alternative<int64_t>(value) && std::holds_alternative<double>(field))
		{
			outValue = static_cast<double>(std::get<int64_t>(value));
			return true;
		}
		if (std::holds_alternative<double>(value) && std::holds_alternative<int64_t>(field))
		{
			outValue = static_cast<int64_t>(std::llround(std::get<double>(value)));
			return true;
		}
		return false;
	}

	glm::mat4 TransformComponent::GetTransform() const
	{
		return Math::ComposeTransform(Translation, m_Rotation, Scale);
	}

	void TransformComponent::SetTransform(const glm::mat4& transform)
	{
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (Math::DecomposeTransform(transform, Translation, rotation, Scale))
			SetRotation(rotation);
	}

	void TransformComponent::SetRotation(const glm::quat& rotation)
	{
		m_Rotation = glm::normalize(rotation);
		m_RotationEuler = glm::eulerAngles(m_Rotation);
	}

	void TransformComponent::SetRotationEuler(const glm::vec3& euler)
	{
		m_RotationEuler = euler;
		m_Rotation = glm::quat(euler);
	}

	glm::mat4 CameraComponent::GetProjection(float aspectRatio) const
	{
		if (Projection == ProjectionType::Orthographic)
		{
			const float halfHeight = OrthographicSize * 0.5f;
			const float halfWidth = halfHeight * aspectRatio;
			return Math::OrthographicProjection(-halfWidth, halfWidth, -halfHeight, halfHeight, NearClip, FarClip);
		}
		return Math::PerspectiveProjection(FieldOfView, aspectRatio, NearClip, FarClip);
	}

	const char* RigidBodyTypeToString(RigidBodyType type)
	{
		switch (type)
		{
			case RigidBodyType::Static:		return "Static";
			case RigidBodyType::Dynamic:	return "Dynamic";
			case RigidBodyType::Kinematic:	return "Kinematic";
		}
		return "Dynamic";
	}

	RigidBodyType RigidBodyTypeFromString(std::string_view text)
	{
		if (text == "Static")
			return RigidBodyType::Static;
		if (text == "Kinematic")
			return RigidBodyType::Kinematic;
		return RigidBodyType::Dynamic;
	}

}
