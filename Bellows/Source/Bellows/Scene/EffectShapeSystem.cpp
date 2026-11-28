#include "bwpch.h"
#include "Bellows/Scene/EffectShapeSystem.h"

#include "Bellows/Scene/ParticleSystem.h"

#include <glm/gtc/constants.hpp>

namespace Bellows {

	const char* EffectShapeTypeToString(EffectShapeType shape)
	{
		switch (shape)
		{
			case EffectShapeType::Ring:		return "ring";
			case EffectShapeType::Cylinder:	return "cylinder";
			case EffectShapeType::Cone:		return "cone";
			case EffectShapeType::Sphere:	return "sphere";
			case EffectShapeType::Dome:		return "dome";
		}
		return "ring";
	}

	std::optional<EffectShapeType> EffectShapeTypeFromString(std::string_view text)
	{
		if (text == "ring")		return EffectShapeType::Ring;
		if (text == "cylinder")	return EffectShapeType::Cylinder;
		if (text == "cone")		return EffectShapeType::Cone;
		if (text == "sphere")	return EffectShapeType::Sphere;
		if (text == "dome")		return EffectShapeType::Dome;
		return std::nullopt;
	}

	namespace {

		// The rows across a shape: one band for flat and straight shapes, more for round ones.
		uint32_t RowCount(const EffectShapeComponent& shape)
		{
			switch (shape.Shape)
			{
				case EffectShapeType::Sphere:	return std::max(2u, shape.Segments / 2);
				case EffectShapeType::Dome:		return std::max(2u, shape.Segments / 4);
				default:						return 1;
			}
		}

		// A point on the shape before scaling: 'angle' around the Y axis (0 toward -Z), 'v' across.
		void Surface(const EffectShapeComponent& shape, float angle, float v, glm::vec3& outPosition, glm::vec3& outNormal)
		{
			const glm::vec3 out(std::sin(angle), 0.0f, -std::cos(angle));
			const glm::vec3 up(0.0f, 1.0f, 0.0f);
			switch (shape.Shape)
			{
				case EffectShapeType::Ring:
					outPosition = out * glm::mix(shape.InnerRadius, shape.Radius, v);
					outNormal = up;
					return;
				case EffectShapeType::Cylinder:
				case EffectShapeType::Cone:
				{
					const float top = shape.Shape == EffectShapeType::Cone ? 0.0f : shape.TopRadius;
					outPosition = out * glm::mix(shape.Radius, top, v) + up * (shape.Height * v);
					// Outward and tipped up or down with the slope of its side.
					outNormal = out * shape.Height + up * (shape.Radius - top);
					return;
				}
				case EffectShapeType::Sphere:
				case EffectShapeType::Dome:
				{
					const float lowest = shape.Shape == EffectShapeType::Sphere ? -glm::half_pi<float>() : 0.0f;
					const float latitude = glm::mix(lowest, glm::half_pi<float>(), v);
					outNormal = out * std::cos(latitude) + up * std::sin(latitude);
					outPosition = outNormal * shape.Radius;
					return;
				}
			}
			outPosition = glm::vec3(0.0f);
			outNormal = up;
		}

		// Meters around and across the shape (before scaling), for the dissolve noise's cells.
		glm::vec2 SurfaceSize(const EffectShapeComponent& shape)
		{
			const float turn = glm::radians(shape.Arc);
			switch (shape.Shape)
			{
				case EffectShapeType::Ring:
					return { turn * shape.Radius, std::abs(shape.Radius - shape.InnerRadius) };
				case EffectShapeType::Cylinder:
				case EffectShapeType::Cone:
				{
					const float top = shape.Shape == EffectShapeType::Cone ? 0.0f : shape.TopRadius;
					return { turn * std::max(shape.Radius, top), std::hypot(shape.Height, shape.Radius - top) };
				}
				case EffectShapeType::Sphere:
					return { turn * shape.Radius, glm::pi<float>() * shape.Radius };
				case EffectShapeType::Dome:
					return { turn * shape.Radius, glm::half_pi<float>() * shape.Radius };
			}
			return { 1.0f, 1.0f };
		}

		float SizeAt(const EffectShapeComponent& shape)
		{
			return ParticleSystem::EvaluateCurve(shape.SizeCurve, EffectShapeSystem::CycleProgress(shape), 1.0f);
		}

	}

	namespace EffectShapeSystem {

		void Update(EffectShapeComponent& shape, float seconds)
		{
			if (!std::isfinite(seconds) || seconds <= 0.0f || shape.RuntimeFinished || !shape.Playing)
				return;
			shape.RuntimeSpin = std::fmod(shape.RuntimeSpin + shape.SpinSpeed * seconds, 360.0f);
			shape.RuntimeOffset = glm::fract(shape.RuntimeOffset + shape.Scroll * seconds);
			shape.RuntimeDistortionPhase = glm::fract(shape.RuntimeDistortionPhase + shape.DistortionSpeed * seconds);
			const float duration = std::max(shape.Duration, EffectShapeComponent::MinDuration);
			shape.RuntimeTime += seconds;
			if (shape.RuntimeTime >= duration)
			{
				if (shape.Loop)
				{
					shape.RuntimeTime = std::fmod(shape.RuntimeTime, duration);
				}
				else
				{
					shape.RuntimeTime = duration;
					shape.RuntimeFinished = true;
				}
			}
		}

