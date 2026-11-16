#pragma once

// Language-neutral building blocks of the script API.
//
// The engine API that scripts see is described once, in ScriptApiDefinition.h, using only the
// types in this header. Each scripting language (Python, ForgeScript) has a "binder" that turns that
// description into its own module, so both languages always offer the same functions.

#include "Bellows/Core/Base.h"
#include "Bellows/Core/InputNames.h"
#include "Bellows/Core/KeyCodes.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace Bellows::Scripting {

	// A mistake in the values a script passed (a missing asset, an unknown name, a hierarchy cycle).
	// Python raises it as ValueError; ForgeScript reports it as a normal script error.
	class ScriptValueError : public std::invalid_argument
	{
	public:
		explicit ScriptValueError(const std::string& message) : std::invalid_argument(message) {}
	};

	// -------------------------------------------------------------------------
	// Entities and the active scene
	// -------------------------------------------------------------------------

	// A script's handle to an entity. Holds IDs instead of pointers, so a stale handle fails with a
	// clear error after the entity or its scene is gone instead of crashing.
	struct ScriptEntity
	{
		uint64_t SceneID = 0;
		UUID ID = 0;

		ScriptEntity() = default;
		explicit ScriptEntity(Entity entity);

		Scene* GetScene() const;
		Entity TryResolve() const;
		// Throws std::runtime_error when the entity no longer exists.
		Entity Resolve() const;
		bool IsValid() const { return static_cast<bool>(TryResolve()); }

		bool operator==(const ScriptEntity& other) const { return SceneID == other.SceneID && ID == other.ID; }
	};

	// The scene that module-level functions (create_entity, find_entity, ...) operate on.
	void SetContextScene(Scene* scene);
	Scene* GetContextScene();			// May be null
	Scene& RequireContextScene();		// Throws std::runtime_error when there is none

	// Host callbacks (the runtime and the editor decide what quitting or loading a scene means).
	void SetQuitHandler(std::function<void(int)> handler);
	void SetLoadSceneHandler(std::function<bool(const std::string&)> handler);
	void RequestQuit(int exitCode);
	bool RequestLoadScene(const std::string& path);

	// Frame timing published to scripts.
	void SetFrameTiming(float deltaTime, float time, uint64_t frame);
	float GetDeltaTime();
	float GetTime();
	uint64_t GetFrame();

	std::optional<ScriptEntity> EntityOrNone(Entity entity);

	// -------------------------------------------------------------------------
	// Components
	// -------------------------------------------------------------------------

	// A script's handle to one component of an entity. Every access re-fetches the component,
	// so a handle never dangles; it fails clearly once the entity or component is gone.
	template<typename T>
	struct ComponentRef
	{
		ScriptEntity Owner;

		T& Get() const;
		Entity GetEntity() const { return Owner.Resolve(); }
	};

	using TransformRef = ComponentRef<TransformComponent>;
	using MeshRendererRef = ComponentRef<MeshRendererComponent>;
	using CameraRef = ComponentRef<CameraComponent>;
	using DirectionalLightRef = ComponentRef<DirectionalLightComponent>;
	using PointLightRef = ComponentRef<PointLightComponent>;
	using SpotLightRef = ComponentRef<SpotLightComponent>;
	using SkyLightRef = ComponentRef<SkyLightComponent>;
	using RigidBodyRef = ComponentRef<RigidBodyComponent>;
	using BoxColliderRef = ComponentRef<BoxColliderComponent>;
	using SphereColliderRef = ComponentRef<SphereColliderComponent>;
	using CapsuleColliderRef = ComponentRef<CapsuleColliderComponent>;
	using MeshColliderRef = ComponentRef<MeshColliderComponent>;
	using ScriptRef = ComponentRef<ScriptComponent>;
	using AudioSourceRef = ComponentRef<AudioSourceComponent>;
	using AudioListenerRef = ComponentRef<AudioListenerComponent>;

	// Any component handle (what get_component and add_component return).
	using ComponentHandle = std::variant<TransformRef, MeshRendererRef, CameraRef, DirectionalLightRef, PointLightRef,
		SpotLightRef, SkyLightRef, RigidBodyRef, BoxColliderRef, SphereColliderRef, CapsuleColliderRef, MeshColliderRef,
		ScriptRef, AudioSourceRef, AudioListenerRef>;

	struct ComponentInfo
	{
		const char* Name;
		std::function<bool(Entity)> Has;
		std::function<void(Entity)> Add;
		std::function<void(Entity)> Remove;
		std::function<ComponentHandle(const ScriptEntity&)> MakeRef;
	};

	// Every component scripts can use, in the order the engine lists them.
	const std::vector<ComponentInfo>& GetComponentInfos();
	// Finds a component by its script name ("RigidBody"); throws ScriptValueError listing the valid names.
	const ComponentInfo& FindComponentInfo(const std::string& name);
	const char* ComponentDisplayName(const std::type_info& type);

	template<typename T>
	T& ComponentRef<T>::Get() const
	{
		Entity entity = Owner.Resolve();
		if (!entity.HasComponent<T>())
			throw std::runtime_error("Entity '" + entity.GetName() + "' no longer has a " + ComponentDisplayName(typeid(T)) + " component");
		return entity.GetComponent<T>();
	}

	// -------------------------------------------------------------------------
	// Argument types that each language may accept in its own way
	// -------------------------------------------------------------------------

	// A component given by name. (Python also accepts the component class, e.g. bellows.RigidBody.)
	struct ComponentName
	{
		std::string Name;
	};

	// An RGBA color. Three components mean an opaque color.
	struct ColorValue
	{
		glm::vec4 Value { 1.0f };
	};

	// A keyboard key, usually given by name ("W", "space", "left_shift").
	struct KeyArg
	{
		KeyCode Code = KeyCode::Space;
	};

	// A mouse button: "left", "right" or "middle" (or a button number).
	struct MouseButtonArg
	{
		MouseButton Button = MouseButton::Left;
	};

	// Every key scripts can name (Bellows::KeyName), with the spelling Python's bellows.Key enum uses.
	const std::vector<KeyName>& GetKeyNames();
	// Accepts any capitalization and ignores spaces and underscores; "1" means D1. Throws ScriptValueError.
	KeyCode KeyFromName(const std::string& name);
	MouseButton MouseButtonFromName(const std::string& name);

	// A PBR material created or loaded by a script.
	struct ScriptMaterial
	{
		Ref<Material> Instance;
		std::string Path;
	};

	// What MeshRenderer.material accepts: a material, a material path, or nothing (use the mesh's own).
	using MaterialAssignment = std::optional<std::variant<ScriptMaterial, std::string>>;

	// -------------------------------------------------------------------------
	// Describing arguments
	// -------------------------------------------------------------------------

	// The name of a required argument.
	struct ArgName
	{
		const char* Name;
	};

	// The name of an optional argument and the value used when it is left out.
	template<typename T>
	struct ArgWithDefault
	{
		const char* Name;
		T Default;
	};

	inline ArgName Arg(const char* name) { return ArgName { name }; }

	template<typename T>
	ArgWithDefault<T> Arg(const char* name, T defaultValue) { return ArgWithDefault<T> { name, std::move(defaultValue) }; }

	// -------------------------------------------------------------------------
	// Engine helpers shared by every language
	// -------------------------------------------------------------------------

	glm::vec3 GetWorldPosition(const ScriptEntity& handle);
	void SetWorldPosition(const ScriptEntity& handle, const glm::vec3& position);
	void LookAt(const ScriptEntity& handle, const glm::vec3& target, const glm::vec3& up);
	// Moves the simulated physics body to the entity after its transform was changed by a script.
	void TeleportIfSimulated(Entity entity);
	// Pushes edited RigidBody settings to the simulated body (no-op outside play mode).
	void ApplyBodySettings(Entity entity);
	PhysicsWorld* GetPhysics(Entity entity);
	SceneAudio* GetAudio(Entity entity);
	// Registers a material under a unique runtime path so MeshRenderers can reference it.
	std::string RegisterRuntimeMaterial(const Ref<Material>& material);

}
