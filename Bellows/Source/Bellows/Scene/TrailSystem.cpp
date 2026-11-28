#include "bwpch.h"
#include "Bellows/Scene/TrailSystem.h"

#include "Bellows/Scene/ParticleSystem.h"

namespace Bellows {

	const char* TrailTextureModeToString(TrailTextureMode mode)
	{
		return mode == TrailTextureMode::Tile ? "tile" : "stretch";
	}

	std::optional<TrailTextureMode> TrailTextureModeFromString(std::string_view text)
	{
		if (text == "stretch") return TrailTextureMode::Stretch;
		if (text == "tile") return TrailTextureMode::Tile;
		return std::nullopt;
	}

}

namespace Bellows::TrailSystem {

	namespace {

		// The longest step one update takes, as for particles: a hitch doesn't age a trail away at once.
		constexpr float c_MaxStep = 0.25f;

		bool IsFinite(const glm::vec3& v)
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

	}

	void Update(TrailComponent& trail, const glm::vec3& position, float seconds)
	{
		seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.0f, c_MaxStep) : 0.0f;
		std::vector<TrailPoint>& points = trail.RuntimePoints;
		for (TrailPoint& point : points)
			point.Age += seconds;
		// Oldest first, so the expired ones are at the front.
		const auto alive = std::find_if(points.begin(), points.end(), [&](const TrailPoint& point) { return point.Age < trail.Lifetime; });
		points.erase(points.begin(), alive);

		if (!trail.Emitting || !IsFinite(position))
			return;
		if (points.empty() || glm::distance(points.back().Position, position) >= trail.MinDistance)
			points.push_back({ position, 0.0f });
		if (points.size() > trail.MaxPoints)
			points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(points.size() - trail.MaxPoints));
	}

	void Clear(TrailComponent& trail)
	{
		trail.RuntimePoints.clear();
	}

	void BuildStrip(const TrailComponent& trail, const glm::vec3& head, std::vector<StripPoint>& outPoints)
	{
		outPoints.clear();
		const std::vector<TrailPoint>& points = trail.RuntimePoints;
		const bool withHead = trail.Emitting && IsFinite(head);
		const size_t count = points.size() + (withHead ? 1 : 0);
		if (count < 2)
			return;
		outPoints.reserve(count);

		// Newest to oldest, starting at the entity itself so the trail never lags behind it.
		const auto add = [&](const glm::vec3& position, float age)
		{
			const float t = std::clamp(age / trail.Lifetime, 0.0f, 1.0f);
			StripPoint point;
			point.Position = position;
			point.Width = trail.WidthCurve.empty() ? glm::mix(trail.StartWidth, trail.EndWidth, t)
				: trail.StartWidth * ParticleSystem::EvaluateCurve(trail.WidthCurve, t, 1.0f);
			glm::vec4 color = ParticleSystem::EvaluateGradient(trail.ColorGradient, t, glm::mix(trail.StartColor, trail.EndColor, t));
			color.r *= trail.Intensity;
			color.g *= trail.Intensity;
			color.b *= trail.Intensity;
			point.Color = color;
			outPoints.push_back(point);
		};
		if (withHead)
			add(head, 0.0f);
		for (auto it = points.rbegin(); it != points.rend(); ++it)
			add(it->Position, it->Age);

		// U by distance along the trail: stretched once over its length, or tiled by TileLength.
		float travelled = 0.0f;
		for (size_t i = 1; i < outPoints.size(); i++)
		{
			travelled += glm::distance(outPoints[i - 1].Position, outPoints[i].Position);
			outPoints[i].U = travelled;
		}
		const float scale = trail.TextureMode == TrailTextureMode::Tile ? 1.0f / std::max(trail.TileLength, TrailComponent::MinTileLength)
			: (travelled > 1e-6f ? 1.0f / travelled : 0.0f);
		for (StripPoint& point : outPoints)
			point.U *= scale;
	}

}
