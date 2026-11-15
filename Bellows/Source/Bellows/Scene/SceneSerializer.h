#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <filesystem>

namespace Bellows {

	// Reads and writes scenes (.bscene) and prefabs (.bprefab) as human-readable JSON.
	//
	// Both formats store a flat, depth-first list of entities; each entity records its parent's UUID,
	// so the hierarchy and the order of siblings are preserved exactly.
	class SceneSerializer
	{
	public:
		static constexpr int SceneFormatVersion = 1;

		static bool Serialize(Scene& scene, const std::filesystem::path& path);
		static bool Deserialize(Scene& scene, const std::filesystem::path& path);

		static Json SerializeToJson(Scene& scene);
		static bool DeserializeFromJson(Scene& scene, const Json& json);

		// One entity's components (without children).
		static Json SerializeEntity(Entity entity);
		// An entity and all of its descendants, parents first.
		static Json SerializeEntityTree(Entity root);
		// Recreates an entity tree. With generateNewUUIDs the copies get fresh IDs (prefab instances, paste).
		// Returns the root entity.
		static Entity DeserializeEntityTree(Scene& scene, const Json& entities, bool generateNewUUIDs, Entity parent = {});

		static bool SavePrefab(Entity root, const std::filesystem::path& path);
		// Instantiates a prefab asset (project-relative path). The root receives a PrefabComponent.
		static Entity InstantiatePrefab(Scene& scene, const std::string& prefabAssetPath, Entity parent = {});

		static Json SerializeSettings(const SceneSettings& settings);
		static void DeserializeSettings(const Json& json, SceneSettings& settings);
	};

}
