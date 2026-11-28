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
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
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

	// -------------------------------------------------------------------------
	// Script events between entities and languages
	// -------------------------------------------------------------------------

	// A value sent with a script event, in a form every scripting language can read: nothing,
	// true/false, a number, text, a vector, an entity, a list of these, or a map from text to these
	// (kept in order).
	struct ScriptValue;
	using ScriptValueList = std::vector<ScriptValue>;
	using ScriptValueMap = std::vector<std::pair<std::string, ScriptValue>>;

	struct ScriptValue
	{
		std::variant<std::monostate, bool, double, std::string, glm::vec3, ScriptEntity, ScriptValueList, ScriptValueMap> Data;

		ScriptValue() = default;
		template<typename T>
		ScriptValue(T value) : Data(std::move(value)) {}

		bool IsNothing() const { return std::holds_alternative<std::monostate>(Data); }
		bool operator==(const ScriptValue& other) const { return Data == other.Data; }
	};

	// Who an event goes to. A broadcast (no Target) reaches every script, as 'trigger' and bellows.emit
	// do. With a Target, a listener hands the event only to the scripts attached to that entity. Source
	// is the entity that raised the event (a UI widget), or empty: ForgeScript compares its name for
	// handlers written 'when "Start" is clicked'.
	struct ScriptEventDelivery
	{
		std::optional<ScriptEntity> Target;
		std::optional<ScriptEntity> Source;
	};

	// Receives every script event: ForgeScript's 'trigger door opened with self', Python's
	// bellows.emit("door opened", self.entity), and the engine's UI events. Each language registers one
	// listener that hands events to its scripts (ForgeScript's 'when door opened with who', Python's
	// on_event(self, name, *values)). A listener must not throw: it reports its own scripts' errors
	// (including a ScriptValueError from an EmitScriptEvent its scripts called), the same way it reports
	// any other script error.
	using ScriptEventListener = std::function<void(const std::string& name, const ScriptValueList& values,
		const ScriptEventDelivery& delivery)>;
	using ScriptEventListenerID = uint64_t;

	ScriptEventListenerID AddScriptEventListener(ScriptEventListener listener);
	void RemoveScriptEventListener(ScriptEventListenerID id);
	// Delivers an event right away to every listener, in the order they were added, except 'skip' (the
	// sender's own listener, when the sender has already delivered it to its own scripts). Events sent
	// from inside an event handler nest; past 64 levels, or past 10,000 events caused by one outer event,
	// this throws ScriptValueError instead, so scripts that answer each other's events forever stop
	// with a clear error.
	void EmitScriptEvent(const std::string& name, const ScriptValueList& values, ScriptEventListenerID skip = 0);
	// Delivers an event raised by 'source' (a UI widget's clicked, changed, ...) to the scripts on
	// 'source', then to those on its parent, and so on up to the root, so one script on a menu hears
	// all its buttons. At each entity every listener runs in the order they were added, so the order is
	// by entity first, whatever language each script is in. 'values' go as given (UI events end with
	// the widget). The walk stops at an entity that no longer exists. The whole walk is one level of
	// nesting, with the same limits as EmitScriptEvent.
	void EmitScriptEventFrom(ScriptEntity source, const std::string& name, const ScriptValueList& values);
	// Delivers an event to every script, as EmitScriptEvent does, naming the entity it is about as its
	// Source (with no Target), so ForgeScript's 'when "Hero" is destination reached' hears only Hero's.
	void EmitScriptEventAbout(ScriptEntity source, const std::string& name, const ScriptValueList& values);
	// The same, sent later, from FlushQueuedScriptEvents: for events raised where no script may run,
	// like a script variable changing in the middle of that script's own instruction ('field changed').
	void QueueScriptEventAbout(ScriptEntity source, const std::string& name, const ScriptValueList& values);
	// Sends the queued events in order (skipping those about entities that are gone), and those their
	// handlers queue, up to c_MaxQueuedEventsPerFlush
	// (past that the rest are dropped with an error, so two fields that keep changing each other stop).
	// The scene calls it after scripts run; ClearQueuedScriptEvents drops them when play stops.
	void FlushQueuedScriptEvents();
	void ClearQueuedScriptEvents();
	inline constexpr size_t c_MaxQueuedEventsPerFlush = 10000;

	// A script error, as each language's runtime reports it to scripts that watch for errors (a crash
	// reporter). File is the script's asset path with forward slashes ("Scripts/player.py"), or a file
	// name when the code is outside the assets; Line is 0 when unknown. Stack lists the calls, innermost
	// first, each "file:line in function", or "file:line" for code at the top of a file.
	struct ScriptErrorReport
	{
		std::string Message;
		std::string File;
		int Line = 0;
		std::vector<std::string> Stack;
		std::string Language;	// "python" or "forgescript"
		std::string Name;		// The error's name (Python's exception class), or "" when it has none
	};

	// Broadcasts the 'script error' event with the values message, file, line, stack, language and
	// name, in that order. The caller still logs the error. An error raised while a 'script error'
	// handler runs is not sent again (it is only logged), so a broken handler cannot loop. Never throws.
	void ReportScriptError(const ScriptErrorReport& report);

	// Broadcasts 'game quitting' (no values) once, just before a normal quit: the window closing,
	// quit(), the end of a --frames run, and the editor's Stop. It is not sent when load_scene() changes
	// the scene. Handlers run to completion before anything shuts down, so they can still save or send
	// over the network. Never throws.
	void EmitGameQuitting();
	// Sends 'scene loaded' (name, path) to every script, after load_scene has loaded a scene into the running one.
	void EmitSceneLoaded(const std::string& name, const std::string& path);

	// The scene that module-level functions (create_entity, find_entity, ...) operate on.
	void SetContextScene(Scene* scene);
	Scene* GetContextScene();			// May be null
	Scene& RequireContextScene();		// Throws std::runtime_error when there is none

	// Host callbacks (the runtime and the editor decide what quitting or loading a scene means).
	void SetQuitHandler(std::function<void(int)> handler);
	// Whether the game was started from the editor: playing in its viewport, or in a game window it
	// started. Set by the host; false in exported games and runtimes started by hand.
	void SetStartedFromEditor(bool started);
	bool IsStartedFromEditor();
	void SetLoadSceneHandler(std::function<bool(const std::string&)> handler);
	// Returns the image the player sees right now (the game view, without editor overlays), or
	// nullopt when nothing has been drawn yet.
	void SetScreenshotHandler(std::function<std::optional<Image>()> handler);
	void RequestQuit(int exitCode);
	bool RequestLoadScene(const std::string& path);

	// The game's own folder for saves, screenshots and reports, writable even when the game is
	// installed read-only: <user data>/MageCapBellows/Games/<project name>.
	std::filesystem::path GetUserFolder();
	// GetUserFolder()/Logs: where a game playing in its own window writes Game.log and, from its last
	// run, Game.previous.log.
	std::filesystem::path GetLogFolder();
	// Saves what the player sees as PNG (or JPEG for .jpg/.jpeg) and returns the absolute path.
	// "" names it Screenshots/<scene>-<date>-<time>.png and a relative path is inside GetUserFolder().
	// max_width > 0 scales a wider image down to that width. Throws ScriptValueError.
	std::string SaveScreenshot(const std::string& path, int maxWidth);

	// Saved data, for progress that outlives a play session: GetUserFolder()/Saves/<name>.json. A name is
	// 1 to 64 letters, digits, spaces, '-' and '_' (not starting or ending with a space, and not a
	// name Windows reserves, like CON), so it always stays a plain file in that folder; others throw
	// ScriptValueError.
	//
	// SaveData writes nothing, true/false, numbers, text, vectors, and lists and maps of these (an
	// entity only exists for one play session, so it is refused with ScriptValueError, like infinite
	// numbers). The file is written next to the old one and then renamed over it, so a crash never
	// leaves half a save. Returns false, with a Console warning, when the file can't be written.
	bool SaveData(const std::string& name, const ScriptValue& value);
	// The saved value, or 'fallback' when there is no save. A save that can't be read is renamed to
	// <name>.damaged.json with a Console warning, so the game's next save can't overwrite what is
	// left of it, and 'fallback' comes back: a broken save never stops a game.
	ScriptValue LoadData(const std::string& name, const ScriptValue& fallback);
	bool HasData(const std::string& name);
	// Returns false when there was nothing to delete.
	bool DeleteData(const std::string& name);
	// Real-world seconds since January 1, 1970 (UTC). Unlike GetTime(), it keeps counting while the
	// game is closed, so a game can tell how long the player was away.
	double GetRealTime();

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
	using AnimatorRef = ComponentRef<AnimatorComponent>;
	using NavAgentRef = ComponentRef<NavAgentComponent>;
	using AudioListenerRef = ComponentRef<AudioListenerComponent>;
	using TextRef = ComponentRef<TextComponent>;
	using ParticleEmitterRef = ComponentRef<ParticleEmitterComponent>;
	using TrailRef = ComponentRef<TrailComponent>;
	using BeamRef = ComponentRef<BeamComponent>;
	using EffectShapeRef = ComponentRef<EffectShapeComponent>;
	using DecalRef = ComponentRef<DecalComponent>;
	using UICanvasRef = ComponentRef<UICanvasComponent>;
	using UIRectRef = ComponentRef<UIRectComponent>;
	using UIImageRef = ComponentRef<UIImageComponent>;
	using UILayoutRef = ComponentRef<UILayoutComponent>;
	using UIScrollRef = ComponentRef<UIScrollComponent>;
	using UIButtonRef = ComponentRef<UIButtonComponent>;
	using UIToggleRef = ComponentRef<UIToggleComponent>;
	using UISliderRef = ComponentRef<UISliderComponent>;
	using UIDropdownRef = ComponentRef<UIDropdownComponent>;
	using UITextInputRef = ComponentRef<UITextInputComponent>;

	// Any component handle (what get_component and add_component return).
	using ComponentHandle = std::variant<TransformRef, MeshRendererRef, CameraRef, DirectionalLightRef, PointLightRef,
		SpotLightRef, SkyLightRef, RigidBodyRef, BoxColliderRef, SphereColliderRef, CapsuleColliderRef, MeshColliderRef,
		ScriptRef, AudioSourceRef, AnimatorRef, NavAgentRef, AudioListenerRef, TextRef, ParticleEmitterRef, TrailRef, BeamRef, EffectShapeRef, DecalRef, UICanvasRef, UIRectRef, UIImageRef, UILayoutRef, UIScrollRef,
		UIButtonRef, UIToggleRef, UISliderRef, UIDropdownRef, UITextInputRef>;

	// A handle to a tween (what tween and tween_by return). It keeps the entity and property it was
	// made for, so they can still be read after the tween has ended.
	struct ScriptTween
	{
		uint64_t SceneID = 0;
		uint64_t ID = 0;
		ScriptEntity Target;
		std::string Property;

		bool operator==(const ScriptTween& other) const { return SceneID == other.SceneID && ID == other.ID; }
	};

	// A script's handle to a Hierarchy group (Scene/SceneGroups.h): IDs, so a deleted group fails with a
	// clear error instead of crashing.
	struct ScriptGroup
	{
		uint64_t SceneID = 0;
		UUID ID = 0;

		bool operator==(const ScriptGroup& other) const { return SceneID == other.SceneID && ID == other.ID; }
	};

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

	// Vectors and colors scripts give the engine must hold ordinary numbers: a NaN or infinite position
	// spreads through physics and rendering, and saves as null. Both languages check every vector
	// argument and property value with this (Python's Vec3 can hold NaN; ForgeScript can make infinity
	// with 1e308 * 10, and 1e39 becomes infinity as a float).
	template<typename T>
	bool HasOnlyFiniteNumbers(const T& value)
	{
		if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
		{
			for (typename T::length_type i = 0; i < T::length(); i++)
			{
				if (!std::isfinite(value[i]))
					return false;
			}
			return true;
		}
		else if constexpr (std::is_same_v<T, ColorValue>)
			return HasOnlyFiniteNumbers(value.Value);
		else
			return true;
	}

	template<typename T>
	bool HasOnlyFiniteNumbers(const std::optional<T>& value)
	{
		return !value || HasOnlyFiniteNumbers(*value);
	}

	// Throws ScriptValueError (Python's ValueError) naming 'what' when 'value' isn't finite.
	template<typename T>
	void RequireFiniteNumbers(const T& value, const std::string& what)
	{
		if (!HasOnlyFiniteNumbers(value))
			throw ScriptValueError(what + " needs ordinary numbers, not NaN or infinity");
	}

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
