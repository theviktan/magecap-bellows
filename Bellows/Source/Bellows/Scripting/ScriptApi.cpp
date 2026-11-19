#include "bwpch.h"
#include "Bellows/Scripting/ScriptApi.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Project/Project.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cctype>
#include <cmath>
#include <ctime>

namespace Bellows::Scripting {

	namespace {

		// The pointer is only used after the ID confirms the scene still exists, because the scene may
		// have been deleted while it was still the context (reading a deleted scene is undefined).
		Scene* s_ContextScene = nullptr;
		uint64_t s_ContextSceneID = 0;
		std::function<void(int)> s_QuitHandler;
		std::function<bool(const std::string&)> s_LoadSceneHandler;
		std::function<std::optional<Image>()> s_ScreenshotHandler;
		float s_DeltaTime = 0.0f;
		float s_Time = 0.0f;
		uint64_t s_Frame = 0;
		uint64_t s_RuntimeMaterialCounter = 0;

		template<typename T>
		ComponentInfo MakeComponentInfo(const char* name)
		{
			return ComponentInfo {
				name,
				[](Entity e) { return e.HasComponent<T>(); },
				[](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); },
				[](Entity e) { if (e.HasComponent<T>()) e.RemoveComponent<T>(); },
				[](const ScriptEntity& owner) { return ComponentHandle(ComponentRef<T> { owner }); },
			};
		}

	}

	// -------------------------------------------------------------------------
	// Entities and the active scene
	// -------------------------------------------------------------------------

	ScriptEntity::ScriptEntity(Entity entity)
		: SceneID(entity ? entity.GetScene()->GetID() : 0), ID(entity ? entity.GetUUID() : UUID(0))
	{
	}

	Scene* ScriptEntity::GetScene() const
	{
		return Scene::GetSceneByID(SceneID);
	}

	Entity ScriptEntity::TryResolve() const
	{
		Scene* scene = GetScene();
		return scene ? scene->TryGetEntityByUUID(ID) : Entity();
	}

	Entity ScriptEntity::Resolve() const
	{
		Entity entity = TryResolve();
		if (!entity)
			throw std::runtime_error("This entity no longer exists (it was destroyed or its scene was unloaded)");
		return entity;
	}

	void SetContextScene(Scene* scene)
	{
		s_ContextScene = scene;
		s_ContextSceneID = scene ? scene->GetID() : 0;
	}

	Scene* GetContextScene()
	{
		if (!s_ContextScene)
			return nullptr;
		Scene* live = Scene::GetSceneByID(s_ContextSceneID);
		return live == s_ContextScene ? live : nullptr;
	}

	Scene& RequireContextScene()
	{
		Scene* scene = GetContextScene();
		if (!scene)
			throw std::runtime_error("No scene is active");
		return *scene;
	}

	void SetQuitHandler(std::function<void(int)> handler) { s_QuitHandler = std::move(handler); }
	void SetLoadSceneHandler(std::function<bool(const std::string&)> handler) { s_LoadSceneHandler = std::move(handler); }
	void SetScreenshotHandler(std::function<std::optional<Image>()> handler) { s_ScreenshotHandler = std::move(handler); }

	void RequestQuit(int exitCode)
	{
		if (s_QuitHandler)
			s_QuitHandler(exitCode);
	}

	bool RequestLoadScene(const std::string& path)
	{
		return s_LoadSceneHandler ? s_LoadSceneHandler(path) : false;
	}

	namespace {

		// File names keep letters, digits, spaces, '-', '_' and '.'; anything else becomes '_'.
		std::string SafeFileName(const std::string& name, const char* fallback)
		{
			std::string safe;
			for (unsigned char c : name)
				safe += std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.' || c >= 0x80 ? static_cast<char>(c) : '_';
			while (!safe.empty() && (safe.back() == '.' || safe.back() == ' '))
				safe.pop_back();
			return safe.empty() ? fallback : safe;
		}

		std::string Timestamp()
		{
			const std::time_t now = std::time(nullptr);
			std::tm local {};
#if defined(BW_PLATFORM_WINDOWS)
			localtime_s(&local, &now);
#else
			localtime_r(&now, &local);
#endif
			char text[32];
			std::strftime(text, sizeof(text), "%Y-%m-%d-%H%M%S", &local);
			return text;
		}

	}

	std::filesystem::path GetUserFolder()
	{
		const Ref<Project> project = Project::GetActive();
		const std::string game = SafeFileName(project ? project->GetConfig().Name : std::string(), "Untitled");
		return Platform::GetUserDataDirectory() / "MageCapBellows" / "Games" / std::filesystem::path(game);
	}

	std::string SaveScreenshot(const std::string& path, int maxWidth)
	{
		if (maxWidth < 0)
			throw ScriptValueError("max_width can't be negative (0 keeps the full size)");

		std::filesystem::path file;
		if (path.empty())
		{
			const Scene* scene = GetContextScene();
			const std::string stem = SafeFileName(scene ? scene->GetName() : std::string(), "Screenshot") + "-" + Timestamp();
			const std::filesystem::path folder = GetUserFolder() / "Screenshots";
			file = folder / std::filesystem::path(stem + ".png");
			// Two shots in the same second get -2, -3, ...
			for (int n = 2; std::filesystem::exists(file) && n < 1000; n++)
				file = folder / std::filesystem::path(stem + "-" + std::to_string(n) + ".png");
		}
		else
		{
			file = std::filesystem::path(path);
			if (file.is_relative())
				file = GetUserFolder() / file;
			std::string extension = file.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension.empty())
				file += ".png";
			else if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
				throw ScriptValueError("Screenshots are saved as .png, .jpg or .jpeg, not '" + extension + "'");
		}

		std::optional<Image> image = s_ScreenshotHandler ? s_ScreenshotHandler() : std::nullopt;
		if (!image || !image->IsValid())
			throw ScriptValueError("There is no game image to save yet: nothing has been drawn, or this program shows no game view");
		if (maxWidth > 0 && image->Width > static_cast<uint32_t>(maxWidth))
		{
			const uint32_t width = static_cast<uint32_t>(maxWidth);
			const uint32_t height = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(image->Height) * width / image->Width)));
			image = image->Resized(width, height);
		}

		file = std::filesystem::absolute(file);
		if (!image->SaveOpaque(file))
			throw ScriptValueError("Could not write the screenshot to '" + file.string() + "'");
		return file.string();
	}

	void SetFrameTiming(float deltaTime, float time, uint64_t frame)
	{
		s_DeltaTime = deltaTime;
		s_Time = time;
		s_Frame = frame;
	}

	float GetDeltaTime() { return s_DeltaTime; }
	float GetTime() { return s_Time; }
	uint64_t GetFrame() { return s_Frame; }

	std::optional<ScriptEntity> EntityOrNone(Entity entity)
	{
		if (!entity)
			return std::nullopt;
		return ScriptEntity(entity);
	}

	// -------------------------------------------------------------------------
	// Components
	// -------------------------------------------------------------------------

	const std::vector<ComponentInfo>& GetComponentInfos()
	{
		static const std::vector<ComponentInfo> s_Infos = {
			MakeComponentInfo<TransformComponent>("Transform"),
			MakeComponentInfo<MeshRendererComponent>("MeshRenderer"),
			MakeComponentInfo<CameraComponent>("Camera"),
			MakeComponentInfo<DirectionalLightComponent>("DirectionalLight"),
			MakeComponentInfo<PointLightComponent>("PointLight"),
			MakeComponentInfo<SpotLightComponent>("SpotLight"),
			MakeComponentInfo<SkyLightComponent>("SkyLight"),
			MakeComponentInfo<RigidBodyComponent>("RigidBody"),
			MakeComponentInfo<BoxColliderComponent>("BoxCollider"),
			MakeComponentInfo<SphereColliderComponent>("SphereCollider"),
			MakeComponentInfo<CapsuleColliderComponent>("CapsuleCollider"),
			MakeComponentInfo<MeshColliderComponent>("MeshCollider"),
			MakeComponentInfo<ScriptComponent>("Script"),
			MakeComponentInfo<AudioSourceComponent>("AudioSource"),
			MakeComponentInfo<AudioListenerComponent>("AudioListener"),
		};
		return s_Infos;
	}

	const ComponentInfo& FindComponentInfo(const std::string& requested)
	{
		// The Script component's Python class is ScriptComponent (bellows.Script is the script base class).
		const std::string name = requested == "ScriptComponent" ? std::string("Script") : requested;
		for (const ComponentInfo& info : GetComponentInfos())
		{
			if (name == info.Name)
				return info;
		}

		std::string valid;
		for (const ComponentInfo& info : GetComponentInfos())
			valid += (valid.empty() ? "" : ", ") + std::string(info.Name);
		throw ScriptValueError("Unknown component '" + requested + "'. Valid components: " + valid);
	}

	const char* ComponentDisplayName(const std::type_info& type)
	{
		if (type == typeid(TransformComponent)) return "Transform";
		if (type == typeid(MeshRendererComponent)) return "MeshRenderer";
		if (type == typeid(CameraComponent)) return "Camera";
		if (type == typeid(DirectionalLightComponent)) return "DirectionalLight";
		if (type == typeid(PointLightComponent)) return "PointLight";
		if (type == typeid(SpotLightComponent)) return "SpotLight";
		if (type == typeid(SkyLightComponent)) return "SkyLight";
		if (type == typeid(RigidBodyComponent)) return "RigidBody";
		if (type == typeid(BoxColliderComponent)) return "BoxCollider";
		if (type == typeid(SphereColliderComponent)) return "SphereCollider";
		if (type == typeid(CapsuleColliderComponent)) return "CapsuleCollider";
		if (type == typeid(MeshColliderComponent)) return "MeshCollider";
		if (type == typeid(ScriptComponent)) return "Script";
		if (type == typeid(AudioSourceComponent)) return "AudioSource";
		if (type == typeid(AudioListenerComponent)) return "AudioListener";
		return "Unknown";
	}

	// -------------------------------------------------------------------------
	// Keys and mouse buttons
	// -------------------------------------------------------------------------

	const std::vector<KeyName>& GetKeyNames()
	{
		return GetKeyCodeNames();
	}

	KeyCode KeyFromName(const std::string& name)
	{
		if (std::optional<KeyCode> key = FindKeyCode(name))
			return *key;
		throw ScriptValueError("Unknown key '" + name + "'");
	}

	MouseButton MouseButtonFromName(const std::string& name)
	{
		if (std::optional<MouseButton> button = FindMouseButton(name))
			return *button;
		throw ScriptValueError("Expected a mouse button: 'left', 'right', 'middle', 'back' or 'forward'");
	}

	// -------------------------------------------------------------------------
	// Engine helpers
	// -------------------------------------------------------------------------

	glm::vec3 GetWorldPosition(const ScriptEntity& handle)
	{
		Entity entity = handle.Resolve();
		return entity.GetScene()->GetWorldPosition(entity);
	}

	void TeleportIfSimulated(Entity entity)
	{
		Scene* scene = entity.GetScene();
		if (scene->GetPhysicsWorld() && scene->GetPhysicsWorld()->HasBody(entity))
			scene->GetPhysicsWorld()->TeleportToEntity(entity);
	}

	void SetWorldPosition(const ScriptEntity& handle, const glm::vec3& position)
	{
		Entity entity = handle.Resolve();
		Scene* scene = entity.GetScene();
		glm::mat4 world = scene->GetWorldTransform(entity);
		world[3] = glm::vec4(position, 1.0f);
		scene->SetWorldTransform(entity, world);
		TeleportIfSimulated(entity);
	}

	void LookAt(const ScriptEntity& handle, const glm::vec3& target, const glm::vec3& up)
	{
		Entity entity = handle.Resolve();
		Scene* scene = entity.GetScene();
		const glm::mat4 world = scene->GetWorldTransform(entity);
		glm::vec3 position, scale;
		glm::quat rotation;
		Math::DecomposeTransform(world, position, rotation, scale);
		glm::vec3 direction = target - position;
		if (glm::length(direction) < 1e-6f)
			return;
		direction = glm::normalize(direction);
		glm::vec3 worldUp = up;
		if (std::abs(glm::dot(direction, glm::normalize(worldUp))) > 0.999f)
			worldUp = std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
		const glm::quat look = glm::quatLookAtRH(direction, worldUp);
		scene->SetWorldTransform(entity, Math::ComposeTransform(position, look, scale));
		TeleportIfSimulated(entity);
	}

	PhysicsWorld* GetPhysics(Entity entity)
	{
		return entity.GetScene()->GetPhysicsWorld();
	}

	SceneAudio* GetAudio(Entity entity)
	{
		return entity.GetScene()->GetAudio();
	}

	void ApplyBodySettings(Entity entity)
	{
		if (PhysicsWorld* physics = GetPhysics(entity))
			physics->ApplyBodySettings(entity);
	}

	namespace {

		struct EventListenerSlot
		{
			ScriptEventListenerID ID = 0;
			ScriptEventListener Listener;
		};

		std::vector<EventListenerSlot> s_EventListeners;
		ScriptEventListenerID s_NextEventListenerID = 1;
		int s_EventDepth = 0;
		constexpr int c_MaxEventDepth = 64;
		// Events sent while one outer event runs. Two languages that both answer an event by sending it
		// again double the work at every level, so a depth limit alone would still take forever.
		int s_EventsInDispatch = 0;
		constexpr int c_MaxEventsPerDispatch = 10000;

	}

	ScriptEventListenerID AddScriptEventListener(ScriptEventListener listener)
	{
		const ScriptEventListenerID id = s_NextEventListenerID++;
		s_EventListeners.push_back({ id, std::move(listener) });
		return id;
	}

	void RemoveScriptEventListener(ScriptEventListenerID id)
	{
		std::erase_if(s_EventListeners, [id](const EventListenerSlot& slot) { return slot.ID == id; });
	}

	namespace {

		// Counts one more event against the limits and holds one level of nesting while it lives.
		struct EventNesting
		{
			explicit EventNesting(const std::string& name)
			{
				if (s_EventDepth >= c_MaxEventDepth)
				{
					throw ScriptValueError("The event '" + name + "' was sent from inside " + std::to_string(c_MaxEventDepth) +
						" other events; scripts that answer each other's events need a way to stop");
				}
				if (s_EventDepth == 0)
					s_EventsInDispatch = 0;
				if (++s_EventsInDispatch > c_MaxEventsPerDispatch)
				{
					throw ScriptValueError("The event '" + name + "' is one of more than " + std::to_string(c_MaxEventsPerDispatch) +
						" events sent while handling a single event; scripts that answer each other's events need a way to stop");
				}
				s_EventDepth++;
			}
			~EventNesting() { s_EventDepth--; }
			EventNesting(const EventNesting&) = delete;
			EventNesting& operator=(const EventNesting&) = delete;
		};

		// Listeners may be added or removed while handling the event (a script that stops play mode),
		// so this delivers to the ones present when it starts and skips any that are gone by their turn.
		void DeliverToListeners(const std::string& name, const ScriptValueList& values, const ScriptEventDelivery& delivery,
			ScriptEventListenerID skip)
		{
			std::vector<ScriptEventListenerID> ids;
			for (const EventListenerSlot& slot : s_EventListeners)
			{
				if (slot.ID != skip)
					ids.push_back(slot.ID);
			}
			for (ScriptEventListenerID id : ids)
			{
				auto it = std::find_if(s_EventListeners.begin(), s_EventListeners.end(), [id](const EventListenerSlot& slot) { return slot.ID == id; });
				if (it == s_EventListeners.end())
					continue;
				// Copy it: the listener may remove itself while running.
				const ScriptEventListener listener = it->Listener;
				listener(name, values, delivery);
			}
		}

	}

	void EmitScriptEvent(const std::string& name, const ScriptValueList& values, ScriptEventListenerID skip)
	{
		EventNesting nesting(name);
		DeliverToListeners(name, values, ScriptEventDelivery(), skip);
	}

	void EmitScriptEventFrom(ScriptEntity source, const std::string& name, const ScriptValueList& values)
	{
		EventNesting nesting(name);
		ScriptEventDelivery delivery;
		delivery.Source = source;
		for (Entity entity = source.TryResolve(); entity; )
		{
			const ScriptEntity target(entity);
			delivery.Target = target;
			DeliverToListeners(name, values, delivery, 0);
			// A handler may have destroyed the entity, or the whole scene with it (stopping play).
			const Entity still = target.TryResolve();
			entity = still ? still.GetParent() : Entity();
		}
	}

	std::string RegisterRuntimeMaterial(const Ref<Material>& material)
	{
		const std::string path = "Runtime/Material_" + std::to_string(++s_RuntimeMaterialCounter);
		AssetManager::RegisterMaterial(path, material);
		return path;
	}

}
