#include "bwpch.h"
#include "Bellows/Scene/SceneGroups.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace Bellows::SceneGroups {

	namespace {

		// Matches the icons the editor draws for groups (Lucide names).
		constexpr std::array<std::string_view, 16> c_Icons = {
			"folder", "mountain", "anchor", "rocket", "lightbulb", "layout", "music", "camera",
			"box", "user", "users", "flag", "star", "swords", "trees", "house"
		};

		// Characters, not bytes: a UTF-8 continuation byte (10xxxxxx) doesn't start one.
		size_t CountCharacters(std::string_view text)
		{
			return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
		}

	}

	std::span<const std::string_view> GetIcons()
	{
		return c_Icons;
	}

	bool IsIcon(std::string_view icon)
	{
		return std::find(c_Icons.begin(), c_Icons.end(), icon) != c_Icons.end();
	}

	std::string Trim(std::string_view text)
	{
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
			text.remove_prefix(1);
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
			text.remove_suffix(1);
		return std::string(text);
	}

	bool NamesMatch(std::string_view a, std::string_view b)
	{
		// ASCII letters match in any case; other characters must be the same.
		return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
		{
			return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
		});
	}

	std::optional<std::string> CheckName(std::string_view name)
	{
		if (name.empty())
			return std::string("A group's name can't be empty");
		if (const size_t characters = CountCharacters(name); characters > MaxNameLength)
			return "A group's name can be at most " + std::to_string(MaxNameLength) + " characters long, and this one has " + std::to_string(characters);
		if (std::any_of(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r' || c == '\t'; }))
			return std::string("A group's name is one line of text, with no tabs or line breaks");
		return std::nullopt;
	}

	std::optional<std::string> CheckNote(std::string_view note)
	{
		if (const size_t characters = CountCharacters(note); characters > MaxNoteLength)
			return "A group's note can be at most " + std::to_string(MaxNoteLength) + " characters long, and this one has " + std::to_string(characters);
		return std::nullopt;
	}

}
