#pragma once

// The engine API that scripts can use, described once for every scripting language.
//
// DefineScriptApi is instantiated by each language's binder (PythonApi in ScriptBindings.cpp,
// ForgeScriptApi in ForgeScriptBindings.cpp). A binder provides:
//
//   api.Function(name, function, doc, Arg(...)...)
//   auto c = api.template Class<T>(name, doc);
//   c.Property(name, getter, setter, doc)   c.ReadOnly(name, getter, doc)
//   c.Method(name, function, doc, Arg(...)...)
//   c.Constructor(function, Arg(...)...)    c.Static(name, function, doc, Arg(...)...)
//
// Functions only use the language-neutral types from ScriptApi.h, so a new engine feature added here
// appears in Python and ForgeScript at once. Language-specific extras (Python operators and
// __repr__, script objects) are added by the binders themselves.

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Core/InputNames.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptApi.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Bellows::Scripting {

	namespace Detail {

		// Script calls that name an input action or gamepad button raise ValueError for unknown names.
		inline const std::string& RequireAction(const std::string& action)
		{
			if (!InputMap::HasAction(action))
				throw ScriptValueError("Unknown input action '" + action + "'. Actions are set in Project Settings > Input actions");
			return action;
		}

		inline GamepadButton RequireGamepadButton(const std::string& name)
		{
			if (std::optional<GamepadButton> button = FindGamepadButton(name))
				return *button;
			throw ScriptValueError("Unknown gamepad button '" + name + "'. Buttons: A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb, DPadUp, DPadRight, DPadDown, DPadLeft");
		}

		// Script calls that name an audio bus raise ValueError for unknown names.
		inline void RequireBus(const std::string& bus)
		{
			if (!AudioEngine::IsBus(bus))
				throw ScriptValueError("Unknown audio bus '" + bus + "'. Buses: Master, Music, Effects");
		}

		// Assigns (or clears) one texture slot of a material from an asset path.
		inline void SetMaterialTexture(Material& material, const std::optional<std::string>& value, std::string& pathSlot,
			TextureColorSpace colorSpace, void (Material::*setter)(Ref<Texture2D>))
		{
			if (!value || value->empty())
			{
				pathSlot.clear();
				(material.*setter)(nullptr);
				return;
			}
			Ref<Texture2D> texture = AssetManager::GetTexture(*value, colorSpace);
			if (!texture)
				throw ScriptValueError("Texture '" + *value + "' could not be loaded");
			pathSlot = *value;
			(material.*setter)(std::move(texture));
		}

		inline glm::vec2 ScriptViewportSize(const Scene& scene)
		{
			return Input::GetViewportSize().x > 0 ? Input::GetViewportSize()
				: glm::vec2(static_cast<float>(scene.GetViewportWidth()), static_cast<float>(scene.GetViewportHeight()));
		}

		inline Entity ResolveOptional(const std::optional<ScriptEntity>& entity)
		{
			return entity ? entity->Resolve() : Entity();
		}
	}

	template<typename Api>
	void DefineScriptApi(Api& api)
	{
		// --- Input ------------------------------------------------------------
		api.Function("is_key_down", [](const KeyArg& k) { return Input::IsKeyDown(k.Code); }, "True while the key is held", Arg("key"));
		api.Function("is_key_pressed", [](const KeyArg& k) { return Input::IsKeyPressed(k.Code); }, "True on the frame the key went down", Arg("key"));
		api.Function("is_key_released", [](const KeyArg& k) { return Input::IsKeyReleased(k.Code); }, "True on the frame the key went up", Arg("key"));
		api.Function("is_mouse_down", [](const MouseButtonArg& b) { return Input::IsMouseButtonDown(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("is_mouse_pressed", [](const MouseButtonArg& b) { return Input::IsMouseButtonPressed(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("is_mouse_released", [](const MouseButtonArg& b) { return Input::IsMouseButtonReleased(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("mouse_position", []() { return Input::GetMousePosition(); }, "Mouse position in viewport pixels (top-left origin)");
		api.Function("mouse_delta", []() { return Input::GetMouseDelta(); }, "Mouse movement this frame in pixels");
		api.Function("scroll_delta", []() { return Input::GetScrollDelta(); }, "Scroll wheel movement this frame");
		api.Function("viewport_size", []() { return Input::GetViewportSize(); }, "Size of the game viewport in pixels");
		api.Function("action_names", []()
			{
				std::vector<std::string> names;
				for (const auto& [name, bindings] : InputMap::GetActions())
					names.push_back(name);
				return names;
			}, "The project's input actions, sorted by name");
		api.Function("is_action_down", [](const std::string& action) { return InputMap::IsActionDown(Detail::RequireAction(action)); },
			"True while any input bound to the action is held", Arg("action"));
		api.Function("is_action_pressed", [](const std::string& action) { return InputMap::IsActionPressed(Detail::RequireAction(action)); },
			"True on the frame the action started", Arg("action"));
		api.Function("is_action_released", [](const std::string& action) { return InputMap::IsActionReleased(Detail::RequireAction(action)); },
			"True on the frame the action ended", Arg("action"));
		api.Function("gamepad_connected", []() { return Input::IsGamepadConnected(); }, "True while a gamepad is connected");
		api.Function("is_gamepad_down", [](const std::string& button) { return Input::IsGamepadButtonDown(Detail::RequireGamepadButton(button)); },
			"True while the gamepad button (\"A\", \"Start\", \"DPadLeft\", ...) is held", Arg("button"));
		api.Function("is_gamepad_pressed", [](const std::string& button) { return Input::IsGamepadButtonPressed(Detail::RequireGamepadButton(button)); },
			"True on the frame the gamepad button went down", Arg("button"));
		api.Function("is_gamepad_released", [](const std::string& button) { return Input::IsGamepadButtonReleased(Detail::RequireGamepadButton(button)); },
			"True on the frame the gamepad button went up", Arg("button"));

		// --- Time -------------------------------------------------------------
		api.Function("delta_time", []() { return GetDeltaTime(); }, "Seconds since the previous frame");
		api.Function("time", []() { return GetTime(); }, "Seconds since play started");
		// A signed count, so ForgeScript sees a number (its unsigned 64-bit values are IDs, shown as text).
		api.Function("frame", []() { return static_cast<int64_t>(GetFrame()); }, "Frames since play started");

		// --- Materials --------------------------------------------------------
		auto material = api.template Class<ScriptMaterial>("Material", "PBR material that can be assigned to a MeshRenderer");
		material.Constructor([](const std::string& name, const std::optional<ColorValue>& color, float metallic, float roughness)
			{
				ScriptMaterial result;
				result.Instance = CreateRef<Material>(name);
				MaterialProperties& properties = result.Instance->EditProperties();
				if (color)
					properties.AlbedoColor = color->Value;
				properties.Metallic = metallic;
				properties.Roughness = roughness;
				result.Path = RegisterRuntimeMaterial(result.Instance);
				return result;
			}, Arg("name", std::string("Material")), Arg("color", std::optional<ColorValue>()), Arg("metallic", 0.0f), Arg("roughness", 0.5f));
		material.Static("load", [](const std::string& path)
			{
				Ref<Material> loaded = AssetManager::GetMaterial(path);
				if (!loaded)
					throw ScriptValueError("Material '" + path + "' could not be loaded");
				return ScriptMaterial { loaded, path };
			}, "Loads a .bmat material from the project", Arg("path"));
		material.ReadOnly("path", [](const ScriptMaterial& m) { return m.Path; }, "");
		material.Property("name", [](const ScriptMaterial& m) { return m.Instance->GetName(); },
			[](const ScriptMaterial& m, const std::string& v) { m.Instance->SetName(v); }, "");
		material.Property("color", [](const ScriptMaterial& m) { return ColorValue { m.Instance->GetProperties().AlbedoColor }; },
			[](const ScriptMaterial& m, const ColorValue& v) { m.Instance->EditProperties().AlbedoColor = v.Value; }, "");
		material.Property("metallic", [](const ScriptMaterial& m) { return m.Instance->GetProperties().Metallic; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().Metallic = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("roughness", [](const ScriptMaterial& m) { return m.Instance->GetProperties().Roughness; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().Roughness = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("emissive", [](const ScriptMaterial& m) { return m.Instance->GetProperties().EmissiveColor; },
			[](const ScriptMaterial& m, const glm::vec3& v) { m.Instance->EditProperties().EmissiveColor = v; }, "");
		material.Property("emissive_intensity", [](const ScriptMaterial& m) { return m.Instance->GetProperties().EmissiveIntensity; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().EmissiveIntensity = std::max(v, 0.0f); }, "");
		material.Property("alpha_mode", [](const ScriptMaterial& m) { return std::string(AlphaModeToString(m.Instance->GetProperties().Mode)); },
			[](const ScriptMaterial& m, const std::string& v) { m.Instance->EditProperties().Mode = AlphaModeFromString(v); }, "");
		material.Property("double_sided", [](const ScriptMaterial& m) { return m.Instance->GetProperties().DoubleSided; },
			[](const ScriptMaterial& m, bool v) { m.Instance->EditProperties().DoubleSided = v; }, "");
		material.Property("normal_strength", [](const ScriptMaterial& m) { return m.Instance->GetProperties().NormalStrength; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().NormalStrength = std::max(v, 0.0f); }, "");
		material.Property("alpha_cutoff", [](const ScriptMaterial& m) { return m.Instance->GetProperties().AlphaCutoff; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().AlphaCutoff = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("uv_tiling", [](const ScriptMaterial& m) { return m.Instance->GetProperties().UVTiling; },
			[](const ScriptMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVTiling = v; }, "");
		material.Property("uv_offset", [](const ScriptMaterial& m) { return m.Instance->GetProperties().UVOffset; },
			[](const ScriptMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVOffset = v; }, "");
		material.Property("albedo_map", [](const ScriptMaterial& m) { return m.Instance->AlbedoMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->AlbedoMapPath, TextureColorSpace::SRGB, &Material::SetAlbedoMap); },
			"Albedo (base color) texture path, or None");
		material.Property("normal_map", [](const ScriptMaterial& m) { return m.Instance->NormalMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->NormalMapPath, TextureColorSpace::Linear, &Material::SetNormalMap); },
			"Tangent-space normal map path, or None");
		material.Property("metallic_roughness_map", [](const ScriptMaterial& m) { return m.Instance->MetallicRoughnessMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->MetallicRoughnessMapPath, TextureColorSpace::Linear, &Material::SetMetallicRoughnessMap); },
			"glTF metallic (B) / roughness (G) texture path, or None");
		material.Property("occlusion_map", [](const ScriptMaterial& m) { return m.Instance->OcclusionMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->OcclusionMapPath, TextureColorSpace::Linear, &Material::SetOcclusionMap); },
			"Ambient occlusion (R) texture path, or None");
		material.Property("emissive_map", [](const ScriptMaterial& m) { return m.Instance->EmissiveMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->EmissiveMapPath, TextureColorSpace::SRGB, &Material::SetEmissiveMap); },
			"Emissive texture path, or None");
		material.Method("save", [](ScriptMaterial& m, const std::string& path)
			{
				if (!AssetManager::SaveMaterial(m.Instance, path))
					throw std::runtime_error("Could not save material to '" + path + "'");
				m.Path = path;
			}, "Writes the material to a .bmat file in the project", Arg("path"));

		// --- Components -------------------------------------------------------
		auto transform = api.template Class<TransformRef>("Transform", "Position, rotation and scale relative to the parent");
		transform.Property("translation", [](const TransformRef& r) { return r.Get().Translation; },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("position", [](const TransformRef& r) { return r.Get().Translation; },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); }, "Local position (same as translation)");
		transform.Property("rotation", [](const TransformRef& r) { return r.Get().GetRotationEuler(); },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(v); TeleportIfSimulated(r.GetEntity()); }, "Euler angles in radians");
		transform.Property("rotation_degrees", [](const TransformRef& r) { return glm::degrees(r.Get().GetRotationEuler()); },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("quaternion", [](const TransformRef& r) { return r.Get().GetRotation(); },
			[](const TransformRef& r, const glm::quat& q) { r.Get().SetRotation(q); TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("scale", [](const TransformRef& r) { return r.Get().Scale; }, [](const TransformRef& r, const glm::vec3& v) { r.Get().Scale = v; }, "");
		transform.Property("world_position", [](const TransformRef& r) { return GetWorldPosition(r.Owner); },
			[](const TransformRef& r, const glm::vec3& v) { SetWorldPosition(r.Owner, v); }, "");
		transform.ReadOnly("forward", [](const TransformRef& r) { return r.Get().GetForward(); }, "");
		transform.ReadOnly("right", [](const TransformRef& r) { return r.Get().GetRight(); }, "");
		transform.ReadOnly("up", [](const TransformRef& r) { return r.Get().GetUp(); }, "");

		auto meshRenderer = api.template Class<MeshRendererRef>("MeshRenderer", "Draws a mesh (glTF file or Builtin/Cube, Sphere, Plane, Capsule, Cylinder, Cone, Quad)");
		meshRenderer.Property("mesh", [](const MeshRendererRef& r) { return r.Get().MeshPath; }, [](const MeshRendererRef& r, const std::string& v) { r.Get().MeshPath = v; }, "");
		meshRenderer.Property("material", [](const MeshRendererRef& r) { return r.Get().MaterialPath; },
			[](const MeshRendererRef& r, const MaterialAssignment& v)
			{
				if (!v)
					r.Get().MaterialPath.clear();
				else if (const ScriptMaterial* m = std::get_if<ScriptMaterial>(&*v))
					r.Get().MaterialPath = m->Path;
				else
					r.Get().MaterialPath = std::get<std::string>(*v);
			}, "Material path (.bmat or runtime material); None uses the mesh's own materials");
		meshRenderer.Property("color", [](const MeshRendererRef& r)
			{
				MeshRendererComponent& c = r.Get();
				if (c.RuntimeMaterial) return ColorValue { c.RuntimeMaterial->GetProperties().AlbedoColor };
				if (c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty()) return ColorValue { c.RuntimeMesh->GetMaterials()[0]->GetProperties().AlbedoColor };
				return ColorValue { glm::vec4(1.0f) };
			},
			[](const MeshRendererRef& r, const ColorValue& v)
			{
				// Gives this entity its own material instance so other entities are unaffected.
				MeshRendererComponent& c = r.Get();
				const std::string uniquePrefix = "Runtime/Entity_" + std::to_string(static_cast<uint64_t>(r.Owner.ID));
				if (c.MaterialPath != uniquePrefix)
				{
					Ref<Material> base = c.RuntimeMaterial;
					if (!base && c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty())
						base = c.RuntimeMesh->GetMaterials()[0];
					Ref<Material> instance = base ? base->Clone() : CreateRef<Material>("Default");
					AssetManager::RegisterMaterial(uniquePrefix, instance);
					c.MaterialPath = uniquePrefix;
					c.RuntimeMaterial = instance;
					c.LoadedMaterialPath = uniquePrefix;
				}
				c.RuntimeMaterial->EditProperties().AlbedoColor = v.Value;
			}, "Albedo color; setting it gives the entity its own material");
		meshRenderer.Property("cast_shadows", [](const MeshRendererRef& r) { return r.Get().CastShadows; }, [](const MeshRendererRef& r, bool v) { r.Get().CastShadows = v; }, "");
		meshRenderer.Property("visible", [](const MeshRendererRef& r) { return r.Get().Visible; }, [](const MeshRendererRef& r, bool v) { r.Get().Visible = v; }, "");

		auto camera = api.template Class<CameraRef>("Camera", "");
		camera.Property("projection", [](const CameraRef& r) { return std::string(r.Get().Projection == ProjectionType::Orthographic ? "Orthographic" : "Perspective"); },
			[](const CameraRef& r, const std::string& v) { r.Get().Projection = v == "Orthographic" ? ProjectionType::Orthographic : ProjectionType::Perspective; }, "");
		camera.Property("fov", [](const CameraRef& r) { return glm::degrees(r.Get().FieldOfView); },
			[](const CameraRef& r, float v) { r.Get().FieldOfView = glm::radians(std::clamp(v, 1.0f, 179.0f)); }, "Vertical field of view in degrees");
		camera.Property("orthographic_size", [](const CameraRef& r) { return r.Get().OrthographicSize; }, [](const CameraRef& r, float v) { r.Get().OrthographicSize = std::max(v, 0.001f); }, "");
		camera.Property("near_clip", [](const CameraRef& r) { return r.Get().NearClip; }, [](const CameraRef& r, float v) { r.Get().NearClip = std::max(v, 0.0001f); }, "");
		camera.Property("far_clip", [](const CameraRef& r) { return r.Get().FarClip; }, [](const CameraRef& r, float v) { r.Get().FarClip = v; }, "");
		camera.Property("primary", [](const CameraRef& r) { return r.Get().Primary; }, [](const CameraRef& r, bool v) { r.Get().Primary = v; }, "");

		auto directionalLight = api.template Class<DirectionalLightRef>("DirectionalLight", "Sun-like light shining along the entity's forward (-Z)");
		directionalLight.Property("color", [](const DirectionalLightRef& r) { return r.Get().Color; }, [](const DirectionalLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		directionalLight.Property("intensity", [](const DirectionalLightRef& r) { return r.Get().Intensity; }, [](const DirectionalLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		directionalLight.Property("cast_shadows", [](const DirectionalLightRef& r) { return r.Get().CastShadows; }, [](const DirectionalLightRef& r, bool v) { r.Get().CastShadows = v; }, "");

		auto pointLight = api.template Class<PointLightRef>("PointLight", "");
		pointLight.Property("color", [](const PointLightRef& r) { return r.Get().Color; }, [](const PointLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		pointLight.Property("intensity", [](const PointLightRef& r) { return r.Get().Intensity; }, [](const PointLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		pointLight.Property("range", [](const PointLightRef& r) { return r.Get().Range; }, [](const PointLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); }, "");

		auto spotLight = api.template Class<SpotLightRef>("SpotLight", "Cone light shining along the entity's forward (-Z)");
		spotLight.Property("color", [](const SpotLightRef& r) { return r.Get().Color; }, [](const SpotLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		spotLight.Property("intensity", [](const SpotLightRef& r) { return r.Get().Intensity; }, [](const SpotLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		spotLight.Property("range", [](const SpotLightRef& r) { return r.Get().Range; }, [](const SpotLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); }, "");
		spotLight.Property("inner_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().InnerAngle); }, [](const SpotLightRef& r, float v) { r.Get().InnerAngle = glm::radians(v); }, "Degrees");
		spotLight.Property("outer_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().OuterAngle); }, [](const SpotLightRef& r, float v) { r.Get().OuterAngle = glm::radians(v); }, "Degrees");

		auto skyLight = api.template Class<SkyLightRef>("SkyLight", "Environment lighting: an HDRI (.hdr) or the procedural sky");
		skyLight.Property("environment", [](const SkyLightRef& r) { return r.Get().EnvironmentPath; }, [](const SkyLightRef& r, const std::string& v) { r.Get().EnvironmentPath = v; }, "");
		skyLight.Property("intensity", [](const SkyLightRef& r) { return r.Get().Intensity; }, [](const SkyLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		skyLight.Property("rotation", [](const SkyLightRef& r) { return glm::degrees(r.Get().Rotation); }, [](const SkyLightRef& r, float v) { r.Get().Rotation = glm::radians(v); }, "Degrees");
		skyLight.Property("skybox_blur", [](const SkyLightRef& r) { return r.Get().SkyboxBlur; }, [](const SkyLightRef& r, float v) { r.Get().SkyboxBlur = std::max(v, 0.0f); }, "");

		auto rigidBody = api.template Class<RigidBodyRef>("RigidBody", "Physics body. Needs a collider component on the same entity.");
		rigidBody.Property("type", [](const RigidBodyRef& r) { return std::string(RigidBodyTypeToString(r.Get().Type)); },
			[](const RigidBodyRef& r, const std::string& v) { r.Get().Type = RigidBodyTypeFromString(v); }, "'Static', 'Dynamic' or 'Kinematic' (set before play)");
		rigidBody.Property("mass", [](const RigidBodyRef& r) { return r.Get().Mass; }, [](const RigidBodyRef& r, float v) { r.Get().Mass = std::max(v, 0.001f); }, "");
		rigidBody.Property("friction", [](const RigidBodyRef& r) { return r.Get().Friction; }, [](const RigidBodyRef& r, float v) { r.Get().Friction = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("restitution", [](const RigidBodyRef& r) { return r.Get().Restitution; }, [](const RigidBodyRef& r, float v) { r.Get().Restitution = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("linear_damping", [](const RigidBodyRef& r) { return r.Get().LinearDamping; }, [](const RigidBodyRef& r, float v) { r.Get().LinearDamping = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("angular_damping", [](const RigidBodyRef& r) { return r.Get().AngularDamping; }, [](const RigidBodyRef& r, float v) { r.Get().AngularDamping = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("gravity_scale", [](const RigidBodyRef& r) { return r.Get().GravityScale; }, [](const RigidBodyRef& r, float v) { r.Get().GravityScale = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("is_trigger", [](const RigidBodyRef& r) { return r.Get().IsTrigger; }, [](const RigidBodyRef& r, bool v) { r.Get().IsTrigger = v; }, "");
		rigidBody.Property("lock_rotation_x", [](const RigidBodyRef& r) { return r.Get().LockRotationX; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationX = v; }, "");
		rigidBody.Property("lock_rotation_y", [](const RigidBodyRef& r) { return r.Get().LockRotationY; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationY = v; }, "");
		rigidBody.Property("lock_rotation_z", [](const RigidBodyRef& r) { return r.Get().LockRotationZ; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationZ = v; }, "");
		rigidBody.Property("continuous", [](const RigidBodyRef& r) { return r.Get().ContinuousCollision; }, [](const RigidBodyRef& r, bool v) { r.Get().ContinuousCollision = v; },
			"Continuous collision detection for fast bodies (set before play)");
		rigidBody.Property("velocity",
			[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetLinearVelocity(e) : glm::vec3(0.0f); },
			[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetLinearVelocity(e, v); }, "Linear velocity (play mode)");
		rigidBody.Property("angular_velocity",
			[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetAngularVelocity(e) : glm::vec3(0.0f); },
			[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetAngularVelocity(e, v); }, "");
		rigidBody.Method("add_force", [](const RigidBodyRef& r, const glm::vec3& f) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddForce(e, f); }, "", Arg("force"));
		rigidBody.Method("add_impulse", [](const RigidBodyRef& r, const glm::vec3& i) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddImpulse(e, i); }, "", Arg("impulse"));
		rigidBody.Method("add_torque", [](const RigidBodyRef& r, const glm::vec3& t) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddTorque(e, t); }, "", Arg("torque"));
		rigidBody.Method("teleport", [](const RigidBodyRef& r, const glm::vec3& position) { r.Get(); SetWorldPosition(r.Owner, position); }, "", Arg("position"));

		auto boxCollider = api.template Class<BoxColliderRef>("BoxCollider", "");
		boxCollider.Property("half_extents", [](const BoxColliderRef& r) { return r.Get().HalfExtents; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().HalfExtents = v; }, "");
		boxCollider.Property("offset", [](const BoxColliderRef& r) { return r.Get().Offset; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto sphereCollider = api.template Class<SphereColliderRef>("SphereCollider", "");
		sphereCollider.Property("radius", [](const SphereColliderRef& r) { return r.Get().Radius; }, [](const SphereColliderRef& r, float v) { r.Get().Radius = v; }, "");
		sphereCollider.Property("offset", [](const SphereColliderRef& r) { return r.Get().Offset; }, [](const SphereColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto capsuleCollider = api.template Class<CapsuleColliderRef>("CapsuleCollider", "");
		capsuleCollider.Property("radius", [](const CapsuleColliderRef& r) { return r.Get().Radius; }, [](const CapsuleColliderRef& r, float v) { r.Get().Radius = v; }, "");
		capsuleCollider.Property("half_height", [](const CapsuleColliderRef& r) { return r.Get().HalfHeight; }, [](const CapsuleColliderRef& r, float v) { r.Get().HalfHeight = v; }, "");
		capsuleCollider.Property("offset", [](const CapsuleColliderRef& r) { return r.Get().Offset; }, [](const CapsuleColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto meshCollider = api.template Class<MeshColliderRef>("MeshCollider", "Static triangle-mesh collider built from the MeshRenderer");
		meshCollider.Property("use_mesh_renderer", [](const MeshColliderRef& r) { return r.Get().UseMeshRenderer; }, [](const MeshColliderRef& r, bool v) { r.Get().UseMeshRenderer = v; }, "");

		auto script = api.template Class<ScriptRef>("ScriptComponent", "Attaches a script to the entity");
		script.Property("path", [](const ScriptRef& r) { return r.Get().ScriptPath; }, [](const ScriptRef& r, const std::string& v) { r.Get().ScriptPath = v; }, "");
		script.Property("class_name", [](const ScriptRef& r) { return r.Get().ClassName; }, [](const ScriptRef& r, const std::string& v) { r.Get().ClassName = v; }, "");

		auto audioSource = api.template Class<AudioSourceRef>("AudioSource", "");
		audioSource.Property("clip", [](const AudioSourceRef& r) { return r.Get().ClipPath; }, [](const AudioSourceRef& r, const std::string& v) { r.Get().ClipPath = v; }, "");
		audioSource.Property("volume", [](const AudioSourceRef& r) { return r.Get().Volume; },
			[](const AudioSourceRef& r, float v) { r.Get().Volume = std::max(v, 0.0f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("pitch", [](const AudioSourceRef& r) { return r.Get().Pitch; },
			[](const AudioSourceRef& r, float v) { r.Get().Pitch = std::max(v, 0.01f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("loop", [](const AudioSourceRef& r) { return r.Get().Loop; },
			[](const AudioSourceRef& r, bool v) { r.Get().Loop = v; Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("play_on_start", [](const AudioSourceRef& r) { return r.Get().PlayOnStart; }, [](const AudioSourceRef& r, bool v) { r.Get().PlayOnStart = v; }, "");
		audioSource.Property("spatial", [](const AudioSourceRef& r) { return r.Get().Spatial; }, [](const AudioSourceRef& r, bool v) { r.Get().Spatial = v; }, "");
		audioSource.Property("min_distance", [](const AudioSourceRef& r) { return r.Get().MinDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MinDistance = v; }, "");
		audioSource.Property("max_distance", [](const AudioSourceRef& r) { return r.Get().MaxDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MaxDistance = v; }, "");
		audioSource.Property("bus", [](const AudioSourceRef& r) { return r.Get().Bus; },
			[](const AudioSourceRef& r, const std::string& v)
			{
				Detail::RequireBus(v);
				r.Get().Bus = v;
			}, "The bus the source plays on: 'Master', 'Music' or 'Effects'");
		audioSource.Method("play", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->Play(e) : false; }, "");
		audioSource.Method("stop", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Stop(e); }, "");
		audioSource.Method("pause", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Pause(e); }, "");
		audioSource.ReadOnly("is_playing", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->IsPlaying(e) : false; }, "");

		auto audioListener = api.template Class<AudioListenerRef>("AudioListener", "");
		audioListener.Property("active", [](const AudioListenerRef& r) { return r.Get().Active; }, [](const AudioListenerRef& r, bool v) { r.Get().Active = v; }, "");

		// --- Entity -----------------------------------------------------------
		auto entity = api.template Class<ScriptEntity>("Entity", "A handle to an entity in a scene");
		entity.ReadOnly("id", [](const ScriptEntity& e) { return static_cast<uint64_t>(e.ID); }, "");
		entity.ReadOnly("valid", [](const ScriptEntity& e) { return e.IsValid(); }, "False once the entity has been destroyed");
		entity.Property("name", [](const ScriptEntity& e) { return e.Resolve().GetName(); }, [](const ScriptEntity& e, const std::string& v) { e.Resolve().SetName(v); }, "");
		entity.ReadOnly("transform", [](const ScriptEntity& e) { e.Resolve(); return TransformRef { e }; }, "");
		entity.Property("position", [](const ScriptEntity& e) { return GetWorldPosition(e); }, [](const ScriptEntity& e, const glm::vec3& v) { SetWorldPosition(e, v); }, "World-space position");
		entity.Property("local_position", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Translation; },
			[](const ScriptEntity& e, const glm::vec3& v) { Entity resolved = e.Resolve(); resolved.GetComponent<TransformComponent>().Translation = v; TeleportIfSimulated(resolved); }, "");
		entity.Property("rotation", [](const ScriptEntity& e) { return glm::degrees(e.Resolve().GetComponent<TransformComponent>().GetRotationEuler()); },
			[](const ScriptEntity& e, const glm::vec3& v) { Entity resolved = e.Resolve(); resolved.GetComponent<TransformComponent>().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(resolved); },
			"Local rotation as Euler angles in degrees");
		entity.Property("scale", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Scale; },
			[](const ScriptEntity& e, const glm::vec3& v) { e.Resolve().GetComponent<TransformComponent>().Scale = v; }, "");
		entity.ReadOnly("forward", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(-glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[2])); }, "");
		entity.ReadOnly("right", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[0])); }, "");
		entity.ReadOnly("up", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[1])); }, "");
		entity.Property("parent",
			[](const ScriptEntity& e) { return EntityOrNone(e.Resolve().GetParent()); },
			[](const ScriptEntity& e, const std::optional<ScriptEntity>& parent)
			{
				Entity resolved = e.Resolve();
				if (!resolved.GetScene()->SetParent(resolved, Detail::ResolveOptional(parent), true))
					throw ScriptValueError("Cannot create a cycle in the hierarchy");
			}, "");
		entity.ReadOnly("children", [](const ScriptEntity& e)
			{
				std::vector<ScriptEntity> children;
				for (Entity child : e.Resolve().GetChildren())
					children.emplace_back(child);
				return children;
			}, "");
		entity.Method("find_child", [](const ScriptEntity& e, const std::string& name)
			{
				std::function<Entity(Entity)> search = [&](Entity current) -> Entity
				{
					for (Entity child : current.GetChildren())
					{
						if (child.GetName() == name)
							return child;
						if (Entity found = search(child))
							return found;
					}
					return {};
				};
				return EntityOrNone(search(e.Resolve()));
			}, "Finds a descendant by name", Arg("name"));
		entity.Method("has_component", [](const ScriptEntity& e, const ComponentName& component) { return FindComponentInfo(component.Name).Has(e.Resolve()); }, "", Arg("component"));
		entity.Method("get_component", [](const ScriptEntity& e, const ComponentName& component) -> std::optional<ComponentHandle>
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				if (!info.Has(e.Resolve()))
					return std::nullopt;
				return info.MakeRef(e);
			}, "Returns the component or None, e.g. entity.get_component('RigidBody')", Arg("component"));
		entity.Method("add_component", [](const ScriptEntity& e, const ComponentName& component)
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				info.Add(e.Resolve());
				return info.MakeRef(e);
			}, "Adds the component (if missing) and returns it", Arg("component"));
		entity.Method("remove_component", [](const ScriptEntity& e, const ComponentName& component)
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				if (std::string(info.Name) == "Transform")
					throw ScriptValueError("The Transform component cannot be removed");
				Entity resolved = e.Resolve();
				if (std::string(info.Name) == "RigidBody" && GetPhysics(resolved))
					GetPhysics(resolved)->DestroyBody(resolved);
				info.Remove(resolved);
			}, "", Arg("component"));
		entity.ReadOnly("components", [](const ScriptEntity& e)
			{
				Entity resolved = e.Resolve();
				std::vector<std::string> names;
				for (const ComponentInfo& info : GetComponentInfos())
				{
					if (info.Has(resolved))
						names.emplace_back(info.Name);
				}
				return names;
			}, "Names of the components on this entity");
		entity.Method("destroy", [](const ScriptEntity& e) { if (Entity resolved = e.TryResolve()) resolved.GetScene()->DestroyEntity(resolved); },
			"Destroys the entity (at the end of the frame during play)");
		entity.Method("look_at", [](const ScriptEntity& e, const glm::vec3& target, const glm::vec3& up) { LookAt(e, target, up); },
			"Rotates the entity so -Z faces the target", Arg("target"), Arg("up", glm::vec3(0.0f, 1.0f, 0.0f)));
		entity.Method("translate", [](const ScriptEntity& e, const glm::vec3& delta) { SetWorldPosition(e, GetWorldPosition(e) + delta); }, "", Arg("delta"));
		entity.Method("rotate", [](const ScriptEntity& e, const glm::vec3& degrees)
			{
				Entity resolved = e.Resolve();
				TransformComponent& component = resolved.GetComponent<TransformComponent>();
				component.SetRotation(component.GetRotation() * glm::quat(glm::radians(degrees)));
				TeleportIfSimulated(resolved);
			}, "Rotates by Euler angles (degrees) in local space", Arg("degrees"));

		// --- Physics ----------------------------------------------------------
		auto raycastHit = api.template Class<RaycastHit>("RaycastHit", "");
		raycastHit.ReadOnly("entity", [](const RaycastHit& hit) -> std::optional<ScriptEntity>
			{
				Scene* scene = GetContextScene();
				return scene ? EntityOrNone(scene->TryGetEntityByUUID(hit.Entity)) : std::nullopt;
			}, "");
		raycastHit.ReadOnly("point", [](const RaycastHit& hit) { return hit.Point; }, "");
		raycastHit.ReadOnly("normal", [](const RaycastHit& hit) { return hit.Normal; }, "");
		raycastHit.ReadOnly("distance", [](const RaycastHit& hit) { return hit.Distance; }, "");

		api.Function("raycast", [](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> std::optional<RaycastHit>
			{
				Scene& scene = RequireContextScene();
				if (!scene.GetPhysicsWorld())
					return std::nullopt;
				RaycastHit hit;
				if (!scene.GetPhysicsWorld()->Raycast(origin, direction, maxDistance, hit))
					return std::nullopt;
				return hit;
			}, "Casts a ray against physics colliders (play mode); returns RaycastHit or None",
			Arg("origin"), Arg("direction"), Arg("max_distance", 1000.0f));

		api.Function("get_gravity", []() { return RequireContextScene().GetSettings().Gravity; }, "");
		api.Function("set_gravity", [](const glm::vec3& gravity) { RequireContextScene().GetSettings().Gravity = gravity; }, "", Arg("gravity"));

		// --- Scene ------------------------------------------------------------
		api.Function("create_entity", [](const std::string& name, const std::optional<ScriptEntity>& parent)
			{
				Scene& scene = RequireContextScene();
				return ScriptEntity(scene.CreateChildEntity(Detail::ResolveOptional(parent), name));
			}, "Creates an entity in the active scene", Arg("name", std::string("Entity")), Arg("parent", std::optional<ScriptEntity>()));
		api.Function("destroy", [](const ScriptEntity& target) { if (Entity e = target.TryResolve()) e.GetScene()->DestroyEntity(e); }, "", Arg("entity"));
		api.Function("find_entity", [](const std::string& name) { return EntityOrNone(RequireContextScene().FindEntityByName(name)); }, "", Arg("name"));
		api.Function("get_entity", [](uint64_t id) { return EntityOrNone(RequireContextScene().TryGetEntityByUUID(UUID(id))); }, "", Arg("id"));
		api.Function("get_entities", [](const std::optional<ComponentName>& component)
			{
				Scene& scene = RequireContextScene();
				std::vector<ScriptEntity> result;
				const ComponentInfo* info = component ? &FindComponentInfo(component->Name) : nullptr;
				for (Entity candidate : scene.GetAllEntities())
				{
					if (scene.IsPendingDestroy(candidate))
						continue;
					if (!info || info->Has(candidate))
						result.emplace_back(candidate);
				}
				return result;
			}, "All entities (optionally only those with a component), in hierarchy order", Arg("component", std::optional<ComponentName>()));
		api.Function("instantiate", [](const std::string& prefab, const std::optional<glm::vec3>& position, const std::optional<ScriptEntity>& parent)
			{
				Scene& scene = RequireContextScene();
				Entity root = SceneSerializer::InstantiatePrefab(scene, prefab, Detail::ResolveOptional(parent));
				if (!root)
					throw ScriptValueError("Prefab '" + prefab + "' could not be instantiated");
				if (position)
					root.GetComponent<TransformComponent>().Translation = *position;
				return ScriptEntity(root);
			}, "Spawns a .bprefab and returns its root entity",
			Arg("prefab"), Arg("position", std::optional<glm::vec3>()), Arg("parent", std::optional<ScriptEntity>()));
		api.Function("primary_camera", []() { return EntityOrNone(RequireContextScene().GetPrimaryCameraEntity()); }, "");
		api.Function("screen_to_world_ray", [](const glm::vec2& screenPosition) -> std::optional<std::pair<glm::vec3, glm::vec3>>
			{
				Scene& scene = RequireContextScene();
				const glm::vec2 viewport = Detail::ScriptViewportSize(scene);
				RenderCamera view;
				if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), view))
					return std::nullopt;
				glm::vec3 origin(0.0f), direction(0.0f);
				Math::ScreenPointToRay(screenPosition, viewport, glm::inverse(view.Projection * view.View), origin, direction);
				return std::make_pair(origin, direction);
			}, "Returns (origin, direction) of a ray through a viewport pixel, or None without a camera", Arg("screen_position"));
		api.Function("world_to_screen", [](const glm::vec3& worldPosition) -> std::optional<glm::vec2>
			{
				Scene& scene = RequireContextScene();
				const glm::vec2 viewport = Detail::ScriptViewportSize(scene);
				RenderCamera view;
				glm::vec2 result(0.0f);
				if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), view)
					|| !Math::WorldToScreenPoint(worldPosition, view.Projection * view.View, viewport, result))
					return std::nullopt;
				return result;
			}, "", Arg("world_position"));
		api.Function("scene_name", []() { return RequireContextScene().GetName(); }, "");
		api.Function("is_playing", []() { Scene* scene = GetContextScene(); return scene && scene->IsRunning(); }, "");
		api.Function("load_scene", [](const std::string& path) { return RequestLoadScene(path); }, "Switches to another scene at the end of the frame", Arg("path"));
		api.Function("save_screenshot", [](const std::string& path, int maxWidth) { return SaveScreenshot(path, maxWidth); },
			"Saves what the player sees as a PNG (or JPEG for .jpg) and returns its full path; \"\" names it Screenshots/<scene>-<date>-<time>.png and relative paths are inside user_folder()",
			Arg("path", std::string()), Arg("max_width", 1920));
		api.Function("user_folder", []() { return GetUserFolder().string(); },
			"The game's own writable folder for saves, screenshots and reports");
		api.Function("quit", [](int exitCode) { RequestQuit(exitCode); }, "Quits the game (stops play mode in the editor)", Arg("exit_code", 0));

		// --- Audio ------------------------------------------------------------
		api.Function("play_sound", [](const std::string& path, float volume, const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::PlayOneShot(path, volume, bus);
			}, "Plays a one-shot 2D sound", Arg("path"), Arg("volume", 1.0f), Arg("bus", std::string("Effects")));
		api.Function("set_master_volume", [](float volume) { AudioEngine::SetMasterVolume(volume); }, "", Arg("volume"));
		api.Function("get_master_volume", []() { return AudioEngine::GetMasterVolume(); }, "");
		api.Function("bus_names", []() { return AudioEngine::GetBusNames(); }, "The audio buses: Master, Music, Effects");
		api.Function("set_bus_volume", [](const std::string& bus, float volume)
			{
				Detail::RequireBus(bus);
				AudioEngine::SetBusVolume(bus, volume);
			}, "Sets a bus's volume (1 = normal, 0 = silent)", Arg("bus"), Arg("volume"));
		api.Function("get_bus_volume", [](const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::GetBusVolume(bus);
			}, "", Arg("bus"));
		api.Function("set_bus_muted", [](const std::string& bus, bool muted)
			{
				Detail::RequireBus(bus);
				AudioEngine::SetBusMuted(bus, muted);
			}, "Mutes or unmutes a bus; its volume is kept", Arg("bus"), Arg("muted", true));
		api.Function("is_bus_muted", [](const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::IsBusMuted(bus);
			}, "", Arg("bus"));
	}

}
