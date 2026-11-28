#include "bwpch.h"
#include "Bellows/Scene/BeamSystem.h"

#include <glm/gtc/constants.hpp>

namespace Bellows {

	const char* BeamEndModeToString(BeamEndMode mode)
	{
		switch (mode)
		{
			case BeamEndMode::Point:	return "point";
			case BeamEndMode::Entity:	return "entity";
			case BeamEndMode::Raycast:	return "raycast";
		}
		return "point";
	}

	std::optional<BeamEndMode> BeamEndModeFromString(std::string_view text)
	{
		if (text == "point") return BeamEndMode::Point;
		if (text == "entity") return BeamEndMode::Entity;
		if (text == "raycast") return BeamEndMode::Raycast;
		return std::nullopt;
	}

}

namespace Bellows::BeamSystem {

	namespace {

		// A smooth wave along the beam that changes with time: three sines at unrelated rates, so it
		// doesn't look regular. About -1 to 1.
		float Wave(float distance, float time, float phase)
		{
			return (std::sin(distance * 6.2832f + time * 1.7f + phase) * 0.5f
				+ std::sin(distance * 13.1f - time * 2.9f + phase * 2.3f) * 0.3f
				+ std::sin(distance * 27.7f + time * 5.3f + phase * 3.1f) * 0.2f);
		}

	}

	void BuildStrip(const BeamComponent& beam, std::vector<StripPoint>& outPoints)
	{
		outPoints.clear();
		if (!beam.Active || !beam.RuntimeValid)
			return;
		const glm::vec3 along = beam.RuntimeEnd - beam.RuntimeStart;
		const float length = glm::length(along);
		if (!(length > 1e-5f) || !std::isfinite(length))
			return;
		const glm::vec3 direction = along / length;
		// Two directions across the beam for its wobble.
		const glm::vec3 helper = std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 side = glm::normalize(glm::cross(direction, helper));
		const glm::vec3 up = glm::cross(side, direction);

		const uint32_t segments = std::clamp(beam.Segments, BeamComponent::MinSegments, BeamComponent::MaxSegments);
		const float time = beam.RuntimeTime * beam.NoiseSpeed;
		// The texture repeats every 1 of U, so only the fraction matters (and stays precise).
		const float scroll = std::fmod(beam.RuntimeTime * beam.ScrollSpeed, 1.0f);
		outPoints.reserve(segments + 1);
		for (uint32_t i = 0; i <= segments; i++)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const float distance = t * length;
			StripPoint point;
			point.Position = beam.RuntimeStart + along * t;
			if (beam.NoiseAmplitude > 0.0f)
			{
				// Fixed at both ends, freest in the middle.
				const float envelope = std::sin(t * glm::pi<float>());
				const float wave = distance * beam.NoiseFrequency;
				point.Position += (side * Wave(wave, time, 0.0f) + up * Wave(wave, time, 1.9f)) * (beam.NoiseAmplitude * envelope);
			}
			point.Width = glm::mix(beam.StartWidth, beam.EndWidth, t);
			glm::vec4 color = glm::mix(beam.StartColor, beam.EndColor, t);
			color.r *= beam.Intensity;
			color.g *= beam.Intensity;
			color.b *= beam.Intensity;
			point.Color = color;
			// Copies of the texture along it, moving outward as it scrolls.
			point.U = distance / std::max(beam.TileLength, BeamComponent::MinTileLength) - scroll;
			outPoints.push_back(point);
		}
	}

}
