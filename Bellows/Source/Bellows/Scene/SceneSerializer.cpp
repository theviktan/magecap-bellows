#include "bwpch.h"
#include "Bellows/Scene/SceneSerializer.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"

namespace Bellows {

	namespace {

		Json SerializeScriptField(const ScriptFieldValue& value)
		{
			Json json;
			json["Type"] = ScriptFieldTypeToString(GetScriptFieldType(value));
			std::visit([&json](const auto& v) { json["Value"] = v; }, value);
			return json;
		}

		bool DeserializeScriptField(const Json& json, ScriptFieldValue& outValue)
		{
			if (!json.is_object() || !json.contains("Type") || !json.contains("Value"))
				return false;
			const std::string type = json["Type"].get<std::string>();
			const Json& value = json["Value"];
			try
			{
				if (type == "bool") outValue = value.get<bool>();
				else if (type == "int") outValue = value.get<int64_t>();
				else if (type == "float") outValue = value.get<double>();
				else if (type == "str") outValue = value.get<std::string>();
				else if (type == "Vec2") outValue = value.get<glm::vec2>();
				else if (type == "Vec3") outValue = value.get<glm::vec3>();
				else if (type == "Vec4") outValue = value.get<glm::vec4>();
				else return false;
			}
			catch (const Json::exception&)
			{
				return false;
			}
			return true;
		}

