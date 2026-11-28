#include "HierarchyLogic.h"

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneGroups.h"

#include <algorithm>
#include <cctype>

namespace Bellows::HierarchyLogic {

	namespace {

		std::string ToLower(std::string_view text)
		{
			std::string lower(text);
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return lower;
		}

		bool HasLight(Entity entity)
		{
			return entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>()
				|| entity.HasComponent<SpotLightComponent>() || entity.HasComponent<SkyLightComponent>();
		}

		bool HasKind(Entity entity, std::string_view kind)
		{
			if (kind == "script")
			{
				const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>();
				return script && !script->ScriptPath.empty();
			}
			if (kind == "light")		return HasLight(entity);
			if (kind == "camera")		return entity.HasComponent<CameraComponent>();
			if (kind == "physics")		return entity.HasComponent<RigidBodyComponent>();
			if (kind == "audio")		return entity.HasComponent<AudioSourceComponent>();
			if (kind == "mesh")			return entity.HasComponent<MeshRendererComponent>();
			if (kind == "ui")			return entity.HasComponent<UICanvasComponent>() || entity.HasComponent<UIRectComponent>();
			if (kind == "animation")	return entity.HasComponent<AnimatorComponent>();
			if (kind == "nav")			return entity.HasComponent<NavAgentComponent>();
			if (kind == "particles")	return entity.HasComponent<ParticleEmitterComponent>();
			if (kind == "decal")		return entity.HasComponent<DecalComponent>();
			if (kind == "tag")			return !entity.GetComponent<TagComponent>().Tags.empty();
			return false;
		}

		struct Destination { const char* Group; const char* Icon; glm::vec3 Color; };

		// Where Organize files a top-level entity, or nothing.
		const Destination* DestinationOf(Entity entity)
		{
			static const Destination s_Lighting { "Lighting", "lightbulb", { 0.949f, 0.757f, 0.306f } };
			static const Destination s_Cameras { "Cameras", "camera", { 0.553f, 0.580f, 0.690f } };
			static const Destination s_UI { "UI", "layout", { 0.714f, 0.549f, 1.0f } };
			static const Destination s_Audio { "Audio", "music", { 0.780f, 0.482f, 0.839f } };
			const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>();
			if (script && !script->ScriptPath.empty())
				return nullptr;	// A scripted entity is gameplay, wherever its parts would go.
			if (HasLight(entity))
				return &s_Lighting;
			if (entity.HasComponent<CameraComponent>())
				return &s_Cameras;
			if (entity.HasComponent<UICanvasComponent>())
				return &s_UI;
			if (entity.HasComponent<AudioSourceComponent>() && !entity.HasComponent<MeshRendererComponent>())
				return &s_Audio;
			return nullptr;
		}

	}

	EntityKind Classify(Entity entity)
	{
		if (HasKind(entity, "script"))	return EntityKind::Scripted;
		if (HasLight(entity))			return EntityKind::Light;
		if (HasKind(entity, "camera"))	return EntityKind::Camera;
		if (HasKind(entity, "ui"))		return EntityKind::UI;
		if (entity.HasComponent<AudioSourceComponent>() && !entity.HasComponent<MeshRendererComponent>())
			return EntityKind::Audio;
		if (HasKind(entity, "physics"))	return EntityKind::Physics;
		if (HasKind(entity, "mesh"))	return EntityKind::Mesh;
		return EntityKind::Other;
	}

	const char* KindName(EntityKind kind)
	{
		switch (kind)
		{
			case EntityKind::Scripted:	return "Scripted";
			case EntityKind::Light:		return "Lights";
			case EntityKind::Camera:	return "Cameras";
			case EntityKind::UI:		return "UI";
			case EntityKind::Audio:		return "Audio";
			case EntityKind::Physics:	return "Physics";
			case EntityKind::Mesh:		return "Meshes";
			case EntityKind::Other:		return "Other";
		}
		return "Other";
	}

	const std::vector<std::string_view>& GetHasValues()
	{
		static const std::vector<std::string_view> s_Values = {
			"script", "light", "camera", "physics", "audio", "mesh", "ui", "animation", "nav", "particles", "decal", "tag"
		};
		return s_Values;
	}

