#include "ForgeScript/Core/Suggest.h"

#include <algorithm>

namespace ForgeScript {

	namespace {

		char Lower(char c)
		{
			return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
		}

		// How many edits a name of this length may be from what it meant.
		size_t AllowedEdits(size_t length)
		{
			if (length <= 2)
				return 0;
			if (length <= 5)
				return 1;
			if (length <= 9)
				return 2;
			return 3;
		}

		// Edits (insert, delete, change or swap two neighbors) from one name to the other, ignoring case.
		// Gives up with limit + 1 once the answer is sure to be above 'limit', which keeps a long list of
		// candidates cheap.
		size_t EditDistance(std::string_view a, std::string_view b, size_t limit)
		{
			const size_t lengthGap = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
			if (lengthGap > limit)
				return limit + 1;

			// Three rows of the usual table: two rows back is needed for swaps.
			std::vector<size_t> before(b.size() + 1), previous(b.size() + 1), current(b.size() + 1);
			for (size_t j = 0; j <= b.size(); j++)
				previous[j] = j;
			for (size_t i = 1; i <= a.size(); i++)
			{
				current[0] = i;
				size_t rowBest = current[0];
				for (size_t j = 1; j <= b.size(); j++)
				{
					const bool same = Lower(a[i - 1]) == Lower(b[j - 1]);
					current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (same ? 0 : 1) });
					if (i > 1 && j > 1 && Lower(a[i - 1]) == Lower(b[j - 2]) && Lower(a[i - 2]) == Lower(b[j - 1]))
						current[j] = std::min(current[j], before[j - 2] + 1);
					rowBest = std::min(rowBest, current[j]);
				}
				if (rowBest > limit)
					return limit + 1;
				std::swap(before, previous);
				std::swap(previous, current);
			}
			return previous[b.size()];
		}

	}

	std::string ClosestName(std::string_view name, const std::vector<std::string_view>& candidates)
	{
		std::string_view best;
		size_t bestEdits = static_cast<size_t>(-1);
		for (std::string_view candidate : candidates)
		{
			// Names starting with "__" are the host's own; scripts never write them.
			if (candidate.empty() || candidate == name || candidate.substr(0, 2) == "__")
				continue;
			// A two-letter name may be a longer one missing a letter ("ad" for "add"), but changing one of
			// its two letters makes another name, not a typo of it.
			const size_t allowed = (name.size() == 2 && candidate.size() == 3) ? 1 : AllowedEdits(name.size());
			const size_t edits = EditDistance(name, candidate, allowed);
			if (edits > allowed)
				continue;
			// Ties go to the alphabetically first name, so the same mistake always gets the same hint.
			if (edits < bestEdits || (edits == bestEdits && !best.empty() && candidate < best))
			{
				best = candidate;
				bestEdits = edits;
			}
		}
		return std::string(best);
	}

	std::string DidYouMean(std::string_view name, const std::vector<std::string_view>& candidates)
	{
		const std::string closest = ClosestName(name, candidates);
		return closest.empty() ? std::string() : " Did you mean '" + closest + "'?";
	}

	std::string OtherLanguageNameHint(std::string_view name)
	{
		struct Way
		{
			std::string_view Name;
			std::string_view Write;
		};
		static const Way s_Ways[] = {
			{ "True", "true" }, { "TRUE", "true" }, { "False", "false" }, { "FALSE", "false" },
			{ "None", "nothing" }, { "null", "nothing" }, { "NULL", "nothing" }, { "nil", "nothing" },
			{ "undefined", "nothing" }, { "this", "self (or my health for a field)" },
			{ "len", "length(list), or: length of list" }, { "str", "text(value)" }, { "tostring", "text(value)" },
			{ "String", "text(value)" }, { "int", "number(text), and floor(x) for a whole number" },
			{ "float", "number(text)" }, { "parseInt", "number(text)" }, { "parseFloat", "number(text)" },
			{ "tonumber", "number(text)" }, { "console", "say value" }, { "echo", "say value" },
			{ "puts", "say value" }, { "println", "say value" }, { "printf", "say \"Score: {score}\"" },
			{ "math", "floor(x), sqrt(x) and the other math functions, without math." },
			{ "Math", "floor(x), sqrt(x) and the other math functions, without Math." },
			{ "await", "wait for door opened, or wait 2 seconds" },
		};
		for (const Way& way : s_Ways)
		{
			if (way.Name == name)
				return " In ForgeScript, write: " + std::string(way.Write);
		}
		return {};
	}

	std::string OtherLanguageMethodHint(std::string_view typeName, std::string_view method)
	{
		struct Way
		{
			std::string_view Type;
			std::string_view Method;
			std::string_view Write;
		};
		static const Way s_Ways[] = {
			{ "list", "append", "add x to list, or list.add(x)" }, { "list", "push", "add x to list, or list.add(x)" },
			{ "list", "push_back", "add x to list, or list.add(x)" }, { "list", "indexOf", "list.index_of(x)" }, { "list", "includes", "list contains x" },
			{ "list", "has", "list contains x" }, { "list", "extend", "for each item in other do add item to list end" },
			{ "list", "forEach", "for each item in list do ... end" }, { "list", "erase", "take x from list, or list.remove(x)" },
			{ "list", "delete", "take x from list, or list.remove(x)" },
			{ "text", "toUpperCase", "text.upper()" }, { "text", "toLowerCase", "text.lower()" },
			{ "text", "strip", "text.trim()" }, { "text", "startswith", "text.starts_with(x)" },
			{ "text", "endswith", "text.ends_with(x)" }, { "text", "includes", "text contains x" },
			{ "text", "format", "\"Score: {score}\", with the value inside { }" },
			{ "text", "toString", "the text itself; it already is text" },
			{ "map", "items", "for each key, value in map do ... end" }, { "map", "containsKey", "map.has(key)" },
			{ "map", "contains", "map.has(key)" },
			{ "map", "update", "map.merge(other)" },
			{ "number", "toString", "text(x)" }, { "number", "round", "round(x)" }, { "number", "floor", "floor(x)" },
			{ "vector", "normalize", "v.normalized()" }, { "vector", "magnitude", "v.length, without brackets" },
		};
		for (const Way& way : s_Ways)
		{
			if (way.Type == typeName && way.Method == method)
				return " In ForgeScript, write: " + std::string(way.Write);
		}
		return {};
	}

}