		void DeserializeComponents(Entity entity, const Json& json)
		{
			if (auto it = json.find("Transform"); it != json.end())
			{
				TransformComponent& transform = entity.GetComponent<TransformComponent>();
				JsonRead(*it, "Translation", transform.Translation);
				JsonRead(*it, "Scale", transform.Scale);
				glm::vec3 euler = transform.GetRotationEuler();
				glm::quat rotation = transform.GetRotation();
				if (it->contains("RotationEuler"))
				{
					JsonRead(*it, "RotationEuler", euler);
					transform.SetRotationEuler(euler);
				}
				else if (it->contains("Rotation"))
				{
					JsonRead(*it, "Rotation", rotation);
					transform.SetRotation(rotation);
				}
			}

			if (auto it = json.find("MeshRenderer"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<MeshRendererComponent>();
				JsonRead(*it, "Mesh", component.MeshPath);
				JsonRead(*it, "Material", component.MaterialPath);
				JsonRead(*it, "CastShadows", component.CastShadows);
				JsonRead(*it, "Visible", component.Visible);
			}

			if (auto it = json.find("Camera"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<CameraComponent>();
				std::string projection = "Perspective";
				JsonRead(*it, "Projection", projection);
				component.Projection = projection == "Orthographic" ? ProjectionType::Orthographic : ProjectionType::Perspective;
				float fovDegrees = glm::degrees(component.FieldOfView);
				JsonRead(*it, "FieldOfView", fovDegrees);
				component.FieldOfView = glm::radians(fovDegrees);
				JsonRead(*it, "OrthographicSize", component.OrthographicSize);
				JsonRead(*it, "NearClip", component.NearClip);
				JsonRead(*it, "FarClip", component.FarClip);
				JsonRead(*it, "Primary", component.Primary);
			}

			if (auto it = json.find("DirectionalLight"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<DirectionalLightComponent>();
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "Intensity", component.Intensity);
				JsonRead(*it, "CastShadows", component.CastShadows);
			}

			if (auto it = json.find("PointLight"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<PointLightComponent>();
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "Intensity", component.Intensity);
				JsonRead(*it, "Range", component.Range);
			}

			if (auto it = json.find("SpotLight"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<SpotLightComponent>();
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "Intensity", component.Intensity);
				JsonRead(*it, "Range", component.Range);
				float inner = glm::degrees(component.InnerAngle);
				float outer = glm::degrees(component.OuterAngle);
				JsonRead(*it, "InnerAngle", inner);
				JsonRead(*it, "OuterAngle", outer);
				component.InnerAngle = glm::radians(inner);
				component.OuterAngle = glm::radians(outer);
			}

			if (auto it = json.find("SkyLight"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<SkyLightComponent>();
				JsonRead(*it, "Environment", component.EnvironmentPath);
				JsonRead(*it, "Intensity", component.Intensity);
				float rotationDegrees = glm::degrees(component.Rotation);
				JsonRead(*it, "Rotation", rotationDegrees);
				component.Rotation = glm::radians(rotationDegrees);
				JsonRead(*it, "SkyboxBlur", component.SkyboxBlur);
				if (auto sky = it->find("ProceduralSky"); sky != it->end())
				{
					JsonRead(*sky, "ZenithColor", component.Sky.ZenithColor);
					JsonRead(*sky, "HorizonColor", component.Sky.HorizonColor);
					JsonRead(*sky, "GroundColor", component.Sky.GroundColor);
					JsonRead(*sky, "SunIntensity", component.Sky.SunIntensity);
					JsonRead(*sky, "SunAngularRadius", component.Sky.SunAngularRadius);
				}
			}

			if (auto it = json.find("RigidBody"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<RigidBodyComponent>();
				std::string type = "Dynamic";
				JsonRead(*it, "Type", type);
				component.Type = RigidBodyTypeFromString(type);
				JsonRead(*it, "Mass", component.Mass);
				JsonRead(*it, "LinearDamping", component.LinearDamping);
				JsonRead(*it, "AngularDamping", component.AngularDamping);
				JsonRead(*it, "GravityScale", component.GravityScale);
				JsonRead(*it, "Friction", component.Friction);
				JsonRead(*it, "Restitution", component.Restitution);
				JsonRead(*it, "IsTrigger", component.IsTrigger);
				JsonRead(*it, "LockRotationX", component.LockRotationX);
				JsonRead(*it, "LockRotationY", component.LockRotationY);
				JsonRead(*it, "LockRotationZ", component.LockRotationZ);
				JsonRead(*it, "ContinuousCollision", component.ContinuousCollision);
			}

			if (auto it = json.find("BoxCollider"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<BoxColliderComponent>();
				JsonRead(*it, "HalfExtents", component.HalfExtents);
				JsonRead(*it, "Offset", component.Offset);
			}

			if (auto it = json.find("SphereCollider"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<SphereColliderComponent>();
				JsonRead(*it, "Radius", component.Radius);
				JsonRead(*it, "Offset", component.Offset);
			}

			if (auto it = json.find("CapsuleCollider"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<CapsuleColliderComponent>();
				JsonRead(*it, "Radius", component.Radius);
				JsonRead(*it, "HalfHeight", component.HalfHeight);
				JsonRead(*it, "Offset", component.Offset);
			}

			if (auto it = json.find("MeshCollider"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<MeshColliderComponent>();
				JsonRead(*it, "UseMeshRenderer", component.UseMeshRenderer);
			}

			if (auto it = json.find("Script"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<ScriptComponent>();
				JsonRead(*it, "Script", component.ScriptPath);
				JsonRead(*it, "Class", component.ClassName);
				if (auto fields = it->find("Fields"); fields != it->end() && fields->is_object())
				{
					for (auto field = fields->begin(); field != fields->end(); ++field)
					{
						ScriptFieldValue value;
						if (DeserializeScriptField(field.value(), value))
							component.Fields[field.key()] = value;
					}
				}
			}

			if (auto it = json.find("AudioSource"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<AudioSourceComponent>();
				JsonRead(*it, "Clip", component.ClipPath);
				JsonRead(*it, "Volume", component.Volume);
				JsonRead(*it, "Pitch", component.Pitch);
				JsonRead(*it, "Loop", component.Loop);
				JsonRead(*it, "PlayOnStart", component.PlayOnStart);
				JsonRead(*it, "Spatial", component.Spatial);
				JsonRead(*it, "MinDistance", component.MinDistance);
				JsonRead(*it, "MaxDistance", component.MaxDistance);
				JsonRead(*it, "Bus", component.Bus);
			}

			if (auto it = json.find("AudioListener"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<AudioListenerComponent>();
				JsonRead(*it, "Active", component.Active);
			}

			if (auto it = json.find("Prefab"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<PrefabComponent>();
				JsonRead(*it, "Prefab", component.PrefabPath);
			}
		}

	}

	Json SceneSerializer::SerializeEntity(Entity entity)
	{
		Json json;
		json["ID"] = static_cast<uint64_t>(entity.GetUUID());
		json["Name"] = entity.GetName();
		json["Parent"] = static_cast<uint64_t>(entity.GetComponent<RelationshipComponent>().Parent);

		{
			const TransformComponent& transform = entity.GetComponent<TransformComponent>();
			Json& t = json["Transform"];
			t["Translation"] = transform.Translation;
			t["RotationEuler"] = transform.GetRotationEuler();
			t["Scale"] = transform.Scale;
		}

		if (const auto* c = entity.TryGetComponent<MeshRendererComponent>())
		{
			Json& j = json["MeshRenderer"];
			j["Mesh"] = c->MeshPath;
			j["Material"] = c->MaterialPath;
			j["CastShadows"] = c->CastShadows;
			j["Visible"] = c->Visible;
		}

		if (const auto* c = entity.TryGetComponent<CameraComponent>())
		{
			Json& j = json["Camera"];
			j["Projection"] = c->Projection == ProjectionType::Orthographic ? "Orthographic" : "Perspective";
			j["FieldOfView"] = glm::degrees(c->FieldOfView);
			j["OrthographicSize"] = c->OrthographicSize;
			j["NearClip"] = c->NearClip;
			j["FarClip"] = c->FarClip;
			j["Primary"] = c->Primary;
		}

		if (const auto* c = entity.TryGetComponent<DirectionalLightComponent>())
		{
			Json& j = json["DirectionalLight"];
			j["Color"] = c->Color;
			j["Intensity"] = c->Intensity;
			j["CastShadows"] = c->CastShadows;
		}

		if (const auto* c = entity.TryGetComponent<PointLightComponent>())
		{
			Json& j = json["PointLight"];
			j["Color"] = c->Color;
			j["Intensity"] = c->Intensity;
			j["Range"] = c->Range;
		}

		if (const auto* c = entity.TryGetComponent<SpotLightComponent>())
		{
			Json& j = json["SpotLight"];
			j["Color"] = c->Color;
			j["Intensity"] = c->Intensity;
			j["Range"] = c->Range;
			j["InnerAngle"] = glm::degrees(c->InnerAngle);
			j["OuterAngle"] = glm::degrees(c->OuterAngle);
		}

		if (const auto* c = entity.TryGetComponent<SkyLightComponent>())
		{
			Json& j = json["SkyLight"];
			j["Environment"] = c->EnvironmentPath;
			j["Intensity"] = c->Intensity;
			j["Rotation"] = glm::degrees(c->Rotation);
			j["SkyboxBlur"] = c->SkyboxBlur;
			Json& sky = j["ProceduralSky"];
			sky["ZenithColor"] = c->Sky.ZenithColor;
			sky["HorizonColor"] = c->Sky.HorizonColor;
			sky["GroundColor"] = c->Sky.GroundColor;
			sky["SunIntensity"] = c->Sky.SunIntensity;
			sky["SunAngularRadius"] = c->Sky.SunAngularRadius;
		}

		if (const auto* c = entity.TryGetComponent<RigidBodyComponent>())
		{
			Json& j = json["RigidBody"];
			j["Type"] = RigidBodyTypeToString(c->Type);
			j["Mass"] = c->Mass;
			j["LinearDamping"] = c->LinearDamping;
			j["AngularDamping"] = c->AngularDamping;
			j["GravityScale"] = c->GravityScale;
			j["Friction"] = c->Friction;
			j["Restitution"] = c->Restitution;
			j["IsTrigger"] = c->IsTrigger;
			j["LockRotationX"] = c->LockRotationX;
			j["LockRotationY"] = c->LockRotationY;
			j["LockRotationZ"] = c->LockRotationZ;
			j["ContinuousCollision"] = c->ContinuousCollision;
		}

		if (const auto* c = entity.TryGetComponent<BoxColliderComponent>())
		{
			Json& j = json["BoxCollider"];
			j["HalfExtents"] = c->HalfExtents;
			j["Offset"] = c->Offset;
		}

		if (const auto* c = entity.TryGetComponent<SphereColliderComponent>())
		{
			Json& j = json["SphereCollider"];
			j["Radius"] = c->Radius;
			j["Offset"] = c->Offset;
		}

		if (const auto* c = entity.TryGetComponent<CapsuleColliderComponent>())
		{
			Json& j = json["CapsuleCollider"];
			j["Radius"] = c->Radius;
			j["HalfHeight"] = c->HalfHeight;
			j["Offset"] = c->Offset;
		}

		if (const auto* c = entity.TryGetComponent<MeshColliderComponent>())
		{
			Json& j = json["MeshCollider"];
			j["UseMeshRenderer"] = c->UseMeshRenderer;
		}

		if (const auto* c = entity.TryGetComponent<ScriptComponent>())
		{
			Json& j = json["Script"];
			j["Script"] = c->ScriptPath;
			j["Class"] = c->ClassName;
			Json fields = Json::object();
			for (const auto& [name, value] : c->Fields)
				fields[name] = SerializeScriptField(value);
			j["Fields"] = fields;
		}

		if (const auto* c = entity.TryGetComponent<AudioSourceComponent>())
		{
			Json& j = json["AudioSource"];
			j["Clip"] = c->ClipPath;
			j["Volume"] = c->Volume;
			j["Pitch"] = c->Pitch;
			j["Loop"] = c->Loop;
			j["PlayOnStart"] = c->PlayOnStart;
			j["Spatial"] = c->Spatial;
			j["MinDistance"] = c->MinDistance;
			j["MaxDistance"] = c->MaxDistance;
			j["Bus"] = c->Bus;
		}

		if (const auto* c = entity.TryGetComponent<AudioListenerComponent>())
			json["AudioListener"]["Active"] = c->Active;

		if (const auto* c = entity.TryGetComponent<PrefabComponent>())
			json["Prefab"]["Prefab"] = c->PrefabPath;

		return json;
	}

	Json SceneSerializer::SerializeEntityTree(Entity root)
	{
		Json entities = Json::array();
		std::function<void(Entity)> visit = [&](Entity entity)
		{
			entities.push_back(SerializeEntity(entity));
			for (Entity child : entity.GetChildren())
				visit(child);
		};
		visit(root);
		return entities;
	}

	Entity SceneSerializer::DeserializeEntityTree(Scene& scene, const Json& entities, bool generateNewUUIDs, Entity parent)
	{
		if (!entities.is_array() || entities.empty())
			return {};

		std::unordered_map<uint64_t, UUID> remap;
		std::vector<std::pair<Entity, uint64_t>> created;
		created.reserve(entities.size());

		for (const Json& entityJson : entities)
		{
			uint64_t id = 0;
			std::string name = "Entity";
			JsonRead(entityJson, "ID", id);
			JsonRead(entityJson, "Name", name);

			UUID uuid = generateNewUUIDs || id == 0 ? UUID() : UUID(id);
			if (!generateNewUUIDs && scene.TryGetEntityByUUID(uuid))
			{
				BW_CORE_WARN("Duplicate entity ID {} while loading; assigning a new ID", id);
				uuid = UUID();
			}
			remap[id] = uuid;

			Entity entity = scene.CreateEntityWithUUID(uuid, name);
			DeserializeComponents(entity, entityJson);

			uint64_t parentID = 0;
			JsonRead(entityJson, "Parent", parentID);
			created.emplace_back(entity, parentID);
		}

		// Rebuild the hierarchy in file order (keeps sibling order). The first entity is the tree root.
		Entity root = created.front().first;
		for (size_t i = 0; i < created.size(); i++)
		{
			auto& [entity, parentID] = created[i];
			Entity newParent;
			if (i == 0)
			{
				newParent = parent;
				if (!newParent && parentID != 0 && !generateNewUUIDs)
					newParent = scene.TryGetEntityByUUID(remap.contains(parentID) ? remap[parentID] : UUID(parentID));
			}
			else if (auto it = remap.find(parentID); it != remap.end())
			{
				newParent = scene.TryGetEntityByUUID(it->second);
			}
			else if (parentID != 0)
			{
				newParent = scene.TryGetEntityByUUID(UUID(parentID));
			}

			if (newParent)
				scene.SetParent(entity, newParent, false);
		}
		return root;
	}

	Json SceneSerializer::SerializeSettings(const SceneSettings& settings)
	{
		Json json;
		json["Gravity"] = settings.Gravity;
		json["FixedTimestep"] = settings.FixedTimestep;
		json["AmbientColor"] = settings.AmbientColor;

		const SceneRendererSettings& r = settings.Rendering;
		Json& rendering = json["Rendering"];
		rendering["ShadowsEnabled"] = r.ShadowsEnabled;
		rendering["ShadowMapSize"] = r.ShadowMapSize;
		rendering["CascadeCount"] = r.CascadeCount;
		rendering["ShadowDistance"] = r.ShadowDistance;
		rendering["ShadowSoftness"] = r.ShadowSoftness;
		rendering["SSAOEnabled"] = r.SSAOEnabled;
		rendering["SSAORadius"] = r.SSAORadius;
		rendering["SSAOStrength"] = r.SSAOStrength;
		rendering["BloomEnabled"] = r.BloomEnabled;
		rendering["BloomIntensity"] = r.BloomIntensity;
		rendering["BloomThreshold"] = r.BloomThreshold;
		rendering["Exposure"] = r.Exposure;
		rendering["Tonemapper"] = TonemapperToString(r.ToneMapper);
		rendering["Vignette"] = r.Vignette;
		rendering["FXAAEnabled"] = r.FXAAEnabled;
		return json;
	}

	void SceneSerializer::DeserializeSettings(const Json& json, SceneSettings& settings)
	{
		JsonRead(json, "Gravity", settings.Gravity);
		JsonRead(json, "FixedTimestep", settings.FixedTimestep);
		JsonRead(json, "AmbientColor", settings.AmbientColor);

		if (auto it = json.find("Rendering"); it != json.end())
		{
			SceneRendererSettings& r = settings.Rendering;
			JsonRead(*it, "ShadowsEnabled", r.ShadowsEnabled);
			JsonRead(*it, "ShadowMapSize", r.ShadowMapSize);
			JsonRead(*it, "CascadeCount", r.CascadeCount);
			JsonRead(*it, "ShadowDistance", r.ShadowDistance);
			JsonRead(*it, "ShadowSoftness", r.ShadowSoftness);
			JsonRead(*it, "SSAOEnabled", r.SSAOEnabled);
			JsonRead(*it, "SSAORadius", r.SSAORadius);
			JsonRead(*it, "SSAOStrength", r.SSAOStrength);
			JsonRead(*it, "BloomEnabled", r.BloomEnabled);
			JsonRead(*it, "BloomIntensity", r.BloomIntensity);
			JsonRead(*it, "BloomThreshold", r.BloomThreshold);
			JsonRead(*it, "Exposure", r.Exposure);
			std::string tonemapper = TonemapperToString(r.ToneMapper);
			JsonRead(*it, "Tonemapper", tonemapper);
			r.ToneMapper = TonemapperFromString(tonemapper);
			JsonRead(*it, "Vignette", r.Vignette);
			JsonRead(*it, "FXAAEnabled", r.FXAAEnabled);
		}
	}

	Json SceneSerializer::SerializeToJson(Scene& scene)
	{
		Json json;
		json["Scene"] = scene.GetName();
		json["Version"] = SceneFormatVersion;
		json["Settings"] = SerializeSettings(scene.GetSettings());

		Json entities = Json::array();
		for (Entity root : scene.GetRootEntities())
		{
			for (Json& entity : SerializeEntityTree(root))
				entities.push_back(std::move(entity));
		}
		json["Entities"] = std::move(entities);
		return json;
	}

	bool SceneSerializer::DeserializeFromJson(Scene& scene, const Json& json)
	{
		if (!json.is_object() || !json.contains("Entities"))
		{
			BW_CORE_ERROR("Scene data is missing the 'Entities' list");
			return false;
		}

		int version = SceneFormatVersion;
		JsonRead(json, "Version", version);
		if (version > SceneFormatVersion)
			BW_CORE_WARN("Scene was saved by a newer version of MageCap Bellows (format {})", version);

		std::string name = scene.GetName();
		JsonRead(json, "Scene", name);
		scene.SetName(name);
		if (auto it = json.find("Settings"); it != json.end())
			DeserializeSettings(*it, scene.GetSettings());

		const Json& entities = json["Entities"];
		if (!entities.is_array())
			return false;

		// Entities are stored as consecutive depth-first trees; split them at every root.
		Json tree = Json::array();
		auto flush = [&]()
		{
			if (!tree.empty())
				DeserializeEntityTree(scene, tree, false);
			tree = Json::array();
		};
		for (const Json& entity : entities)
		{
			uint64_t parentID = 0;
			JsonRead(entity, "Parent", parentID);
			if (parentID == 0)
				flush();
			tree.push_back(entity);
		}
		flush();
		return true;
	}

	bool SceneSerializer::Serialize(Scene& scene, const std::filesystem::path& path)
	{
		const Json json = SerializeToJson(scene);
		if (!FileSystem::WriteTextFile(path, json.dump(1, '\t')))
		{
			BW_CORE_ERROR("Failed to write scene '{}'", path.string());
			return false;
		}
		return true;
	}

	bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& path)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			BW_CORE_ERROR("Scene file '{}' could not be read", path.string());
			return false;
		}
		const Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded())
		{
			BW_CORE_ERROR("Scene file '{}' is not valid JSON", path.string());
			return false;
		}
		return DeserializeFromJson(scene, json);
	}

	bool SceneSerializer::SavePrefab(Entity root, const std::filesystem::path& path)
	{
		if (!root)
			return false;
		Json json;
		json["Prefab"] = root.GetName();
		json["Version"] = SceneFormatVersion;
		Json entities = SerializeEntityTree(root);
		entities[0]["Parent"] = 0;
		entities[0].erase("Prefab");
		json["Entities"] = std::move(entities);
		if (!FileSystem::WriteTextFile(path, json.dump(1, '\t')))
		{
			BW_CORE_ERROR("Failed to write prefab '{}'", path.string());
			return false;
		}
		return true;
	}

	Entity SceneSerializer::InstantiatePrefab(Scene& scene, const std::string& prefabAssetPath, Entity parent)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(AssetManager::GetAbsolutePath(prefabAssetPath));
		if (!text)
		{
			BW_CORE_ERROR("Prefab '{}' could not be read", prefabAssetPath);
			return {};
		}
		const Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded() || !json.contains("Entities"))
		{
			BW_CORE_ERROR("Prefab '{}' is not valid", prefabAssetPath);
			return {};
		}

		Entity root = DeserializeEntityTree(scene, json["Entities"], true, parent);
		if (root)
			root.AddOrReplaceComponent<PrefabComponent>().PrefabPath = prefabAssetPath;
		return root;
	}

}