	Filter ParseFilter(std::string_view text)
	{
		Filter filter;
		size_t position = 0;
		while (position < text.size())
		{
			while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])))
				position++;
			const size_t start = position;
			while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position])))
				position++;
			if (start == position)
				break;
			const std::string token = ToLower(text.substr(start, position - start));
			auto value = [&](std::string_view prefix) { return token.substr(prefix.size()); };
			if (token.starts_with("tag:") && token.size() > 4)
				filter.Tags.push_back(value("tag:"));
			else if (token.starts_with("group:") && token.size() > 6)
				filter.Groups.push_back(value("group:"));
			else if (token.starts_with("has:") && token.size() > 4)
			{
				const std::string kind = value("has:");
				const auto& known = GetHasValues();
				if (std::find(known.begin(), known.end(), kind) != known.end())
					filter.Has.push_back(kind);
				else
					filter.Unknown.push_back(kind);
			}
			else
				filter.Words.push_back(token);
		}
		return filter;
	}

	bool Matches(Scene& scene, Entity entity, const Filter& filter)
	{
		const std::string name = ToLower(entity.GetName());
		for (const std::string& word : filter.Words)
		{
			if (name.find(word) == std::string::npos)
				return false;
		}
		const std::vector<std::string>& tags = entity.GetComponent<TagComponent>().Tags;
		for (const std::string& wanted : filter.Tags)
		{
			if (std::none_of(tags.begin(), tags.end(), [&](const std::string& tag) { return ToLower(tag) == wanted; }))
				return false;
		}
		for (const std::string& kind : filter.Has)
		{
			if (!HasKind(entity, kind))
				return false;
		}
		if (!filter.Groups.empty())
		{
			const SceneGroup* group = scene.FindGroup(scene.GetEntityGroup(entity));
			if (!group || std::none_of(filter.Groups.begin(), filter.Groups.end(), [&](const std::string& wanted) { return SceneGroups::NamesMatch(group->Name, wanted); }))
				return false;
		}
		return true;
	}

	std::string StackName(std::string_view name)
	{
		// Drop the number at the end with what separates it ("Crate 0-1", "Crate (2)", "Flower_57").
		size_t end = name.size();
		bool sawDigit = false;
		while (end > 0)
		{
			const char c = name[end - 1];
			if (std::isdigit(static_cast<unsigned char>(c)))
				sawDigit = true;
			else if (c != ' ' && c != '_' && c != '-' && c != '.' && c != '(' && c != ')')
				break;
			end--;
		}
		if (!sawDigit || end == 0)
			return {};
		return std::string(name.substr(0, end));
	}

	std::vector<OrganizeMove> SuggestOrganize(Scene& scene)
	{
		std::vector<OrganizeMove> moves;
		for (Entity root : scene.GetRootEntities())
		{
			if (scene.GetEntityGroup(root).IsValid() || scene.IsPendingDestroy(root))
				continue;
			const Destination* destination = DestinationOf(root);
			if (!destination)
				continue;
			OrganizeMove& move = moves.emplace_back();
			move.Entity = root.GetUUID();
			const SceneGroup* existing = scene.FindGroup(destination->Group);
			move.Group = existing ? existing->Name : destination->Group;
			move.Icon = destination->Icon;
			move.Color = destination->Color;
			move.NewGroup = !existing;
		}
		return moves;
	}

	size_t ApplyOrganize(Scene& scene, const std::vector<OrganizeMove>& moves)
	{
		size_t moved = 0;
		for (const OrganizeMove& move : moves)
		{
			Entity entity = scene.TryGetEntityByUUID(move.Entity);
			if (!entity || entity.GetParent())
				continue;
			SceneGroup* group = scene.FindGroup(move.Group);
			if (!group)
			{
				if (scene.GetGroups().size() >= SceneGroups::MaxGroups)
					continue;
				group = &scene.CreateGroup(move.Group);
				group->Icon = move.Icon;
				group->Color = move.Color;
			}
			if (scene.SetEntityGroup(entity, group->ID))
				moved++;
		}
		return moved;
	}

}
