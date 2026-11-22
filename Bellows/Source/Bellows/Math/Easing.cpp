#include "bwpch.h"
#include "Bellows/Math/Easing.h"

#include "ForgeScript/Core/Easing.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Bellows::Math {

	double EaseCurve::operator()(double t) const
	{
		double value = 0.0;
		if (!m_Style.empty() && ForgeScript::TryEase(m_Style, t, value))
			return value;
		return std::isnan(t) ? 0.0 : std::clamp(t, 0.0, 1.0);
	}

	EaseCurve FindEaseCurve(std::string_view style)
	{
		while (!style.empty() && std::isspace(static_cast<unsigned char>(style.front())))
			style.remove_prefix(1);
		while (!style.empty() && std::isspace(static_cast<unsigned char>(style.back())))
			style.remove_suffix(1);
		for (std::string_view name : ForgeScript::EaseStyleNames())
		{
			const bool same = name.size() == style.size() && std::equal(name.begin(), name.end(), style.begin(),
				[](char a, char b) { return a == static_cast<char>(std::tolower(static_cast<unsigned char>(b))); });
			if (same)
				return EaseCurve(name);
		}
		return EaseCurve();
	}

	const std::vector<std::string>& GetEaseStyleNames()
	{
		static const std::vector<std::string> s_Names(ForgeScript::EaseStyleNames().begin(), ForgeScript::EaseStyleNames().end());
		return s_Names;
	}

}
