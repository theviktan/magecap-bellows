#include "bwpch.h"
#include "Bellows/Scene/EntityTags.h"

#include "Bellows/Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace Bellows::EntityTags {

	namespace {

		void CollectTagged(Entity entity, std::string_view tag, std::vector<Entity>& outEntities)
		{
			Scene* scene = entity.GetScene();
			if (!scene->IsPendingDestroy(entity) && HasTag(entity.GetComponent<TagComponent>(), tag))
				outEntities.push_back(entity);
			for (Entity child : entity.GetChildren())
			{
				if (child)
					CollectTagged(child, tag, outEntities);
			}
		}

	}

	std::string Trim(std::string_view tag)
	{
		while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front())))
			tag.remove_prefix(1);
		while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back())))
			tag.remove_suffix(1);
		return std::string(tag);
	}

	std::optional<std::string> CheckTag(std::string_view tag)
	{
		if (tag.empty())
			return std::string("A tag can't be empty");
		// Characters, not bytes: a UTF-8 continuation byte (10xxxxxx) doesn't start one.
		const size_t characters = static_cast<size_t>(std::count_if(tag.begin(), tag.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
		if (characters > MaxTagLength)
			return "A tag can be at most " + std::to_string(MaxTagLength) + " characters long, and this one has " + std::to_string(characters);
		if (std::any_of(tag.begin(), tag.end(), [](char c) { return c == '\n' || c == '\r' || c == '\t'; }))
			return std::string("A tag is one line of text, with no tabs or line breaks");
		return std::nullopt;
	}

	bool HasTag(const TagComponent& component, std::string_view tag)
	{
		const std::string trimmed = Trim(tag);
		return std::find(component.Tags.begin(), component.Tags.end(), trimmed) != component.Tags.end();
	}

	bool AddTag(TagComponent& component, std::string_view tag)
	{
		const std::string trimmed = Trim(tag);
		if (std::find(component.Tags.begin(), component.Tags.end(), trimmed) != component.Tags.end())
			return false;
		if (const std::optional<std::string> problem = CheckTag(trimmed))
			throw std::invalid_argument(*problem);
		if (component.Tags.size() >= MaxTagsPerEntity)
			throw std::invalid_argument("An entity can have at most " + std::to_string(MaxTagsPerEntity) + " tags");
		component.Tags.push_back(trimmed);
		return true;
	}

	bool RemoveTag(TagComponent& component, std::string_view tag)
	{
		const std::string trimmed = Trim(tag);
		const auto it = std::find(component.Tags.begin(), component.Tags.end(), trimmed);
		if (it == component.Tags.end())
			return false;
		component.Tags.erase(it);
		return true;
	}

	std::vector<std::string> Clean(const std::vector<std::string>& tags, std::vector<std::string>& outDropped)
	{
		TagComponent cleaned;
		for (const std::string& tag : tags)
		{
			try
			{
				if (!AddTag(cleaned, tag))
					outDropped.push_back(tag);
			}
			catch (const std::invalid_argument&)
			{
				outDropped.push_back(tag);
			}
		}
		return cleaned.Tags;
	}

	std::vector<Entity> FindTagged(Scene& scene, std::string_view tag)
	{
		std::vector<Entity> entities;
		const std::string trimmed = Trim(tag);
		if (trimmed.empty())
			return entities;
		for (Entity root : scene.GetRootEntities())
			CollectTagged(root, trimmed, entities);
		return entities;
	}

}
