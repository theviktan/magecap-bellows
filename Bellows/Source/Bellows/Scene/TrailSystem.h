#pragma once

#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"

#include <glm/glm.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace Bellows {

	// "stretch", "tile" (how scenes and scripts name them). Unknown text: none.
	const char* TrailTextureModeToString(TrailTextureMode mode);
	std::optional<TrailTextureMode> TrailTextureModeFromString(std::string_view text);

	// Trails on the CPU: the scene calls Update once a frame for each trail with its entity's world
	// position, and draws BuildStrip's points. Points are in world space, so a trail stays where it
	// was left when its entity moves on.
	namespace TrailSystem {

		// Ages the points by 'seconds', drops the ones past Lifetime, and leaves a new point at
		// 'position' when it's Emitting and has moved MinDistance since the last one.
		void Update(TrailComponent& trail, const glm::vec3& position, float seconds);

		// Forgets every point (a teleport, a respawn).
		void Clear(TrailComponent& trail);

		// The strip to draw, from the entity at 'head' (while Emitting) back to the oldest point:
		// width and color from where each point is along the trail (0 at the head, 1 at Lifetime),
		// and U along its length (0 to 1 for Stretch, meters / TileLength for Tile). Fewer than two
		// points: nothing to draw.
		void BuildStrip(const TrailComponent& trail, const glm::vec3& head, std::vector<StripPoint>& outPoints);

	}

}
