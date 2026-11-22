#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Bellows::Math {

	// A curve from 0 at t = 0 to 1 at t = 1 (some overshoot in between).
	using EaseCurve = double (*)(double t);

	// The curves tweens use, named as ForgeScript's ease(t, style) names them: "linear", "in", "out",
	// "in out", "sine in", "sine out", "sine in out", "back in", "back out", "bounce out" and
	// "elastic out". Matching ignores case and spaces at either end. Returns nullptr for a name it
	// doesn't know.
	EaseCurve FindEaseCurve(std::string_view style);
	// Every style name, in the order above.
	const std::vector<std::string>& GetEaseStyleNames();

}
