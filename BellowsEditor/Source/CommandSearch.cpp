#include "CommandSearch.h"

#include <algorithm>
#include <cctype>

namespace Bellows::CommandSearch {

	namespace {

		char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

		// A word starts the text, follows a separator, or is an uppercase letter after a lowercase one
		// ("DockingBay" has two words).
		bool IsWordStart(std::string_view text, size_t index)
		{
			if (index == 0)
				return true;
			const unsigned char previous = static_cast<unsigned char>(text[index - 1]);
			const unsigned char current = static_cast<unsigned char>(text[index]);
			if (previous == ' ' || previous == '_' || previous == '-' || previous == '/' || previous == '.' || previous == '(')
				return true;
			return std::islower(previous) && std::isupper(current);
		}

		int KindOrder(ResultKind kind) { return static_cast<int>(kind); }

	}

	const char* KindName(ResultKind kind)
	{
		switch (kind)
		{
			case ResultKind::Command:	return "command";
			case ResultKind::Entity:	return "entity";
			case ResultKind::Scene:		return "scene";
			case ResultKind::Script:	return "script";
		}
		return "command";
	}

	int Score(std::string_view text, std::string_view query)
	{
		std::string needle;
		for (char c : query)
		{
			if (c != ' ')
				needle += Lower(c);
		}
		if (needle.empty())
			return 0;
		std::string haystack(text.size(), '\0');
		std::transform(text.begin(), text.end(), haystack.begin(), Lower);
		const int length = static_cast<int>(text.size());

		// The whole query in one piece: best at the start, then at a word start, then anywhere.
		if (const size_t at = haystack.find(needle); at != std::string::npos)
		{
			if (at == 0)
				return 3000 - length;
			const size_t wordAt = [&]
			{
				for (size_t position = at; position != std::string::npos; position = haystack.find(needle, position + 1))
				{
					if (IsWordStart(text, position))
						return position;
				}
				return std::string::npos;
			}();
			if (wordAt != std::string::npos)
				return 2000 - static_cast<int>(wordAt) - length;
			return 1000 - static_cast<int>(at) - length;
		}

		// Letters in order. Each one goes to the next word start that holds it when there is one
		// before the next plain occurrence's run would end, so "sc" in "Save Scene" lands on "Scene".
		int score = 500 - length;
		size_t position = 0;
		size_t previous = std::string::npos;
		for (char c : needle)
		{
			size_t found = haystack.find(c, position);
			if (found == std::string::npos)
				return -1;
			// Prefer a word start holding the letter, unless the letter continues the current run.
			if (!(previous != std::string::npos && found == previous + 1) && !IsWordStart(text, found))
			{
				for (size_t candidate = haystack.find(c, found + 1); candidate != std::string::npos; candidate = haystack.find(c, candidate + 1))
				{
					if (IsWordStart(text, candidate))
					{
						found = candidate;
						break;
					}
				}
			}
			if (IsWordStart(text, found))
				score += 30;
			if (previous != std::string::npos && found == previous + 1)
				score += 20;
			else if (previous != std::string::npos)
				score -= static_cast<int>(std::min<size_t>(found - previous - 1, 20));
			previous = found;
			position = found + 1;
		}
		return std::max(score, 0);
	}

	std::vector<size_t> Rank(const std::vector<Item>& items, std::string_view query, size_t limit)
	{
		const bool empty = std::all_of(query.begin(), query.end(), [](char c) { return c == ' '; });
		struct Scored { size_t Index; int Value; };
		std::vector<Scored> scored;
		for (size_t i = 0; i < items.size(); i++)
		{
			if (empty)
			{
				if (items[i].Kind == ResultKind::Command)
					scored.push_back({ i, 0 });
				continue;
			}
			const int value = Score(items[i].Name, query);
			if (value >= 0)
				scored.push_back({ i, value });
		}
		if (!empty)
		{
			std::stable_sort(scored.begin(), scored.end(), [&](const Scored& a, const Scored& b)
			{
				if (a.Value != b.Value)
					return a.Value > b.Value;
				if (items[a.Index].Kind != items[b.Index].Kind)
					return KindOrder(items[a.Index].Kind) < KindOrder(items[b.Index].Kind);
				return items[a.Index].Name < items[b.Index].Name;
			});
		}
		if (scored.size() > limit)
			scored.resize(limit);
		std::vector<size_t> result;
		result.reserve(scored.size());
		for (const Scored& entry : scored)
			result.push_back(entry.Index);
		return result;
	}

}
