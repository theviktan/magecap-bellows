#include "bwpch.h"
#include "Bellows/Scene/SceneSerializer.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Navigation/SceneNavigation.h"
#include "Bellows/Scene/EntityTags.h"
#include "Bellows/Scene/BeamSystem.h"
#include "Bellows/Scene/EffectShapeSystem.h"
#include "Bellows/Scene/ParticleSystem.h"
#include "Bellows/Scene/TrailSystem.h"

namespace Bellows {

	namespace {

		// Curves are lists of [time, value] keys and gradients lists of [time, r, g, b, a]; a malformed
		// key is skipped, and the component's Clamp holds the rest to their ranges.
		void ReadCurve(const Json& json, const char* key, std::vector<ParticleCurveKey>& out)
		{
			const auto it = json.find(key);
			if (it == json.end() || !it->is_array())
				return;
			for (const Json& item : *it)
			{
				if (out.size() < ParticleEmitterComponent::MaxCurveKeys && item.is_array() && item.size() == 2 && item[0].is_number() && item[1].is_number())
					out.push_back({ item[0].get<float>(), item[1].get<float>() });
			}
		}

		void ReadGradient(const Json& json, const char* key, std::vector<ParticleColorKey>& out)
		{
			const auto it = json.find(key);
			if (it == json.end() || !it->is_array())
				return;
			for (const Json& item : *it)
			{
				if (out.size() >= ParticleEmitterComponent::MaxCurveKeys || !item.is_array() || item.size() != 5)
					continue;
				if (std::all_of(item.begin(), item.end(), [](const Json& value) { return value.is_number(); }))
					out.push_back({ item[0].get<float>(), glm::vec4(item[1].get<float>(), item[2].get<float>(), item[3].get<float>(), item[4].get<float>()) });
			}
		}

		// Written only when there are keys, so files without curves stay as they were.
		void WriteCurve(Json& json, const char* key, const std::vector<ParticleCurveKey>& keys)
		{
			if (keys.empty())
				return;
			Json& curve = json[key] = Json::array();
			for (const ParticleCurveKey& item : keys)
				curve.push_back(Json::array({ item.Time, item.Value }));
		}

		void WriteGradient(Json& json, const char* key, const std::vector<ParticleColorKey>& keys)
		{
			if (keys.empty())
				return;
			Json& gradient = json[key] = Json::array();
			for (const ParticleColorKey& item : keys)
				gradient.push_back(Json::array({ item.Time, item.Color.r, item.Color.g, item.Color.b, item.Color.a }));
		}

		Json SerializeScriptField(const ScriptFieldValue& value)
		{
			Json json;
			json["Type"] = ScriptFieldTypeToString(GetScriptFieldType(value));
			std::visit([&json](const auto& v) { json["Value"] = v; }, value);
			return json;
		}

