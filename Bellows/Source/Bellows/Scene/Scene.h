#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/Timestep.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Bellows {

	class Entity;
	class PhysicsWorld;
	class SceneAudio;

	struct SceneSettings
	{
		glm::vec3 Gravity { 0.0f, -9.81f, 0.0f };
		float FixedTimestep = 1.0f / 60.0f;		// Physics step
		glm::vec3 AmbientColor { 0.03f, 0.035f, 0.04f };
		SceneRendererSettings Rendering;
	};

	// A collection of entities plus the systems that animate them during play.
	class Scene
	{
	public:
		explicit Scene(std::string name = "Untitled");
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Deep copy (entities keep their UUIDs). Used to enter play mode without touching the edited scene.
		static Ref<Scene> Copy(const Ref<Scene>& source);

		Entity CreateEntity(const std::string& name = "Entity");
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = "Entity");
		Entity CreateChildEntity(Entity parent, const std::string& name = "Entity");

		// In play mode destruction is deferred to the end of the frame; in edit mode it is immediate.
		// Children are destroyed with their parent.
		void DestroyEntity(Entity entity);
		bool IsPendingDestroy(Entity entity) const;

		// Copies an entity and its children with new UUIDs, as a sibling of the original.
		Entity DuplicateEntity(Entity entity);

		Entity FindEntityByName(std::string_view name);
		Entity TryGetEntityByUUID(UUID uuid);
		Entity GetEntityByRenderID(uint32_t renderID);
		std::vector<Entity> GetRootEntities();
		std::vector<Entity> GetAllEntities();
		size_t GetEntityCount() const { return m_EntityMap.size(); }

		// Re-parents 'child'. With keepWorldTransform the entity stays where it is in the world.
		// Returns false if the change would create a cycle.
		bool SetParent(Entity child, Entity parent, bool keepWorldTransform = true);
		void Unparent(Entity child, bool keepWorldTransform = true);
		// Moves a root entity (or a child within its parent) to a new index in its sibling list.
		void SetSiblingIndex(Entity entity, size_t index);

		glm::mat4 GetWorldTransform(Entity entity);
		void SetWorldTransform(Entity entity, const glm::mat4& worldTransform);
		glm::vec3 GetWorldPosition(Entity entity);

		// Play mode
		void OnRuntimeStart();
		void OnRuntimeStop();
		void OnUpdateRuntime(Timestep timestep);
		void OnUpdateEditor(Timestep timestep);
		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }
		void SetPaused(bool paused) { m_IsPaused = paused; }
		// While paused, advance the given number of frames.
		void Step(uint32_t frames = 1) { m_StepFrames += frames; }

		// Submits every renderable, light and the environment. Uses 'camera' as the view.
		// 'submitExtras' runs before the frame is finished (editor overlays, debug lines).
		void OnRender(SceneRenderer& renderer, const RenderCamera& camera, const std::function<void(SceneRenderer&)>& submitExtras = {});
		// Finds the primary camera entity and builds a RenderCamera for the given aspect ratio.
		bool GetPrimaryCamera(float aspectRatio, RenderCamera& outCamera);
		Entity GetPrimaryCameraEntity();

		void OnViewportResize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }
		SceneSettings& GetSettings() { return m_Settings; }
		const SceneSettings& GetSettings() const { return m_Settings; }

		PhysicsWorld* GetPhysicsWorld() const { return m_PhysicsWorld.get(); }
		SceneAudio* GetAudio() const { return m_Audio.get(); }
		float GetTime() const { return m_Time; }
		uint64_t GetFrameCount() const { return m_FrameCount; }

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

		entt::registry& GetRegistry() { return m_Registry; }

		// Unique for the lifetime of the process; lets external handles (Python) detect deleted scenes.
		uint64_t GetID() const { return m_SceneID; }
		static Scene* GetSceneByID(uint64_t id);

		// The scene currently running scripts (play mode). Null when nothing is playing.
		static Scene* GetActiveScene() { return s_ActiveScene; }
	private:
		void DestroyEntityImmediate(Entity entity);
		void FlushPendingDestroys();
		void RemoveFromParentList(Entity entity);
		void UpdateRuntimeCaches();
	private:
		uint64_t m_SceneID = 0;
		std::string m_Name;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::vector<UUID> m_RootEntities;
		std::vector<UUID> m_PendingDestroy;
		SceneSettings m_Settings;

		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;

		bool m_IsRunning = false;
		bool m_IsPaused = false;
		uint32_t m_StepFrames = 0;
		float m_Time = 0.0f;
		uint64_t m_FrameCount = 0;
		float m_PhysicsAccumulator = 0.0f;

		Scope<PhysicsWorld> m_PhysicsWorld;
		Scope<SceneAudio> m_Audio;

		static Scene* s_ActiveScene;

		friend class Entity;
		friend class SceneSerializer;
	};

}