		void Play(EffectShapeComponent& shape)
		{
			shape.Playing = true;
			shape.RuntimeTime = 0.0f;
			shape.RuntimeFinished = false;
		}

		void Stop(EffectShapeComponent& shape)
		{
			shape.RuntimeTime = 0.0f;
			shape.RuntimeFinished = true;
		}

		float CycleProgress(const EffectShapeComponent& shape)
		{
			const float duration = std::max(shape.Duration, EffectShapeComponent::MinDuration);
			return std::clamp(shape.RuntimeTime / duration, 0.0f, 1.0f);
		}

		void BuildMesh(const EffectShapeComponent& shape, const glm::mat4& world, std::vector<EffectVertex>& outVertices)
		{
			outVertices.clear();
			if (shape.RuntimeFinished)
				return;
			const float size = SizeAt(shape);
			if (!(size > 0.0f))
				return;

			const float arc = glm::radians(std::clamp(shape.Arc, EffectShapeComponent::MinArc, EffectShapeComponent::MaxArc));
			const uint32_t segments = std::clamp(shape.Segments, EffectShapeComponent::MinSegments, EffectShapeComponent::MaxSegments);
			const uint32_t around = std::max(1u, static_cast<uint32_t>(std::ceil(static_cast<float>(segments) * arc / glm::two_pi<float>())));
			const uint32_t rows = RowCount(shape);
			// A slice is centered on the entity's forward (-Z) before it spins.
			const float start = glm::radians(shape.RuntimeSpin) - arc * 0.5f;

			// Normals through the inverse transpose, so a squashed shape's rim still reads right.
			const glm::mat3 linear(world);
			const float determinant = glm::determinant(linear);
			const glm::mat3 normalMatrix = std::abs(determinant) > 1e-12f ? glm::transpose(glm::inverse(linear)) : linear;

			const auto corner = [&](uint32_t i, uint32_t j) -> EffectVertex
			{
				const float u = static_cast<float>(i) / static_cast<float>(around);
				const float v = static_cast<float>(j) / static_cast<float>(rows);
				glm::vec3 position, normal;
				Surface(shape, start + u * arc, v, position, normal);
				EffectVertex vertex;
				vertex.Position = glm::vec3(world * glm::vec4(position * size, 1.0f));
				const glm::vec3 worldNormal = normalMatrix * normal;
				const float length = glm::length(worldNormal);
				vertex.Normal = length > 1e-8f ? worldNormal / length : glm::vec3(0.0f, 1.0f, 0.0f);
				vertex.UV = glm::vec2(u, v);
				return vertex;
			};

			outVertices.reserve(static_cast<size_t>(around) * rows * 6);
			for (uint32_t j = 0; j < rows; j++)
			{
				for (uint32_t i = 0; i < around; i++)
				{
					const EffectVertex a = corner(i, j), b = corner(i + 1, j), c = corner(i + 1, j + 1), d = corner(i, j + 1);
					outVertices.insert(outVertices.end(), { a, b, c, a, c, d });
				}
			}
		}

		EffectMaterial BuildMaterial(const EffectShapeComponent& shape, const glm::mat4& world)
		{
			const float t = CycleProgress(shape);
			EffectMaterial material;
			glm::vec4 color = shape.Color * ParticleSystem::EvaluateGradient(shape.ColorGradient, t, glm::vec4(1.0f));
			material.Color = glm::vec4(glm::vec3(color) * shape.Intensity, color.a);
			material.Additive = shape.Blend == ParticleBlend::Additive;
			material.Tiling = shape.Tiling;
			material.Offset = shape.RuntimeOffset;
			material.EdgeFade = shape.EdgeFade;
			material.Rim = shape.Rim;
			material.Dissolve = shape.DissolveCurve.empty() ? shape.Dissolve : ParticleSystem::EvaluateCurve(shape.DissolveCurve, t, shape.Dissolve);
			material.DissolveEdgeWidth = shape.DissolveEdgeWidth;
			material.DissolveEdgeColor = glm::vec4(glm::vec3(shape.DissolveEdgeColor) * shape.Intensity, shape.DissolveEdgeColor.a);
			// The entity's scale, averaged over its axes, sizes the blobs along with the shape.
			const float scale = (glm::length(glm::vec3(world[0])) + glm::length(glm::vec3(world[1])) + glm::length(glm::vec3(world[2]))) / 3.0f;
			const glm::vec2 meters = SurfaceSize(shape) * SizeAt(shape) * (std::isfinite(scale) ? scale : 1.0f);
			material.NoiseCells = glm::max(meters * shape.DissolveScale, glm::vec2(1.0f));
			material.Distortion = shape.Distortion * EffectShapeComponent::DistortionScreenFraction;
			material.DistortionPhase = shape.RuntimeDistortionPhase;
			material.DistortionWaves = glm::max(meters * shape.DistortionScale, glm::vec2(1.0f));
			return material;
		}

	}

}
