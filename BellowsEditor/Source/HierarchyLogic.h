#pragma once

#include "Bellows/Core/UUID.h"
#include "Bellows/Scene/Entity.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	class Scene;

	// The Hierarchy panel's logic that has no UI: what kind an entity is (the By type view), what the
	// search box and its chips match, which sibling names stack, and what Organize suggests. The
	// editor and its tests share it.
	namespace HierarchyLogic {

		// An entity's main kind, in the order the By type view lists them. The first that fits wins.
		enum class EntityKind : uint8_t { Scripted, Light, Camera, UI, Audio, Physics, Mesh, Other };
		EntityKind Classify(Entity entity);
		const char* KindName(EntityKind kind);

		// The search box: plain words match names (any letter case, all of them); "tag:enemy" needs the
		// tag (any letter case); "has:script" needs a kind of component (script, light, camera, physics,
		// audio, mesh, ui, animation, nav, particles, tag for any tag); "group:docking" needs the entity's group.
		struct Filter
		{
			std::vector<std::string> Words;
			std::vector<std::string> Tags;
			std::vector<std::string> Has;
			std::vector<std::string> Groups;
			std::vector<std::string> Unknown;	// "has:" values it doesn't know, shown as a hint

			bool IsActive() const { return !Words.empty() || !Tags.empty() || !Has.empty() || !Groups.empty(); }
		};
		Filter ParseFilter(std::string_view text);
		// The "has:" values the search understands.
		const std::vector<std::string_view>& GetHasValues();
		bool Matches(Scene& scene, Entity entity, const Filter& filter);

		// The name a run of numbered siblings shares ("Crate 0-1", "Crate (2)", "SM_Flower_57" give
		// "Crate", "Crate" and "SM_Flower"), or empty when the name has no number at its end.
		std::string StackName(std::string_view name);
		// How many siblings in a row a stack needs.
		constexpr size_t MinStackSize = 3;

		// One move Organize suggests: a top-level entity in no group, into a group it makes when the scene
		// has none of that name.
		struct OrganizeMove
		{
			UUID Entity = 0;
			std::string Group;
			std::string Icon;
			glm::vec3 Color { 0.0f };
			bool NewGroup = false;
		};
		// Lights and skies go to Lighting, cameras to Cameras, UI canvases to UI and sound sources (that
		// draw nothing) to Audio. Entities already in a group, and everything else, stay where they are.
		std::vector<OrganizeMove> SuggestOrganize(Scene& scene);
		// Makes the groups the moves need and files each entity; returns how many moved.
		size_t ApplyOrganize(Scene& scene, const std::vector<OrganizeMove>& moves);

	}

}
