#include "bwpch.h"
#include "Bellows/Scene/Components.h"

#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	namespace {

		float ClampOrDefault(float value, float minimum, float maximum, float fallback)
		{
			return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
		}

	}

	void NavAgentComponent::Clamp()
	{
		const NavAgentComponent defaults;
		Speed = ClampOrDefault(Speed, 0.0f, MaxSpeed, defaults.Speed);
		Acceleration = ClampOrDefault(Acceleration, MinAcceleration, MaxAcceleration, defaults.Acceleration);
		Radius = ClampOrDefault(Radius, MinRadius, MaxRadius, defaults.Radius);
		Height = ClampOrDefault(Height, MinHeight, MaxHeight, defaults.Height);
		BaseOffset = ClampOrDefault(BaseOffset, -MaxBaseOffset, MaxBaseOffset, defaults.BaseOffset);
		StoppingDistance = ClampOrDefault(StoppingDistance, MinStoppingDistance, MaxStoppingDistance, defaults.StoppingDistance);
		TurnSpeed = ClampOrDefault(TurnSpeed, 0.0f, MaxTurnSpeed, defaults.TurnSpeed);
	}

	void ParticleEmitterComponent::Clamp()
	{
		const ParticleEmitterComponent defaults;
		MaxParticles = std::min(MaxParticles, MaxMaxParticles);
		Burst = std::min(Burst, MaxBurst);
		Duration = ClampOrDefault(Duration, MinDuration, MaxDuration, defaults.Duration);
		Rate = ClampOrDefault(Rate, 0.0f, MaxRate, defaults.Rate);
		Lifetime = ClampOrDefault(Lifetime, MinLifetime, MaxLifetime, defaults.Lifetime);
		LifetimeVariation = ClampOrDefault(LifetimeVariation, 0.0f, 1.0f, defaults.LifetimeVariation);
		Speed = ClampOrDefault(Speed, 0.0f, MaxSpeed, defaults.Speed);
		SpeedVariation = ClampOrDefault(SpeedVariation, 0.0f, 1.0f, defaults.SpeedVariation);
		SpreadAngle = ClampOrDefault(SpreadAngle, 0.0f, 180.0f, defaults.SpreadAngle);
		for (int axis = 0; axis < 3; axis++)
			ShapeSize[axis] = ClampOrDefault(ShapeSize[axis], 0.0f, MaxShapeSize, defaults.ShapeSize[axis]);
		GravityScale = ClampOrDefault(GravityScale, -MaxGravityScale, MaxGravityScale, defaults.GravityScale);
		Drag = ClampOrDefault(Drag, 0.0f, MaxDrag, defaults.Drag);
		StartSize = ClampOrDefault(StartSize, 0.0f, MaxSize, defaults.StartSize);
		EndSize = ClampOrDefault(EndSize, 0.0f, MaxSize, defaults.EndSize);
		for (int channel = 0; channel < 4; channel++)
		{
			StartColor[channel] = ClampOrDefault(StartColor[channel], 0.0f, 1.0f, defaults.StartColor[channel]);
			EndColor[channel] = ClampOrDefault(EndColor[channel], 0.0f, 1.0f, defaults.EndColor[channel]);
		}
		Intensity = ClampOrDefault(Intensity, 0.0f, MaxIntensity, defaults.Intensity);
		if (Shape != ParticleShape::Point && Shape != ParticleShape::Sphere && Shape != ParticleShape::Box)
			Shape = defaults.Shape;
		if (Blend != ParticleBlend::Additive && Blend != ParticleBlend::Alpha)
			Blend = defaults.Blend;
	}

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
