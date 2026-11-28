#pragma once

#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Bellows {

	// "point", "entity", "raycast" (how scenes and scripts name them). Unknown text: none.
	const char* BeamEndModeToString(BeamEndMode mode);
	std::optional<BeamEndMode> BeamEndModeFromString(std::string_view text);

	// Beams: the scene works out each one's start and end once a frame (Scene::UpdateBeams) and draws
	// BuildStrip's points.
	namespace BeamSystem {

		// The strip from RuntimeStart to RuntimeEnd: Segments pieces, width and color from start to
		// end, a sideways wobble (zero at both ends) and the texture's U scrolling with time. Nothing
		// when it isn't valid or has no length.
		void BuildStrip(const BeamComponent& beam, std::vector<StripPoint>& outPoints);

	}

}
