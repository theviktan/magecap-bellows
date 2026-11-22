#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Bellows::Math {

	// A curve from 0 at t = 0 to 1 at t = 1 (some overshoot in between), by its ForgeScript style
	// name. Evaluating it is ForgeScript's ease(t, style) itself (ForgeScript::TryEase), so tweens move
	// exactly as a script's ease() does. An empty curve is a straight line.
	class EaseCurve
	{
	public:
		EaseCurve() = default;

		// The curve's value at t, which is clamped to 0 to 1.
		double operator()(double t) const;
		explicit operator bool() const { return !m_Style.empty(); }
		bool operator==(const EaseCurve& other) const = default;

		std::string_view GetStyle() const { return m_Style; }
	private:
		explicit EaseCurve(std::string_view style)
			: m_Style(style) {}
		friend EaseCurve FindEaseCurve(std::string_view style);
	private:
		std::string_view m_Style;	// One of ForgeScript::EaseStyleNames(), which live as long as the program
	};

	// The curves tweens use, named as ForgeScript's ease(t, style) names them: "linear", "in", "out",
	// "in out", "sine in", "sine out", "sine in out", "back in", "back out", "bounce out" and
	// "elastic out". Matching ignores case and spaces at either end. Returns an empty curve for a name
	// it doesn't know.
	EaseCurve FindEaseCurve(std::string_view style);
	// Every style name, in the order above.
	const std::vector<std::string>& GetEaseStyleNames();

}
