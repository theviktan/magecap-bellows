#pragma once

#include <string_view>
#include <vector>

namespace ForgeScript {

	// The easing curves behind ease(t, style), shared with hosts so an engine's tweens move exactly the
	// way a script's ease() does. Each curve goes from 0 at t = 0 to 1 at t = 1 ("back" and "elastic"
	// overshoot on the way).

	// Puts the curve's value at t into outValue and returns true, or returns false for an unknown style.
	// The style is matched like ease() matches it: surrounding spaces and letter case don't matter.
	// t is clamped to 0 to 1; a NaN t counts as 0.
	bool TryEase(std::string_view style, double t, double& outValue);

	// The 11 style names, lowercase, in the order ease()'s mistake lists them: "linear", "in", "out",
	// "in out", "sine in", "sine out", "sine in out", "back in", "back out", "bounce out", "elastic out".
	const std::vector<std::string_view>& EaseStyleNames();

}