		bool DeserializeScriptField(const Json& json, ScriptFieldValue& outValue)
		{
			if (!json.is_object() || !json.contains("Type") || !json.contains("Value") || !json["Type"].is_string())
				return false;
			const std::string& type = json["Type"].get_ref<const std::string&>();
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

		// Finds the dotted path of keys from 'root' down to 'target', an object or array inside it.
		bool FindJsonPath(const Json& root, const Json* target, std::string& path, int depth = 0)
		{
			if (&root == target)
				return true;
			if (depth >= 16 || !root.is_structured())
				return false;
			for (auto it = root.begin(); it != root.end(); ++it)
			{
				std::string step = root.is_object() ? it.key() : "[" + std::to_string(std::distance(root.begin(), it)) + "]";
				std::string inner;
				if (FindJsonPath(*it, target, inner, depth + 1))
				{
					path = inner.empty() ? step : (inner.front() == '[' ? step + inner : step + "." + inner);
					return true;
				}
			}
			return false;
		}

		// Names every value JsonRead could not use under 'root' in one Console warning, so a NaN saved
		// as null (or a hand-edited typo) is never dropped without a word.
		void WarnUnreadValues(const JsonReadIssues& issues, const Json& root, const std::string& owner)
		{
			if (issues.IsEmpty())
				return;
			std::string fields;
			for (const JsonReadIssues::Issue& issue : issues.GetIssues())
			{
				std::string path;
				FindJsonPath(root, issue.Object, path);
				if (!fields.empty())
					fields += ", ";
				fields += path.empty() ? issue.Key : path + "." + issue.Key;
			}
			BW_CORE_WARN("{}: {} could not be read (empty, the wrong kind of value, or not a finite number), so the default was kept",
				owner, fields);
		}

		Json SerializeGroups(const std::vector<SceneGroup>& groups)
		{
			Json json = Json::array();
			for (const SceneGroup& group : groups)
			{
				Json& entry = json.emplace_back();
				entry["ID"] = static_cast<uint64_t>(group.ID);
				entry["Name"] = group.Name;
				entry["Icon"] = group.Icon;
				entry["Color"] = group.Color;
				if (!group.Note.empty())
					entry["Note"] = group.Note;
				if (group.HiddenInEditor)
					entry["HiddenInEditor"] = true;
				if (group.LockedInEditor)
					entry["LockedInEditor"] = true;
			}
			return json;
		}

		// The groups of a scene file, as the scene's own rules would leave them: a group without a usable
		// name, a repeated name or ID, or one past MaxGroups is left out (with a warning); an unknown icon
		// becomes the default, a color is kept between 0 and 1, and a note is cut to MaxNoteLength.
		std::vector<SceneGroup> DeserializeGroups(const Json& json)
		{
			std::vector<SceneGroup> groups;
			if (!json.is_array())
			{
				BW_CORE_WARN("Scene groups: 'Groups' is not a list, so the scene has none");
				return groups;
			}
			for (const Json& entry : json)
			{
				JsonReadIssues issues;
				SceneGroup group;
				uint64_t id = 0;
				JsonRead(entry, "ID", id);
				std::string name;
				JsonRead(entry, "Name", name);
				name = SceneGroups::Trim(name);
				JsonRead(entry, "Icon", group.Icon);
				JsonRead(entry, "Color", group.Color);
				JsonRead(entry, "Note", group.Note);
				JsonRead(entry, "HiddenInEditor", group.HiddenInEditor);
				JsonRead(entry, "LockedInEditor", group.LockedInEditor);
				WarnUnreadValues(issues, entry, "Group '" + name + "'");

				const bool repeated = std::any_of(groups.begin(), groups.end(), [&](const SceneGroup& other)
				{
					return other.ID == UUID(id) || SceneGroups::NamesMatch(other.Name, name);
				});
				if (id == 0 || repeated || SceneGroups::CheckName(name) || groups.size() >= SceneGroups::MaxGroups)
				{
					BW_CORE_WARN("Scene groups: the group '{}' was left out (no ID, an unusable or repeated name, or past {} groups); its entities are in no group",
						name, SceneGroups::MaxGroups);
					continue;
				}
				group.ID = UUID(id);
				group.Name = std::move(name);
				if (!SceneGroups::IsIcon(group.Icon))
					group.Icon = std::string(SceneGroups::GetIcons().front());
				group.Color = glm::clamp(group.Color, glm::vec3(0.0f), glm::vec3(1.0f));
				while (SceneGroups::CheckNote(group.Note))
					group.Note.pop_back();
				groups.push_back(std::move(group));
			}
			return groups;
		}

		void DeserializeComponents(Entity entity, const Json& json)
		{
			TagComponent& tag = entity.GetComponent<TagComponent>();
			JsonRead(json, "KeepAcrossScenes", tag.KeepAcrossScenes);
			JsonRead(json, "Pinned", tag.Pinned);
			uint64_t group = 0;
			JsonRead(json, "Group", group);
			tag.Group = UUID(group);
			std::vector<std::string> tags;
			JsonRead(json, "Tags", tags);
			std::vector<std::string> dropped;
			tag.Tags = EntityTags::Clean(tags, dropped);
			for (const std::string& unused : dropped)
				BW_CORE_WARN("Entity '{}': the tag '{}' was left out (empty, too long, a repeat, or past {} tags)", tag.Tag, unused, EntityTags::MaxTagsPerEntity);
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
				JsonRead(*it, "RunWhilePaused", component.RunWhilePaused);
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

			if (auto it = json.find("Animator"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<AnimatorComponent>();
				JsonRead(*it, "Clip", component.Clip);
				JsonRead(*it, "PlayOnStart", component.PlayOnStart);
				JsonRead(*it, "Loop", component.Loop);
				JsonRead(*it, "Speed", component.Speed);
				component.Speed = std::isfinite(component.Speed) ? std::clamp(component.Speed, -AnimationPlayer::MaxSpeed, AnimationPlayer::MaxSpeed) : 1.0f;
			}

			if (auto it = json.find("NavAgent"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<NavAgentComponent>();
				JsonRead(*it, "Speed", component.Speed);
				JsonRead(*it, "Acceleration", component.Acceleration);
				JsonRead(*it, "Radius", component.Radius);
				JsonRead(*it, "Height", component.Height);
				JsonRead(*it, "BaseOffset", component.BaseOffset);
				JsonRead(*it, "StoppingDistance", component.StoppingDistance);
				JsonRead(*it, "TurnSpeed", component.TurnSpeed);
				JsonRead(*it, "AvoidOthers", component.AvoidOthers);
				component.Clamp();
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

			if (auto it = json.find("Decal"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<DecalComponent>();
				JsonRead(*it, "Texture", component.TexturePath);
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "Size", component.Size);
				JsonRead(*it, "MaxAngle", component.MaxAngle);
				JsonRead(*it, "SortOrder", component.SortOrder);
				component.Clamp();
			}

			if (auto it = json.find("ParticleEmitter"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<ParticleEmitterComponent>();
				JsonRead(*it, "PlayOnStart", component.PlayOnStart);
				JsonRead(*it, "Looping", component.Looping);
				JsonRead(*it, "Duration", component.Duration);
				JsonRead(*it, "Rate", component.Rate);
				JsonRead(*it, "Burst", component.Burst);
				JsonRead(*it, "MaxParticles", component.MaxParticles);
				JsonRead(*it, "Lifetime", component.Lifetime);
				JsonRead(*it, "LifetimeVariation", component.LifetimeVariation);
				JsonRead(*it, "Speed", component.Speed);
				JsonRead(*it, "SpeedVariation", component.SpeedVariation);
				JsonRead(*it, "SpreadAngle", component.SpreadAngle);
				std::string shape = ParticleShapeToString(component.Shape);
				JsonRead(*it, "Shape", shape);
				component.Shape = ParticleShapeFromString(shape).value_or(component.Shape);
				JsonRead(*it, "ShapeSize", component.ShapeSize);
				JsonRead(*it, "GravityScale", component.GravityScale);
				JsonRead(*it, "Drag", component.Drag);
				JsonRead(*it, "StartSize", component.StartSize);
				JsonRead(*it, "EndSize", component.EndSize);
				JsonRead(*it, "StartColor", component.StartColor);
				JsonRead(*it, "EndColor", component.EndColor);
				JsonRead(*it, "Intensity", component.Intensity);
				std::string blend = ParticleBlendToString(component.Blend);
				JsonRead(*it, "Blend", blend);
				component.Blend = ParticleBlendFromString(blend).value_or(component.Blend);
				JsonRead(*it, "Texture", component.TexturePath);
				JsonRead(*it, "WorldSpace", component.WorldSpace);
				JsonRead(*it, "Outward", component.Outward);
				JsonRead(*it, "NoiseStrength", component.NoiseStrength);
				JsonRead(*it, "NoiseFrequency", component.NoiseFrequency);
				JsonRead(*it, "NoiseSpeed", component.NoiseSpeed);
				JsonRead(*it, "FlipbookColumns", component.FlipbookColumns);
				JsonRead(*it, "FlipbookRows", component.FlipbookRows);
				JsonRead(*it, "FlipbookFps", component.FlipbookFps);
				JsonRead(*it, "FlipbookRandomStart", component.FlipbookRandomStart);
				std::string facing = ParticleFacingToString(component.Facing);
				JsonRead(*it, "Facing", facing);
				component.Facing = ParticleFacingFromString(facing).value_or(component.Facing);
				JsonRead(*it, "StretchAmount", component.StretchAmount);
				JsonRead(*it, "StartRotation", component.StartRotation);
				JsonRead(*it, "RotationVariation", component.RotationVariation);
				JsonRead(*it, "SpinSpeed", component.SpinSpeed);
				JsonRead(*it, "SpinVariation", component.SpinVariation);
				ReadCurve(*it, "SizeCurve", component.SizeCurve);
				ReadGradient(*it, "ColorGradient", component.ColorGradient);
				component.Clamp();
			}

			if (auto it = json.find("Beam"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<BeamComponent>();
				JsonRead(*it, "Active", component.Active);
				std::string endMode = BeamEndModeToString(component.EndMode);
				JsonRead(*it, "EndMode", endMode);
				component.EndMode = BeamEndModeFromString(endMode).value_or(component.EndMode);
				JsonRead(*it, "EndPoint", component.EndPoint);
				uint64_t target = 0;
				JsonRead(*it, "Target", target);
				component.Target = UUID(target);
				JsonRead(*it, "Length", component.Length);
				JsonRead(*it, "StartWidth", component.StartWidth);
				JsonRead(*it, "EndWidth", component.EndWidth);
				JsonRead(*it, "StartColor", component.StartColor);
				JsonRead(*it, "EndColor", component.EndColor);
				JsonRead(*it, "Intensity", component.Intensity);
				std::string blend = ParticleBlendToString(component.Blend);
				JsonRead(*it, "Blend", blend);
				component.Blend = ParticleBlendFromString(blend).value_or(component.Blend);
				JsonRead(*it, "Texture", component.TexturePath);
				JsonRead(*it, "TileLength", component.TileLength);
				JsonRead(*it, "ScrollSpeed", component.ScrollSpeed);
				JsonRead(*it, "Segments", component.Segments);
				JsonRead(*it, "NoiseAmplitude", component.NoiseAmplitude);
				JsonRead(*it, "NoiseFrequency", component.NoiseFrequency);
				JsonRead(*it, "NoiseSpeed", component.NoiseSpeed);
				component.Clamp();
			}

			if (auto it = json.find("Trail"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<TrailComponent>();
				JsonRead(*it, "Emitting", component.Emitting);
				JsonRead(*it, "Lifetime", component.Lifetime);
				JsonRead(*it, "MinDistance", component.MinDistance);
				JsonRead(*it, "MaxPoints", component.MaxPoints);
				JsonRead(*it, "StartWidth", component.StartWidth);
				JsonRead(*it, "EndWidth", component.EndWidth);
				ReadCurve(*it, "WidthCurve", component.WidthCurve);
				JsonRead(*it, "StartColor", component.StartColor);
				JsonRead(*it, "EndColor", component.EndColor);
				ReadGradient(*it, "ColorGradient", component.ColorGradient);
				JsonRead(*it, "Intensity", component.Intensity);
				std::string blend = ParticleBlendToString(component.Blend);
				JsonRead(*it, "Blend", blend);
				component.Blend = ParticleBlendFromString(blend).value_or(component.Blend);
				JsonRead(*it, "Texture", component.TexturePath);
				std::string textureMode = TrailTextureModeToString(component.TextureMode);
				JsonRead(*it, "TextureMode", textureMode);
				component.TextureMode = TrailTextureModeFromString(textureMode).value_or(component.TextureMode);
				JsonRead(*it, "TileLength", component.TileLength);
				component.Clamp();
			}

			if (auto it = json.find("EffectShape"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<EffectShapeComponent>();
				std::string shape = EffectShapeTypeToString(component.Shape);
				JsonRead(*it, "Shape", shape);
				component.Shape = EffectShapeTypeFromString(shape).value_or(component.Shape);
				JsonRead(*it, "Radius", component.Radius);
				JsonRead(*it, "InnerRadius", component.InnerRadius);
				JsonRead(*it, "TopRadius", component.TopRadius);
				JsonRead(*it, "Height", component.Height);
				JsonRead(*it, "Arc", component.Arc);
				JsonRead(*it, "Segments", component.Segments);
				JsonRead(*it, "Playing", component.Playing);
				JsonRead(*it, "Loop", component.Loop);
				JsonRead(*it, "Duration", component.Duration);
				ReadCurve(*it, "SizeCurve", component.SizeCurve);
				ReadGradient(*it, "ColorGradient", component.ColorGradient);
				ReadCurve(*it, "DissolveCurve", component.DissolveCurve);
				JsonRead(*it, "SpinSpeed", component.SpinSpeed);
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "Intensity", component.Intensity);
				std::string blend = ParticleBlendToString(component.Blend);
				JsonRead(*it, "Blend", blend);
				component.Blend = ParticleBlendFromString(blend).value_or(component.Blend);
				JsonRead(*it, "Texture", component.TexturePath);
				JsonRead(*it, "Tiling", component.Tiling);
				JsonRead(*it, "Scroll", component.Scroll);
				JsonRead(*it, "EdgeFade", component.EdgeFade);
				JsonRead(*it, "Rim", component.Rim);
				JsonRead(*it, "Dissolve", component.Dissolve);
				JsonRead(*it, "DissolveEdgeWidth", component.DissolveEdgeWidth);
				JsonRead(*it, "DissolveEdgeColor", component.DissolveEdgeColor);
				JsonRead(*it, "DissolveScale", component.DissolveScale);
				JsonRead(*it, "Distortion", component.Distortion);
				JsonRead(*it, "DistortionScale", component.DistortionScale);
				JsonRead(*it, "DistortionSpeed", component.DistortionSpeed);
				component.Clamp();
			}

			if (auto it = json.find("Text"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<TextComponent>();
				JsonRead(*it, "Text", component.Text);
				JsonRead(*it, "Font", component.FontPath);
				JsonRead(*it, "Size", component.Size);
				JsonRead(*it, "Color", component.Color);
				std::string alignment = TextAlignmentToString(component.Alignment);
				JsonRead(*it, "Alignment", alignment);
				component.Alignment = TextAlignmentFromString(alignment);
				std::string verticalAlignment = TextVerticalAlignmentToString(component.VerticalAlignment);
				JsonRead(*it, "VerticalAlignment", verticalAlignment);
				component.VerticalAlignment = TextVerticalAlignmentFromString(verticalAlignment);
				JsonRead(*it, "BoxSize", component.BoxSize);
				JsonRead(*it, "Wrap", component.Wrap);
				std::string overflow = TextOverflowToString(component.Overflow);
				JsonRead(*it, "Overflow", overflow);
				component.Overflow = TextOverflowFromString(overflow);
				JsonRead(*it, "LineSpacing", component.LineSpacing);
				JsonRead(*it, "OutlineWidth", component.OutlineWidth);
				JsonRead(*it, "OutlineColor", component.OutlineColor);
				JsonRead(*it, "FaceCamera", component.FaceCamera);
				JsonRead(*it, "ThemeColor", component.ThemeColor);
				JsonRead(*it, "Visible", component.Visible);
			}

			if (auto it = json.find("UICanvas"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UICanvasComponent>();
				JsonRead(*it, "ReferenceSize", component.ReferenceSize);
				std::string scaleMode = UIScaleModeToString(component.ScaleMode);
				JsonRead(*it, "ScaleMode", scaleMode);
				component.ScaleMode = UIScaleModeFromString(scaleMode);
				JsonRead(*it, "SortOrder", component.SortOrder);
				JsonRead(*it, "Visible", component.Visible);
				JsonRead(*it, "Modal", component.Modal);
				JsonRead(*it, "Theme", component.Theme);
			}

			if (auto it = json.find("UIRect"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UIRectComponent>();
				JsonRead(*it, "AnchorMin", component.AnchorMin);
				JsonRead(*it, "AnchorMax", component.AnchorMax);
				JsonRead(*it, "Pivot", component.Pivot);
				JsonRead(*it, "OffsetMin", component.OffsetMin);
				JsonRead(*it, "OffsetMax", component.OffsetMax);
				JsonRead(*it, "Visible", component.Visible);
				JsonRead(*it, "Interactable", component.Interactable);
				JsonRead(*it, "Focusable", component.Focusable);
				JsonRead(*it, "StyleClass", component.StyleClass);
				std::string widthSizing = UISizingToString(component.WidthSizing), heightSizing = UISizingToString(component.HeightSizing);
				JsonRead(*it, "WidthSizing", widthSizing);
				JsonRead(*it, "HeightSizing", heightSizing);
				component.WidthSizing = UISizingFromString(widthSizing);
				component.HeightSizing = UISizingFromString(heightSizing);
			}

			if (auto it = json.find("UIImage"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UIImageComponent>();
				JsonRead(*it, "Texture", component.TexturePath);
				JsonRead(*it, "Color", component.Color);
				JsonRead(*it, "CornerRadius", component.CornerRadius);
				JsonRead(*it, "BorderWidth", component.BorderWidth);
				JsonRead(*it, "BorderColor", component.BorderColor);
				JsonRead(*it, "SliceBorder", component.SliceBorder);
				JsonRead(*it, "Visible", component.Visible);
			}

			if (auto it = json.find("UILayout"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UILayoutComponent>();
				std::string direction = UILayoutDirectionToString(component.Direction);
				std::string mainAlignment = UIAlignmentToString(component.MainAlignment), crossAlignment = UIAlignmentToString(component.CrossAlignment);
				JsonRead(*it, "Direction", direction);
				JsonRead(*it, "Spacing", component.Spacing);
				JsonRead(*it, "Padding", component.Padding);
				JsonRead(*it, "MainAlignment", mainAlignment);
				JsonRead(*it, "CrossAlignment", crossAlignment);
				JsonRead(*it, "Columns", component.Columns);
				component.Direction = UILayoutDirectionFromString(direction);
				component.MainAlignment = UIAlignmentFromString(mainAlignment);
				component.CrossAlignment = UIAlignmentFromString(crossAlignment);
			}

			if (auto it = json.find("UIScroll"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UIScrollComponent>();
				JsonRead(*it, "Horizontal", component.Horizontal);
				JsonRead(*it, "Vertical", component.Vertical);
				JsonRead(*it, "ScrollPosition", component.ScrollPosition);
				JsonRead(*it, "WheelSpeed", component.WheelSpeed);
				JsonRead(*it, "ShowScrollbars", component.ShowScrollbars);
			}

			if (auto it = json.find("UIButton"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UIButtonComponent>();
				// Only the colors a button sets are saved; the others come from the theme.
				const auto readColor = [&](const char* key, std::optional<glm::vec4>& color)
				{
					if (auto value = it->find(key); value != it->end() && value->is_array() && value->size() == 4
						&& std::all_of(value->begin(), value->end(), [](const Json& channel) { return channel.is_number(); }))
						color = value->get<glm::vec4>();
				};
				readColor("NormalColor", component.NormalColor);
				readColor("HoverColor", component.HoverColor);
				readColor("PressedColor", component.PressedColor);
				readColor("DisabledColor", component.DisabledColor);
			}

			if (auto it = json.find("UIToggle"); it != json.end())
				JsonRead(*it, "On", entity.AddOrReplaceComponent<UIToggleComponent>().On);

			if (auto it = json.find("UISlider"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UISliderComponent>();
				JsonRead(*it, "Min", component.Min);
				JsonRead(*it, "Max", component.Max);
				JsonRead(*it, "Step", component.Step);
				JsonRead(*it, "Value", component.Value);
			}

			if (auto it = json.find("UIDropdown"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UIDropdownComponent>();
				JsonRead(*it, "Options", component.Options);
				JsonRead(*it, "Selected", component.Selected);
			}

			if (auto it = json.find("UITextInput"); it != json.end())
			{
				auto& component = entity.AddOrReplaceComponent<UITextInputComponent>();
				JsonRead(*it, "Placeholder", component.Placeholder);
				JsonRead(*it, "MultiLine", component.MultiLine);
				JsonRead(*it, "Password", component.Password);
				JsonRead(*it, "MaxLength", component.MaxLength);
				component.MaxLength = std::max(component.MaxLength, 0);
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
		if (entity.GetComponent<TagComponent>().KeepAcrossScenes)
			json["KeepAcrossScenes"] = true;
		if (entity.GetComponent<TagComponent>().Pinned)
			json["Pinned"] = true;
		if (const std::vector<std::string>& tags = entity.GetComponent<TagComponent>().Tags; !tags.empty())
			json["Tags"] = tags;
		// Only a top-level entity is filed under a group, and only one the scene has.
		if (const UUID group = entity.GetComponent<TagComponent>().Group; group.IsValid() && !entity.GetParent() && entity.GetScene()->FindGroup(group))
			json["Group"] = static_cast<uint64_t>(group);

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
			j["RunWhilePaused"] = c->RunWhilePaused;
			Json fields = Json::object();
			for (const auto& [name, value] : c->Fields)
				fields[name] = SerializeScriptField(value);
			j["Fields"] = fields;
		}

		if (const auto* c = entity.TryGetComponent<AnimatorComponent>())
		{
			Json& j = json["Animator"];
			j["Clip"] = c->Clip;
			j["PlayOnStart"] = c->PlayOnStart;
			j["Loop"] = c->Loop;
			j["Speed"] = c->Speed;
		}

		if (const auto* c = entity.TryGetComponent<NavAgentComponent>())
		{
			Json& j = json["NavAgent"];
			j["Speed"] = c->Speed;
			j["Acceleration"] = c->Acceleration;
			j["Radius"] = c->Radius;
			j["Height"] = c->Height;
			j["BaseOffset"] = c->BaseOffset;
			j["StoppingDistance"] = c->StoppingDistance;
			j["TurnSpeed"] = c->TurnSpeed;
			j["AvoidOthers"] = c->AvoidOthers;
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

		if (const auto* c = entity.TryGetComponent<DecalComponent>())
		{
			Json& j = json["Decal"];
			j["Texture"] = c->TexturePath;
			j["Color"] = c->Color;
			j["Size"] = c->Size;
			j["MaxAngle"] = c->MaxAngle;
			j["SortOrder"] = c->SortOrder;
		}

		if (const auto* c = entity.TryGetComponent<ParticleEmitterComponent>())
		{
			Json& j = json["ParticleEmitter"];
			j["PlayOnStart"] = c->PlayOnStart;
			j["Looping"] = c->Looping;
			j["Duration"] = c->Duration;
			j["Rate"] = c->Rate;
			j["Burst"] = c->Burst;
			j["MaxParticles"] = c->MaxParticles;
			j["Lifetime"] = c->Lifetime;
			j["LifetimeVariation"] = c->LifetimeVariation;
			j["Speed"] = c->Speed;
			j["SpeedVariation"] = c->SpeedVariation;
			j["SpreadAngle"] = c->SpreadAngle;
			j["Shape"] = ParticleShapeToString(c->Shape);
			j["ShapeSize"] = c->ShapeSize;
			j["GravityScale"] = c->GravityScale;
			j["Drag"] = c->Drag;
			j["StartSize"] = c->StartSize;
			j["EndSize"] = c->EndSize;
			j["StartColor"] = c->StartColor;
			j["EndColor"] = c->EndColor;
			j["Intensity"] = c->Intensity;
			j["Blend"] = ParticleBlendToString(c->Blend);
			j["Texture"] = c->TexturePath;
			j["WorldSpace"] = c->WorldSpace;
			j["Outward"] = c->Outward;
			j["NoiseStrength"] = c->NoiseStrength;
			j["NoiseFrequency"] = c->NoiseFrequency;
			j["NoiseSpeed"] = c->NoiseSpeed;
			j["FlipbookColumns"] = c->FlipbookColumns;
			j["FlipbookRows"] = c->FlipbookRows;
			j["FlipbookFps"] = c->FlipbookFps;
			j["FlipbookRandomStart"] = c->FlipbookRandomStart;
			j["Facing"] = ParticleFacingToString(c->Facing);
			j["StretchAmount"] = c->StretchAmount;
			j["StartRotation"] = c->StartRotation;
			j["RotationVariation"] = c->RotationVariation;
			j["SpinSpeed"] = c->SpinSpeed;
			j["SpinVariation"] = c->SpinVariation;
			WriteCurve(j, "SizeCurve", c->SizeCurve);
			WriteGradient(j, "ColorGradient", c->ColorGradient);
		}

		if (const auto* c = entity.TryGetComponent<BeamComponent>())
		{
			Json& j = json["Beam"];
			j["Active"] = c->Active;
			j["EndMode"] = BeamEndModeToString(c->EndMode);
			j["EndPoint"] = c->EndPoint;
			j["Target"] = static_cast<uint64_t>(c->Target);
			j["Length"] = c->Length;
			j["StartWidth"] = c->StartWidth;
			j["EndWidth"] = c->EndWidth;
			j["StartColor"] = c->StartColor;
			j["EndColor"] = c->EndColor;
			j["Intensity"] = c->Intensity;
			j["Blend"] = ParticleBlendToString(c->Blend);
			j["Texture"] = c->TexturePath;
			j["TileLength"] = c->TileLength;
			j["ScrollSpeed"] = c->ScrollSpeed;
			j["Segments"] = c->Segments;
			j["NoiseAmplitude"] = c->NoiseAmplitude;
			j["NoiseFrequency"] = c->NoiseFrequency;
			j["NoiseSpeed"] = c->NoiseSpeed;
		}

		if (const auto* c = entity.TryGetComponent<EffectShapeComponent>())
		{
			Json& j = json["EffectShape"];
			j["Shape"] = EffectShapeTypeToString(c->Shape);
			j["Radius"] = c->Radius;
			j["InnerRadius"] = c->InnerRadius;
			j["TopRadius"] = c->TopRadius;
			j["Height"] = c->Height;
			j["Arc"] = c->Arc;
			j["Segments"] = c->Segments;
			j["Playing"] = c->Playing;
			j["Loop"] = c->Loop;
			j["Duration"] = c->Duration;
			WriteCurve(j, "SizeCurve", c->SizeCurve);
			WriteGradient(j, "ColorGradient", c->ColorGradient);
			WriteCurve(j, "DissolveCurve", c->DissolveCurve);
			j["SpinSpeed"] = c->SpinSpeed;
			j["Color"] = c->Color;
			j["Intensity"] = c->Intensity;
			j["Blend"] = ParticleBlendToString(c->Blend);
			j["Texture"] = c->TexturePath;
			j["Tiling"] = c->Tiling;
			j["Scroll"] = c->Scroll;
			j["EdgeFade"] = c->EdgeFade;
			j["Rim"] = c->Rim;
			j["Dissolve"] = c->Dissolve;
			j["DissolveEdgeWidth"] = c->DissolveEdgeWidth;
			j["DissolveEdgeColor"] = c->DissolveEdgeColor;
			j["DissolveScale"] = c->DissolveScale;
			j["Distortion"] = c->Distortion;
			j["DistortionScale"] = c->DistortionScale;
			j["DistortionSpeed"] = c->DistortionSpeed;
		}

		if (const auto* c = entity.TryGetComponent<TrailComponent>())
		{
			Json& j = json["Trail"];
			j["Emitting"] = c->Emitting;
			j["Lifetime"] = c->Lifetime;
			j["MinDistance"] = c->MinDistance;
			j["MaxPoints"] = c->MaxPoints;
			j["StartWidth"] = c->StartWidth;
			j["EndWidth"] = c->EndWidth;
			WriteCurve(j, "WidthCurve", c->WidthCurve);
			j["StartColor"] = c->StartColor;
			j["EndColor"] = c->EndColor;
			WriteGradient(j, "ColorGradient", c->ColorGradient);
			j["Intensity"] = c->Intensity;
			j["Blend"] = ParticleBlendToString(c->Blend);
			j["Texture"] = c->TexturePath;
			j["TextureMode"] = TrailTextureModeToString(c->TextureMode);
			j["TileLength"] = c->TileLength;
		}

		if (const auto* c = entity.TryGetComponent<TextComponent>())
		{
			Json& j = json["Text"];
			j["Text"] = c->Text;
			j["Font"] = c->FontPath;
			j["Size"] = c->Size;
			j["Color"] = c->Color;
			j["Alignment"] = TextAlignmentToString(c->Alignment);
			j["VerticalAlignment"] = TextVerticalAlignmentToString(c->VerticalAlignment);
			j["BoxSize"] = c->BoxSize;
			j["Wrap"] = c->Wrap;
			j["Overflow"] = TextOverflowToString(c->Overflow);
			j["LineSpacing"] = c->LineSpacing;
			j["OutlineWidth"] = c->OutlineWidth;
			j["OutlineColor"] = c->OutlineColor;
			j["FaceCamera"] = c->FaceCamera;
			if (c->ThemeColor)
				j["ThemeColor"] = true;
			j["Visible"] = c->Visible;
		}

		if (const auto* c = entity.TryGetComponent<UICanvasComponent>())
		{
			Json& j = json["UICanvas"];
			j["ReferenceSize"] = c->ReferenceSize;
			j["ScaleMode"] = UIScaleModeToString(c->ScaleMode);
			j["SortOrder"] = c->SortOrder;
			j["Visible"] = c->Visible;
			j["Modal"] = c->Modal;
			if (!c->Theme.empty())
				j["Theme"] = c->Theme;
		}

		if (const auto* c = entity.TryGetComponent<UIRectComponent>())
		{
			Json& j = json["UIRect"];
			j["AnchorMin"] = c->AnchorMin;
			j["AnchorMax"] = c->AnchorMax;
			j["Pivot"] = c->Pivot;
			j["OffsetMin"] = c->OffsetMin;
			j["OffsetMax"] = c->OffsetMax;
			j["Visible"] = c->Visible;
			j["Interactable"] = c->Interactable;
			j["Focusable"] = c->Focusable;
			if (!c->StyleClass.empty())
				j["StyleClass"] = c->StyleClass;
			j["WidthSizing"] = UISizingToString(c->WidthSizing);
			j["HeightSizing"] = UISizingToString(c->HeightSizing);
		}

		if (const auto* c = entity.TryGetComponent<UIImageComponent>())
		{
			Json& j = json["UIImage"];
			j["Texture"] = c->TexturePath;
			j["Color"] = c->Color;
			j["CornerRadius"] = c->CornerRadius;
			j["BorderWidth"] = c->BorderWidth;
			j["BorderColor"] = c->BorderColor;
			j["SliceBorder"] = c->SliceBorder;
			j["Visible"] = c->Visible;
		}

		if (const auto* c = entity.TryGetComponent<UILayoutComponent>())
		{
			Json& j = json["UILayout"];
			j["Direction"] = UILayoutDirectionToString(c->Direction);
			j["Spacing"] = c->Spacing;
			j["Padding"] = c->Padding;
			j["MainAlignment"] = UIAlignmentToString(c->MainAlignment);
			j["CrossAlignment"] = UIAlignmentToString(c->CrossAlignment);
			j["Columns"] = c->Columns;
		}

		if (const auto* c = entity.TryGetComponent<UIScrollComponent>())
		{
			Json& j = json["UIScroll"];
			j["Horizontal"] = c->Horizontal;
			j["Vertical"] = c->Vertical;
			j["ScrollPosition"] = c->ScrollPosition;
			j["WheelSpeed"] = c->WheelSpeed;
			j["ShowScrollbars"] = c->ShowScrollbars;
		}

		if (const auto* c = entity.TryGetComponent<UIButtonComponent>())
		{
			Json& j = json["UIButton"];
			j = Json::object();
			const auto writeColor = [&](const char* key, const std::optional<glm::vec4>& color)
			{
				if (color)
					j[key] = *color;
			};
			writeColor("NormalColor", c->NormalColor);
			writeColor("HoverColor", c->HoverColor);
			writeColor("PressedColor", c->PressedColor);
			writeColor("DisabledColor", c->DisabledColor);
		}

		if (const auto* c = entity.TryGetComponent<UIToggleComponent>())
			json["UIToggle"]["On"] = c->On;

		if (const auto* c = entity.TryGetComponent<UISliderComponent>())
		{
			Json& j = json["UISlider"];
			j["Min"] = c->Min;
			j["Max"] = c->Max;
			j["Step"] = c->Step;
			j["Value"] = c->Value;
		}

		if (const auto* c = entity.TryGetComponent<UIDropdownComponent>())
		{
			Json& j = json["UIDropdown"];
			j["Options"] = c->Options;
			j["Selected"] = c->Selected;
		}

		if (const auto* c = entity.TryGetComponent<UITextInputComponent>())
		{
			Json& j = json["UITextInput"];
			j["Placeholder"] = c->Placeholder;
			j["MultiLine"] = c->MultiLine;
			j["Password"] = c->Password;
			j["MaxLength"] = c->MaxLength;
		}

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
			JsonReadIssues issues;
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
			WarnUnreadValues(issues, entityJson, "Entity '" + name + "'");
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
		// A beam aimed at an entity of the same tree aims at it under its new ID.
		for (const auto& entry : created)
		{
			if (BeamComponent* beam = Entity(entry.first).TryGetComponent<BeamComponent>())
			{
				if (auto it = remap.find(static_cast<uint64_t>(beam->Target)); beam->Target != 0 && it != remap.end())
					beam->Target = it->second;
			}
		}
		// The tree's top entity keeps its group when the scene has it (a prefab placed in the scene it was
		// made in, a paste); otherwise it goes in none. SetParent cleared the children's.
		if (TagComponent& tag = root.GetComponent<TagComponent>(); root.GetParent() || !scene.FindGroup(tag.Group))
			tag.Group = 0;
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

		Json& navigation = json["Navigation"];
		navigation["AgentRadius"] = settings.Navigation.AgentRadius;
		navigation["AgentHeight"] = settings.Navigation.AgentHeight;
		navigation["MaxClimb"] = settings.Navigation.MaxClimb;
		navigation["MaxSlope"] = settings.Navigation.MaxSlope;
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
		if (auto it = json.find("Navigation"); it != json.end())
		{
			NavMeshSettings& navigation = settings.Navigation;
			JsonRead(*it, "AgentRadius", navigation.AgentRadius);
			JsonRead(*it, "AgentHeight", navigation.AgentHeight);
			JsonRead(*it, "MaxClimb", navigation.MaxClimb);
			JsonRead(*it, "MaxSlope", navigation.MaxSlope);
			navigation = navigation.Clamped();
		}
	}

	Json SceneSerializer::SerializeToJson(Scene& scene)
	{
		Json json;
		json["Scene"] = scene.GetName();
		json["Version"] = SceneFormatVersion;
		json["Settings"] = SerializeSettings(scene.GetSettings());
		if (!scene.GetGroups().empty())
			json["Groups"] = SerializeGroups(scene.GetGroups());

		Json entities = Json::array();
		for (Entity root : scene.GetRootEntities())
		{
			for (Json& entity : SerializeEntityTree(root))
				entities.push_back(std::move(entity));
		}
		json["Entities"] = std::move(entities);
		return json;
	}

	bool SceneSerializer::DeserializeFromJson(Scene& scene, const Json& json, bool skipExistingTrees)
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
		{
			JsonReadIssues issues;
			DeserializeSettings(*it, scene.GetSettings());
			WarnUnreadValues(issues, *it, "Scene settings");
		}

		const Json& entities = json["Entities"];
		if (!entities.is_array())
			return false;

		// The file's groups. On a scene change (skipExistingTrees), groups the kept entities are in stay;
		// a file group with a kept group's name is that group, so its entities join it.
		std::unordered_map<uint64_t, UUID> groupRemap;
		{
			std::vector<SceneGroup> groups = json.contains("Groups") ? DeserializeGroups(json["Groups"]) : std::vector<SceneGroup>();
			if (skipExistingTrees)
			{
				std::vector<SceneGroup> merged;
				for (Entity root : scene.GetRootEntities())
				{
					const SceneGroup* kept = scene.FindGroup(root.GetComponent<TagComponent>().Group);
					if (kept && std::none_of(merged.begin(), merged.end(), [&](const SceneGroup& g) { return g.ID == kept->ID; }))
						merged.push_back(*kept);
				}
				for (SceneGroup& group : groups)
				{
					auto same = std::find_if(merged.begin(), merged.end(), [&](const SceneGroup& g) { return g.ID == group.ID || SceneGroups::NamesMatch(g.Name, group.Name); });
					if (same != merged.end())
						groupRemap[static_cast<uint64_t>(group.ID)] = same->ID;
					else if (merged.size() < SceneGroups::MaxGroups)
						merged.push_back(std::move(group));
				}
				groups = std::move(merged);
			}
			scene.SetGroups(std::move(groups));
		}

		// Entities the scene already has (kept across a scene change) win over the file's copies, at
		// any depth: a kept player is left out of the file wherever it is there, under "Level" or at
		// the top, with the copy's children. Parents come before their children in the file.
		std::unordered_set<uint64_t> existing, skipped;
		if (skipExistingTrees)
		{
			for (const auto& [id, handle] : scene.m_EntityMap)
				existing.insert(static_cast<uint64_t>(id));
		}

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
			uint64_t id = 0, parentID = 0;
			JsonRead(entity, "ID", id);
			JsonRead(entity, "Parent", parentID);
			if (skipExistingTrees && ((id != 0 && existing.contains(id)) || (parentID != 0 && skipped.contains(parentID))))
			{
				skipped.insert(id);
				continue;
			}
			if (parentID == 0)
				flush();
			tree.push_back(entity);
			if (!groupRemap.empty())
			{
				uint64_t group = 0;
				JsonRead(entity, "Group", group);
				if (auto it = groupRemap.find(group); it != groupRemap.end())
					tree.back()["Group"] = static_cast<uint64_t>(it->second);
			}
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

	std::optional<Json> SceneSerializer::ReadSceneFile(const std::filesystem::path& path)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		if (!text)
		{
			BW_CORE_ERROR("Scene file '{}' could not be read", path.string());
			return std::nullopt;
		}
		Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded())
		{
			BW_CORE_ERROR("Scene file '{}' is not valid JSON", path.string());
			return std::nullopt;
		}
		return json;
	}

	bool SceneSerializer::Deserialize(Scene& scene, const std::filesystem::path& path)
	{
		const std::optional<Json> json = ReadSceneFile(path);
		if (!json || !DeserializeFromJson(scene, *json))
			return false;
		scene.SetNavMesh(LoadSceneNavMesh(path));
		return true;
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
