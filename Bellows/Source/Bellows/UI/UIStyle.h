#pragma once

#include <glm/glm.hpp>

namespace Bellows {

	// The colors widgets draw their own parts with (check boxes, slider tracks, the dropdown list, the
	// focus ring): the MageCap family's navy, slate, off-white and orange, as sRGB with alpha.
	namespace UIColors {

		inline const glm::vec4 Navy { 0.086f, 0.094f, 0.165f, 1.0f };			// #16182A
		inline const glm::vec4 NavyLight { 0.165f, 0.180f, 0.325f, 1.0f };	// #2A2E53
		inline const glm::vec4 Slate { 0.306f, 0.329f, 0.525f, 1.0f };		// #4E5486
		inline const glm::vec4 OffWhite { 0.941f, 0.949f, 0.980f, 1.0f };	// #F0F2FA
		inline const glm::vec4 Accent { 0.961f, 0.529f, 0.227f, 1.0f };		// #F5873A
		inline const glm::vec4 AccentLight { 0.980f, 0.690f, 0.470f, 1.0f };

	}

}
