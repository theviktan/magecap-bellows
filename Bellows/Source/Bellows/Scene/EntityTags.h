#pragma once

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	class Scene;

	// Tags group entities ("enemy", "pickup", "door") so scripts can find them all at once. The rules
	// are shared by the editor, scene files and both script languages: a tag is trimmed, can't be
	// empty, is at most MaxTagLength characters on one line, and matches exactly (letter case counts). An entity has
	// at most MaxTagsPerEntity tags, each once, in the order they were added.
	namespace EntityTags {

		constexpr size_t MaxTagLength = 64;
		constexpr size_t MaxTagsPerEntity = 32;

		// The tag without spaces at either end.
		std::string Trim(std::string_view tag);

		// Why a tag (already trimmed) can't be used, or nothing when it can.
		std::optional<std::string> CheckTag(std::string_view tag);

		bool HasTag(const TagComponent& component, std::string_view tag);

		// Adds a tag and returns true, or returns false when the entity already has it. Throws
		// std::invalid_argument with the reason when the tag can't be used or the entity has
		// MaxTagsPerEntity already.
		bool AddTag(TagComponent& component, std::string_view tag);

		// Removes a tag and returns true, or returns false when the entity didn't have it.
		bool RemoveTag(TagComponent& component, std::string_view tag);

		// A list read from a file, as AddTag would leave it: unusable tags, repeats and those past
		// MaxTagsPerEntity are dropped. 'outDropped' receives what was dropped.
		std::vector<std::string> Clean(const std::vector<std::string>& tags, std::vector<std::string>& outDropped);

		// Every entity of the scene with the tag, in hierarchy order (parents before their children),
		// leaving out entities destroyed this frame.
		std::vector<Entity> FindTagged(Scene& scene, std::string_view tag);

	}

}
