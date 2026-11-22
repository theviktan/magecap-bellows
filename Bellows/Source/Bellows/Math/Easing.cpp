#include "bwpch.h"
#include "Bellows/Math/Easing.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Bellows::Math {

	namespace {

		// The same formulas as ForgeScript's ease() (VM/Library.cpp); a test checks they agree.
		constexpr double c_Pi = 3.14159265358979323846;
		constexpr double c_Back = 1.70158;

		double BounceOut(double t)
		{
			if (t < 1 / 2.75)
				return 7.5625 * t * t;
			if (t < 2 / 2.75)
			{
				t -= 1.5 / 2.75;
				return 7.5625 * t * t + 0.75;
			}
			if (t < 2.5 / 2.75)
			{
				t -= 2.25 / 2.75;
				return 7.5625 * t * t + 0.9375;
			}
			t -= 2.625 / 2.75;
			return 7.5625 * t * t + 0.984375;
		}

		struct EaseStyle
		{
			const char* Name;
			EaseCurve Curve;
		};

		constexpr EaseStyle c_Styles[] = {
			{ "linear", [](double t) { return t; } },
			{ "in", [](double t) { return t * t * t; } },
			{ "out", [](double t) { return 1 - std::pow(1 - t, 3); } },
			{ "in out", [](double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; } },
			{ "sine in", [](double t) { return 1 - std::cos(t * c_Pi / 2); } },
			{ "sine out", [](double t) { return std::sin(t * c_Pi / 2); } },
			{ "sine in out", [](double t) { return -(std::cos(c_Pi * t) - 1) / 2; } },
			{ "back in", [](double t) { return (c_Back + 1) * t * t * t - c_Back * t * t; } },
			{ "back out", [](double t) { return 1 + (c_Back + 1) * std::pow(t - 1, 3) + c_Back * std::pow(t - 1, 2); } },
			{ "bounce out", BounceOut },
			{ "elastic out", [](double t)
				{
					if (t <= 0 || t >= 1)
						return t <= 0 ? 0.0 : 1.0;
					return std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * c_Pi / 3)) + 1;
				} },
		};

	}

	EaseCurve FindEaseCurve(std::string_view style)
	{
		while (!style.empty() && std::isspace(static_cast<unsigned char>(style.front())))
			style.remove_prefix(1);
		while (!style.empty() && std::isspace(static_cast<unsigned char>(style.back())))
			style.remove_suffix(1);
		std::string lower(style);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const EaseStyle& candidate : c_Styles)
		{
			if (lower == candidate.Name)
				return candidate.Curve;
		}
		return nullptr;
	}

	const std::vector<std::string>& GetEaseStyleNames()
	{
		static const std::vector<std::string> s_Names = []
		{
			std::vector<std::string> names;
			for (const EaseStyle& style : c_Styles)
				names.emplace_back(style.Name);
			return names;
		}();
		return s_Names;
	}

}
