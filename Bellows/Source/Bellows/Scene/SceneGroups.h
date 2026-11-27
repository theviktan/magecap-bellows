#pragma once

#include "Bellows/Core/UUID.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Bellows {

	// A group in the Hierarchy: a named, colored folder that a scene's top-level entities are filed
	// under (an entity's children follow it). A group never moves or parents anything; it is only how
	// a scene is laid out, and scripts can use it to reach the entities in it ("Docking", "Enemies").
	struct SceneGroup
	{
		UUID ID;
		std::string Name;
		std::string Icon = "folder";			// One of SceneGroups::GetIcons()
		glm::vec3 Color { 0.553f, 0.580f, 0.690f };
		std::string Note;						// What the group is for, shown under its name
		// Editor-only: the editor's viewport doesn't draw the group's entities (their meshes, lights and
		// world text), or doesn't let them be picked. The game always shows them.
		bool HiddenInEditor = false;
		bool LockedInEditor = false;
	};

	// The rules for groups, shared by the editor, scene files and both script languages. A name is
	// trimmed, 1 to MaxNameLength characters on one line, and unique in its scene in any letter case;
	// names are found in any letter case ("ui" finds "UI"). A note is at most MaxNoteLength characters.
	namespace SceneGroups {

		constexpr size_t MaxNameLength = 64;
		constexpr size_t MaxNoteLength = 200;
		constexpr size_t MaxGroups = 256;

		// The icons a group can show, the first being the default.
		std::span<const std::string_view> GetIcons();
		bool IsIcon(std::string_view icon);

		// The text without spaces at either end.
		std::string Trim(std::string_view text);
		// True when the two names are the same in any letter case.
		bool NamesMatch(std::string_view a, std::string_view b);

		// Why a name (already trimmed) or a note can't be used, or nothing when it can.
		std::optional<std::string> CheckName(std::string_view name);
		std::optional<std::string> CheckNote(std::string_view note);

	}

}
