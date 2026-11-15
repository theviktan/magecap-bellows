#include "bwpch.h"
#include "Bellows/Scripting/ScriptApi.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cctype>

namespace Bellows::Scripting {

	namespace {

		Scene* s_ContextScene = nullptr;
		std::function<void(int)> s_QuitHandler;
		std::function<bool(const std::string&)> s_LoadSceneHandler;
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

		// Lower case without spaces or underscores, so "Left Shift", "left_shift" and "LeftShift" match.
		std::string NormalizeName(const std::string& name)
		{
			std::string normalized;
			for (char c : name)
			{
				if (c != '_' && c != ' ')
					normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return normalized;
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

	void SetContextScene(Scene* scene) { s_ContextScene = scene; }

	Scene* GetContextScene()
	{
		if (s_ContextScene && Scene::GetSceneByID(s_ContextScene->GetID()) == s_ContextScene)
			return s_ContextScene;
		return nullptr;
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

	void RequestQuit(int exitCode)
	{
		if (s_QuitHandler)
			s_QuitHandler(exitCode);
	}

	bool RequestLoadScene(const std::string& path)
	{
		return s_LoadSceneHandler ? s_LoadSceneHandler(path) : false;
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
		static const std::vector<KeyName> s_Keys = {
			{ "Space", KeyCode::Space }, { "Apostrophe", KeyCode::Apostrophe }, { "Comma", KeyCode::Comma },
			{ "Minus", KeyCode::Minus }, { "Period", KeyCode::Period }, { "Slash", KeyCode::Slash },
			{ "D0", KeyCode::D0 }, { "D1", KeyCode::D1 }, { "D2", KeyCode::D2 }, { "D3", KeyCode::D3 }, { "D4", KeyCode::D4 },
			{ "D5", KeyCode::D5 }, { "D6", KeyCode::D6 }, { "D7", KeyCode::D7 }, { "D8", KeyCode::D8 }, { "D9", KeyCode::D9 },
			{ "Semicolon", KeyCode::Semicolon }, { "Equal", KeyCode::Equal },
			{ "A", KeyCode::A }, { "B", KeyCode::B }, { "C", KeyCode::C }, { "D", KeyCode::D }, { "E", KeyCode::E },
			{ "F", KeyCode::F }, { "G", KeyCode::G }, { "H", KeyCode::H }, { "I", KeyCode::I }, { "J", KeyCode::J },
			{ "K", KeyCode::K }, { "L", KeyCode::L }, { "M", KeyCode::M }, { "N", KeyCode::N }, { "O", KeyCode::O },
			{ "P", KeyCode::P }, { "Q", KeyCode::Q }, { "R", KeyCode::R }, { "S", KeyCode::S }, { "T", KeyCode::T },
			{ "U", KeyCode::U }, { "V", KeyCode::V }, { "W", KeyCode::W }, { "X", KeyCode::X }, { "Y", KeyCode::Y },
			{ "Z", KeyCode::Z },
			{ "LeftBracket", KeyCode::LeftBracket }, { "Backslash", KeyCode::Backslash }, { "RightBracket", KeyCode::RightBracket },
			{ "GraveAccent", KeyCode::GraveAccent },
			{ "Escape", KeyCode::Escape }, { "Enter", KeyCode::Enter }, { "Tab", KeyCode::Tab }, { "Backspace", KeyCode::Backspace },
			{ "Insert", KeyCode::Insert }, { "Delete", KeyCode::Delete },
			{ "Right", KeyCode::Right }, { "Left", KeyCode::Left }, { "Down", KeyCode::Down }, { "Up", KeyCode::Up },
			{ "PageUp", KeyCode::PageUp }, { "PageDown", KeyCode::PageDown }, { "Home", KeyCode::Home }, { "End", KeyCode::End },
			{ "CapsLock", KeyCode::CapsLock }, { "Pause", KeyCode::Pause },
			{ "F1", KeyCode::F1 }, { "F2", KeyCode::F2 }, { "F3", KeyCode::F3 }, { "F4", KeyCode::F4 }, { "F5", KeyCode::F5 },
			{ "F6", KeyCode::F6 }, { "F7", KeyCode::F7 }, { "F8", KeyCode::F8 }, { "F9", KeyCode::F9 }, { "F10", KeyCode::F10 },
			{ "F11", KeyCode::F11 }, { "F12", KeyCode::F12 },
			{ "KP0", KeyCode::KP0 }, { "KP1", KeyCode::KP1 }, { "KP2", KeyCode::KP2 }, { "KP3", KeyCode::KP3 }, { "KP4", KeyCode::KP4 },
			{ "KP5", KeyCode::KP5 }, { "KP6", KeyCode::KP6 }, { "KP7", KeyCode::KP7 }, { "KP8", KeyCode::KP8 }, { "KP9", KeyCode::KP9 },
			{ "KPEnter", KeyCode::KPEnter }, { "KPAdd", KeyCode::KPAdd }, { "KPSubtract", KeyCode::KPSubtract },
			{ "LeftShift", KeyCode::LeftShift }, { "LeftControl", KeyCode::LeftControl }, { "LeftAlt", KeyCode::LeftAlt },
			{ "RightShift", KeyCode::RightShift }, { "RightControl", KeyCode::RightControl }, { "RightAlt", KeyCode::RightAlt },
		};
		return s_Keys;
	}

	KeyCode KeyFromName(const std::string& name)
	{
		const std::string normalized = NormalizeName(name);
		for (const KeyName& key : GetKeyNames())
		{
			if (NormalizeName(key.Name) == normalized)
				return key.Code;
		}
		// Digits written plainly ("1") map to D1.
		if (normalized.size() == 1 && std::isdigit(static_cast<unsigned char>(normalized[0])))
			return static_cast<KeyCode>(static_cast<int>(KeyCode::D0) + (normalized[0] - '0'));
		throw ScriptValueError("Unknown key '" + name + "'");
	}

	MouseButton MouseButtonFromName(const std::string& name)
	{
		const std::string normalized = NormalizeName(name);
		if (normalized == "left") return MouseButton::Left;
		if (normalized == "right") return MouseButton::Right;
		if (normalized == "middle") return MouseButton::Middle;
		throw ScriptValueError("Expected a mouse button: 'left', 'right' or 'middle'");
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

	std::string RegisterRuntimeMaterial(const Ref<Material>& material)
	{
		const std::string path = "Runtime/Material_" + std::to_string(++s_RuntimeMaterialCounter);
		AssetManager::RegisterMaterial(path, material);
		return path;
	}

}
