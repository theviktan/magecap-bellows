#include "bwpch.h"
#include "Bellows/Scene/Scene.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Profiler.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Navigation/NavAgents.h"
#include "Bellows/Navigation/SceneNavigation.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/ParticleSystem.h"
#include "Bellows/Scene/BeamSystem.h"
#include "Bellows/Scene/EffectShapeSystem.h"
#include "Bellows/Scene/TrailSystem.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scene/Tweens.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	Scene* Scene::s_ActiveScene = nullptr;

	namespace {

		std::unordered_map<uint64_t, Scene*>& GetSceneRegistry()
		{
			static std::unordered_map<uint64_t, Scene*> s_Scenes;
			return s_Scenes;
		}

		uint64_t s_NextSceneID = 1;

		template<typename... Component>
		void CopyComponents(ComponentGroup<Component...>, entt::registry& destination, const entt::registry& source,
			const std::unordered_map<UUID, entt::entity>& destinationMap)
		{
			([&]()
			{
				auto view = source.view<Component>();
				for (entt::entity sourceEntity : view)
				{
					const UUID uuid = source.get<IDComponent>(sourceEntity).ID;
					auto it = destinationMap.find(uuid);
					if (it == destinationMap.end())
						continue;
					destination.emplace_or_replace<Component>(it->second, source.get<Component>(sourceEntity));
				}
			}(), ...);
		}

		template<typename... Component>
		void CopyEntityComponents(ComponentGroup<Component...>, Entity destination, Entity source)
		{
			([&]()
			{
				if constexpr (!std::is_same_v<Component, RelationshipComponent>)
				{
					if (source.HasComponent<Component>())
						destination.AddOrReplaceComponent<Component>(source.GetComponent<Component>());
				}
			}(), ...);
		}

	}

	// -------------------------------------------------------------------------
	// Entity helpers (defined here because they need the full Scene)
	// -------------------------------------------------------------------------

	Entity Entity::GetParent() const
	{
		const UUID parent = GetComponent<RelationshipComponent>().Parent;
		return parent.IsValid() ? m_Scene->TryGetEntityByUUID(parent) : Entity();
	}

	std::vector<Entity> Entity::GetChildren() const
	{
		std::vector<Entity> children;
		for (UUID child : GetComponent<RelationshipComponent>().Children)
		{
			if (Entity entity = m_Scene->TryGetEntityByUUID(child))
				children.push_back(entity);
		}
		return children;
	}

	bool Entity::IsDescendantOf(Entity ancestor) const
	{
		Entity current = GetParent();
		while (current)
		{
			if (current == ancestor)
				return true;
			current = current.GetParent();
		}
		return false;
	}

	// -------------------------------------------------------------------------
	// Scene
	// -------------------------------------------------------------------------

	Scene::Scene(std::string name)
		: m_SceneID(s_NextSceneID++), m_Name(std::move(name))
	{
		GetSceneRegistry()[m_SceneID] = this;
	}

	Scene::~Scene()
	{
		if (m_IsRunning)
			OnRuntimeStop();
		if (s_ActiveScene == this)
			s_ActiveScene = nullptr;
		GetSceneRegistry().erase(m_SceneID);
	}

	Scene* Scene::GetSceneByID(uint64_t id)
	{
		auto it = GetSceneRegistry().find(id);
		return it != GetSceneRegistry().end() ? it->second : nullptr;
	}

	Ref<Scene> Scene::Copy(const Ref<Scene>& source)
	{
		Ref<Scene> scene = CreateRef<Scene>(source->m_Name);
		scene->m_Settings = source->m_Settings;
		scene->m_Groups = source->m_Groups;
		scene->m_NavMesh = source->m_NavMesh;
		scene->m_ViewportWidth = source->m_ViewportWidth;
		scene->m_ViewportHeight = source->m_ViewportHeight;

		auto view = source->m_Registry.view<IDComponent>();
		for (entt::entity entity : view)
		{
			const UUID uuid = source->m_Registry.get<IDComponent>(entity).ID;
			const TagComponent& tag = source->m_Registry.get<TagComponent>(entity);
			scene->CreateEntityWithUUID(uuid, tag.Tag).GetComponent<TagComponent>() = tag;
		}
		CopyComponents(AllComponents {}, scene->m_Registry, source->m_Registry, scene->m_EntityMap);
		scene->m_RootEntities = source->m_RootEntities;
		return scene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		BW_CORE_ASSERT(!m_EntityMap.contains(uuid), "Duplicate entity UUID {}", static_cast<uint64_t>(uuid));

		Entity entity(m_Registry.create(), this);
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TagComponent>(name.empty() ? std::string("Entity") : name);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		m_EntityMap[uuid] = entity;
		m_RootEntities.push_back(uuid);
		return entity;
	}

	Entity Scene::CreateChildEntity(Entity parent, const std::string& name)
	{
		Entity entity = CreateEntity(name);
		if (parent)
			SetParent(entity, parent, false);
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity || entity.GetScene() != this)
			return;

		if (m_IsRunning)
		{
			const UUID uuid = entity.GetUUID();
			if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), uuid) == m_PendingDestroy.end())
				m_PendingDestroy.push_back(uuid);
			for (Entity child : entity.GetChildren())
				DestroyEntity(child);
			return;
		}
		DestroyEntityImmediate(entity);
	}

	bool Scene::IsPendingDestroy(Entity entity) const
	{
		return entity && std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), entity.GetUUID()) != m_PendingDestroy.end();
	}

	void Scene::DestroyEntityImmediate(Entity entity)
	{
		if (!entity)
			return;

		for (Entity child : entity.GetChildren())
			DestroyEntityImmediate(child);

		if (m_IsRunning)
		{
			ScriptEngine::OnEntityDestroyed(entity);
			if (m_PhysicsWorld)
				m_PhysicsWorld->DestroyBody(entity);
			if (m_Audio)
				m_Audio->OnEntityDestroyed(entity);
		}

		RemoveFromParentList(entity);
		const UUID uuid = entity.GetUUID();
		m_EntityMap.erase(uuid);
		m_Registry.destroy(entity);
	}

	void Scene::FlushPendingDestroys()
	{
		// Destroy callbacks may queue more entities; keep going until the queue is empty.
		for (int iteration = 0; iteration < 16 && !m_PendingDestroy.empty(); iteration++)
		{
			std::vector<UUID> pending;
			pending.swap(m_PendingDestroy);
			for (UUID uuid : pending)
			{
				if (Entity entity = TryGetEntityByUUID(uuid))
					DestroyEntityImmediate(entity);
			}
		}
		m_PendingDestroy.clear();
	}

	void Scene::RemoveFromParentList(Entity entity)
	{
		const UUID uuid = entity.GetUUID();
		RelationshipComponent& relationship = entity.GetComponent<RelationshipComponent>();
		if (relationship.Parent.IsValid())
		{
			if (Entity parent = TryGetEntityByUUID(relationship.Parent))
				std::erase(parent.GetComponent<RelationshipComponent>().Children, uuid);
		}
		else
		{
			std::erase(m_RootEntities, uuid);
		}
		relationship.Parent = 0;
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		if (!entity)
			return {};

		std::unordered_map<UUID, UUID> copies;		// Original to copy, for references inside the tree
		std::vector<Entity> created;
		std::function<Entity(Entity, Entity)> duplicate = [&](Entity source, Entity parent) -> Entity
		{
			Entity copy = CreateEntity(source.GetName());
			copy.GetComponent<TagComponent>() = source.GetComponent<TagComponent>();
			CopyEntityComponents(AllComponents {}, copy, source);
			copies[source.GetUUID()] = copy.GetUUID();
			created.push_back(copy);
			if (parent)
				SetParent(copy, parent, false);
			for (Entity child : source.GetChildren())
				duplicate(child, copy);
			return copy;
		};

		Entity copy = duplicate(entity, entity.GetParent());
		// A beam aimed at an entity inside the copied tree aims at that entity's copy.
		for (Entity made : created)
		{
			if (BeamComponent* beam = made.TryGetComponent<BeamComponent>())
			{
				if (auto it = copies.find(beam->Target); it != copies.end())
					beam->Target = it->second;
			}
		}

		// Place the copy right after the original among its siblings.
		Entity parent = entity.GetParent();
		const std::vector<UUID>& siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootEntities;
		auto it = std::find(siblings.begin(), siblings.end(), entity.GetUUID());
		if (it != siblings.end())
			SetSiblingIndex(copy, static_cast<size_t>(std::distance(siblings.begin(), it)) + 1);
		return copy;
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		auto view = m_Registry.view<TagComponent>();
		for (entt::entity entity : view)
		{
			if (view.get<TagComponent>(entity).Tag == name && !IsPendingDestroy(Entity(entity, this)))
				return Entity(entity, this);
		}
		return {};
	}

	Entity Scene::TryGetEntityByUUID(UUID uuid)
	{
		auto it = m_EntityMap.find(uuid);
		return it != m_EntityMap.end() ? Entity(it->second, this) : Entity();
	}

	Entity Scene::GetEntityByRenderID(uint32_t renderID)
	{
		if (renderID == 0)
			return {};
		const auto handle = static_cast<entt::entity>(renderID - 1);
		return m_Registry.valid(handle) ? Entity(handle, this) : Entity();
	}

	std::vector<Entity> Scene::GetRootEntities()
	{
		std::vector<Entity> roots;
		roots.reserve(m_RootEntities.size());
		for (UUID uuid : m_RootEntities)
		{
			if (Entity entity = TryGetEntityByUUID(uuid))
				roots.push_back(entity);
		}
		return roots;
	}

	std::vector<Entity> Scene::GetAllEntities()
	{
		std::vector<Entity> entities;
		std::function<void(Entity)> visit = [&](Entity entity)
		{
			entities.push_back(entity);
			for (Entity child : entity.GetChildren())
				visit(child);
		};
		for (Entity root : GetRootEntities())
			visit(root);
		return entities;
	}

	bool Scene::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		if (!child || child == parent)
			return false;
		if (parent && parent.IsDescendantOf(child))
		{
			BW_CORE_WARN("Cannot parent '{}' to its own descendant '{}'", child.GetName(), parent.GetName());
			return false;
		}

		const glm::mat4 worldTransform = GetWorldTransform(child);
		// A child follows its top-level ancestor's group; one that becomes top-level stays in the group
		// it was in, so it doesn't jump to another place in the Hierarchy.
		const UUID group = GetEntityGroup(child);
		RemoveFromParentList(child);

		RelationshipComponent& relationship = child.GetComponent<RelationshipComponent>();
		if (parent)
		{
			relationship.Parent = parent.GetUUID();
			parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());
		}
		else
		{
			m_RootEntities.push_back(child.GetUUID());
		}
		child.GetComponent<TagComponent>().Group = parent ? UUID(0) : group;

		if (keepWorldTransform)
			SetWorldTransform(child, worldTransform);
		return true;
	}

	void Scene::Unparent(Entity child, bool keepWorldTransform)
	{
		SetParent(child, Entity(), keepWorldTransform);
	}

	void Scene::SetSiblingIndex(Entity entity, size_t index)
	{
		if (!entity)
			return;
		Entity parent = entity.GetParent();
		std::vector<UUID>& siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : m_RootEntities;
		const UUID uuid = entity.GetUUID();
		auto it = std::find(siblings.begin(), siblings.end(), uuid);
		if (it == siblings.end())
			return;
		siblings.erase(it);
		index = std::min(index, siblings.size());
		siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), uuid);
	}

	SceneGroup* Scene::FindGroup(UUID id)
	{
		if (!id.IsValid())
			return nullptr;
		auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [&](const SceneGroup& group) { return group.ID == id; });
		return it != m_Groups.end() ? &*it : nullptr;
	}

	SceneGroup* Scene::FindGroup(std::string_view name)
	{
		const std::string trimmed = SceneGroups::Trim(name);
		auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [&](const SceneGroup& group) { return SceneGroups::NamesMatch(group.Name, trimmed); });
		return it != m_Groups.end() ? &*it : nullptr;
	}

	SceneGroup& Scene::CreateGroup(std::string_view name)
	{
		const std::string trimmed = SceneGroups::Trim(name);
		if (const std::optional<std::string> problem = SceneGroups::CheckName(trimmed))
			throw std::invalid_argument(*problem);
		if (const SceneGroup* existing = FindGroup(trimmed))
			throw std::invalid_argument("The scene already has a group named '" + existing->Name + "'");
		if (m_Groups.size() >= SceneGroups::MaxGroups)
			throw std::invalid_argument("A scene can have at most " + std::to_string(SceneGroups::MaxGroups) + " groups");
		SceneGroup& group = m_Groups.emplace_back();
		group.Name = trimmed;
		return group;
	}

	void Scene::RenameGroup(UUID id, std::string_view name)
	{
		SceneGroup* group = FindGroup(id);
		if (!group)
			throw std::invalid_argument("The group no longer exists");
		const std::string trimmed = SceneGroups::Trim(name);
		if (const std::optional<std::string> problem = SceneGroups::CheckName(trimmed))
			throw std::invalid_argument(*problem);
		if (const SceneGroup* existing = FindGroup(trimmed); existing && existing != group)
			throw std::invalid_argument("The scene already has a group named '" + existing->Name + "'");
		group->Name = trimmed;
	}

	bool Scene::DeleteGroup(UUID id)
	{
		auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [&](const SceneGroup& group) { return group.ID == id; });
		if (it == m_Groups.end())
			return false;
		m_Groups.erase(it);
		for (entt::entity handle : m_Registry.view<TagComponent>())
		{
			TagComponent& tag = m_Registry.get<TagComponent>(handle);
			if (tag.Group == id)
				tag.Group = 0;
		}
		return true;
	}

	void Scene::MoveGroup(UUID id, size_t index)
	{
		auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [&](const SceneGroup& group) { return group.ID == id; });
		if (it == m_Groups.end())
			return;
		SceneGroup group = std::move(*it);
		m_Groups.erase(it);
		index = std::min(index, m_Groups.size());
		m_Groups.insert(m_Groups.begin() + static_cast<std::ptrdiff_t>(index), std::move(group));
	}

	UUID Scene::GetEntityGroup(Entity entity)
	{
		if (!entity)
			return 0;
		Entity root = entity;
		while (Entity parent = root.GetParent())
			root = parent;
		const UUID group = root.GetComponent<TagComponent>().Group;
		return FindGroup(group) ? group : UUID(0);
	}

	bool Scene::SetEntityGroup(Entity entity, UUID group)
	{
		if (!entity || entity.GetParent() || (group.IsValid() && !FindGroup(group)))
			return false;
		entity.GetComponent<TagComponent>().Group = group;
		return true;
	}

	std::vector<Entity> Scene::GetGroupEntities(UUID group, bool withDescendants)
	{
		std::vector<Entity> entities;
		if (!FindGroup(group))
			return entities;
		std::function<void(Entity)> visit = [&](Entity entity)
		{
			if (IsPendingDestroy(entity))
				return;
			entities.push_back(entity);
			if (withDescendants)
			{
				for (Entity child : entity.GetChildren())
				{
					if (child)
						visit(child);
				}
			}
		};
		for (Entity root : GetRootEntities())
		{
			if (root.GetComponent<TagComponent>().Group == group)
				visit(root);
		}
		return entities;
	}

	void Scene::SetGroups(std::vector<SceneGroup> groups)
	{
		m_Groups = std::move(groups);
		for (entt::entity handle : m_Registry.view<TagComponent>())
		{
			TagComponent& tag = m_Registry.get<TagComponent>(handle);
			if (tag.Group.IsValid() && !FindGroup(tag.Group))
				tag.Group = 0;
		}
	}

	bool Scene::IsHiddenInEditor(Entity entity)
	{
		const SceneGroup* group = FindGroup(GetEntityGroup(entity));
		return group && group->HiddenInEditor;
	}

	bool Scene::IsLockedInEditor(Entity entity)
	{
		const SceneGroup* group = FindGroup(GetEntityGroup(entity));
		return group && group->LockedInEditor;
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		glm::mat4 transform = entity.GetComponent<TransformComponent>().GetTransform();
		Entity parent = entity.GetParent();
		while (parent)
		{
			transform = parent.GetComponent<TransformComponent>().GetTransform() * transform;
			parent = parent.GetParent();
		}
		return transform;
	}

	void Scene::SetWorldTransform(Entity entity, const glm::mat4& worldTransform)
	{
		Entity parent = entity.GetParent();
		const glm::mat4 local = parent ? glm::inverse(GetWorldTransform(parent)) * worldTransform : worldTransform;
		entity.GetComponent<TransformComponent>().SetTransform(local);
	}

	glm::vec3 Scene::GetWorldPosition(Entity entity)
	{
		return glm::vec3(GetWorldTransform(entity)[3]);
	}

	void Scene::OnRuntimeStart()
	{
		if (m_IsRunning)
			return;

		m_IsRunning = true;
		m_IsPaused = false;
		m_GamePaused = false;
		m_Time = 0.0f;
		m_FrameCount = 0;
		m_PhysicsAccumulator = 0.0f;
		s_ActiveScene = this;
		ResetUIInput();

		UpdateRuntimeCaches();
		// The edited scene's copy may carry the editor's preview: every Animator starts afresh.
		for (entt::entity handle : m_Registry.view<AnimatorComponent>())
		{
			AnimatorComponent& animator = m_Registry.get<AnimatorComponent>(handle);
			animator.RuntimePlayer.Reset();
			animator.RuntimeMesh = nullptr;
			animator.RuntimeSkinMatrices.clear();
			animator.RuntimePreview = false;
		}
		UpdateAnimators(0.0f);
		// Likewise every emitter: the editor's preview particles don't carry into play.
		for (entt::entity handle : m_Registry.view<ParticleEmitterComponent>())
			ParticleSystem::Reset(m_Registry.get<ParticleEmitterComponent>(handle), Entity(handle, this).GetUUID());
		for (entt::entity handle : m_Registry.view<TrailComponent>())
			TrailSystem::Clear(m_Registry.get<TrailComponent>(handle));
		for (entt::entity handle : m_Registry.view<EffectShapeComponent>())
		{
			// Every shape starts its cycle with play; one stopped by a script in the editor shows again.
			EffectShapeComponent& shape = m_Registry.get<EffectShapeComponent>(handle);
			shape.RuntimeTime = 0.0f;
			shape.RuntimeSpin = 0.0f;
			shape.RuntimeOffset = glm::vec2(0.0f);
			shape.RuntimeDistortionPhase = 0.0f;
			shape.RuntimeFinished = false;
		}
		for (entt::entity handle : m_Registry.view<BeamComponent>())
		{
			BeamComponent& beam = m_Registry.get<BeamComponent>(handle);
			beam.RuntimeValid = false;
			beam.RuntimeHit = false;
			beam.RuntimeHitEntity = 0;
			beam.RuntimeTime = 0.0f;
		}
		for (entt::entity handle : m_Registry.view<NavAgentComponent>())
		{
			NavAgentComponent& agent = m_Registry.get<NavAgentComponent>(handle);
			agent.RuntimeDestination.reset();
			agent.RuntimeDestinationChanged = false;
			agent.RuntimeVelocity = glm::vec3(0.0f);
		}

		m_PhysicsWorld = CreateScope<PhysicsWorld>(*this);
		m_NavAgents = CreateScope<SceneNavAgents>();
		m_PhysicsWorld->SyncBodies();
		m_Tweens = CreateScope<SceneTweens>();
		m_CameraShake.Clear();

		if (AudioEngine::IsInitialized())
			m_Audio = CreateScope<SceneAudio>(*this);

		if (ScriptEngine::IsInitialized())
			ScriptEngine::OnRuntimeStart(*this);

		if (m_Audio)
			m_Audio->OnRuntimeStart();
	}

	void Scene::OnRuntimeStop()
	{
		if (!m_IsRunning)
			return;

		if (ScriptEngine::IsInitialized())
			ScriptEngine::OnRuntimeStop(*this);

		m_PendingDestroy.clear();
		Scripting::ClearQueuedScriptEvents();
		// After the scripts stopped, so a tween started in on_destroy goes too.
		m_Tweens.reset();
		m_CameraShake.Clear();
		m_Audio.reset();
		m_NavAgents.reset();
		m_PhysicsWorld.reset();
		m_IsRunning = false;
		ResetUIInput();
		if (s_ActiveScene == this)
			s_ActiveScene = nullptr;
	}

	bool Scene::ChangeScene(const Json& json, const std::string& path)
	{
		if (!m_IsRunning || !json.is_object() || !json.contains("Entities") || !json["Entities"].is_array())
			return false;

		FlushPendingDestroys();

		// The kept entities: those marked, with everything under them. A marked entity under one that
		// goes moves to the top first, where it stays put in the world.
		std::unordered_set<UUID> kept;
		std::vector<Entity> marked;
		for (entt::entity handle : m_Registry.view<TagComponent>())
		{
			if (m_Registry.get<TagComponent>(handle).KeepAcrossScenes)
				marked.emplace_back(handle, this);
		}
		std::function<void(Entity)> keep = [&](Entity entity)
		{
			kept.insert(entity.GetUUID());
			for (Entity child : entity.GetChildren())
				keep(child);
		};
		for (Entity entity : marked)
			keep(entity);
		for (Entity entity : marked)
		{
			const UUID parent = entity.GetComponent<RelationshipComponent>().Parent;
			if (parent.IsValid() && !kept.contains(parent))
				Unparent(entity, true);
		}

		// on_destroy handlers may create entities; those go too, so only the kept ones reach the new scene.
		for (int pass = 0;; pass++)
		{
			std::vector<Entity> roots;
			for (entt::entity handle : m_Registry.view<RelationshipComponent>())
			{
				Entity entity(handle, this);
				if (!entity.GetComponent<RelationshipComponent>().Parent.IsValid() && !kept.contains(entity.GetUUID()))
					roots.push_back(entity);
			}
			if (roots.empty())
				break;
			if (pass == 16)
			{
				BW_CORE_WARN("load_scene: on_destroy handlers keep creating entities; {} of them come along to the new scene", roots.size());
				break;
			}
			for (Entity entity : roots)
			{
				if (m_Registry.valid(entity))
					DestroyEntityImmediate(entity);
			}
			FlushPendingDestroys();
		}

		// The new scene's settings and name, as a new scene would have them; a file may leave either out.
		m_Settings = SceneSettings {};
		m_Name = "Untitled";
		SceneSerializer::DeserializeFromJson(*this, json, true);
		m_NavMesh = LoadSceneNavMesh(AssetManager::GetAssetDirectory() / path);

		std::vector<Entity> loaded;
		for (entt::entity handle : m_Registry.view<IDComponent>())
		{
			if (!kept.contains(m_Registry.get<IDComponent>(handle).ID))
				loaded.emplace_back(handle, this);
		}

		m_GamePaused = false;
		ResetUIInput();
		UpdateRuntimeCaches();
		if (m_PhysicsWorld)
			m_PhysicsWorld->SyncBodies();
		if (ScriptEngine::IsInitialized())
			ScriptEngine::InstantiatePendingScripts(*this);
		if (m_Audio)
			m_Audio->OnEntitiesLoaded(loaded);
		if (ScriptEngine::IsInitialized())
			Scripting::EmitSceneLoaded(m_Name, path);
		return true;
	}

	void Scene::OnUpdateRuntime(Timestep timestep)
	{
		if (!m_IsRunning)
			return;

		BW_PROFILE_SCOPE("Scene Update");
		UpdateRuntimeCaches();

		if (m_IsPaused)
		{
			if (m_StepFrames == 0)
				return;
			m_StepFrames--;
			timestep = Timestep(m_Settings.FixedTimestep);
		}

		s_ActiveScene = this;
		const float deltaTime = timestep.GetSeconds();

		// The UI goes first, so scripts see this frame's UI events and only the input it left the game.
		{
			BW_PROFILE_SCOPE("Game UI");
			UpdateUIInput();
		}

		// A camera shake and animations run in game time, so a paused game holds them.
		if (!m_GamePaused)
		{
			BW_PROFILE_SCOPE("Animation");
			m_CameraShake.Update(deltaTime);
			UpdateAnimators(deltaTime);
		}

		// Tweens next, so scripts see this frame's values; while the game is paused only those marked
		// to run while paused move.
		{
			BW_PROFILE_SCOPE("Tweens");
			for (const FinishedTween& finished : m_Tweens->Update(*this, deltaTime, m_GamePaused))
			{
				Scripting::ScriptEntity entity;
				entity.SceneID = m_SceneID;
				entity.ID = finished.Entity;
				Scripting::EmitScriptEvent("tween finished", { entity, finished.Property, static_cast<double>(finished.ID) });
			}
		}

		{
			BW_PROFILE_SCOPE("Scripts");
			if (ScriptEngine::IsInitialized())
			{
				ScriptEngine::InstantiatePendingScripts(*this);
				ScriptEngine::OnUpdate(*this, deltaTime);
			}
			// Events queued while scripts ran ('field changed'), now that none is in the middle of a line.
			Scripting::FlushQueuedScriptEvents();
		}

		// Agents walk after scripts, so a destination set this frame is used, and before physics, so
		// a kinematic body goes where its agent went.
		if (!m_GamePaused && m_NavAgents)
		{
			BW_PROFILE_SCOPE("Navigation");
			for (const NavAgentArrival& arrival : m_NavAgents->Update(*this, deltaTime))
			{
				Scripting::ScriptEntity entity;
				entity.SceneID = m_SceneID;
				entity.ID = arrival.Entity;
				Scripting::EmitScriptEventAbout(entity, "destination reached", { entity, arrival.Reached });
			}
		}

		// Fixed-step physics. At most a few steps per frame so a slow frame cannot spiral. The game's
		// pause holds it (and fixed updates) where it is.
		const float fixedTimestep = std::clamp(m_Settings.FixedTimestep, 1.0f / 480.0f, 1.0f / 15.0f);
		if (!m_GamePaused)
			m_PhysicsAccumulator = std::min(m_PhysicsAccumulator + deltaTime, fixedTimestep * 5.0f);
		while (!m_GamePaused && m_PhysicsAccumulator >= fixedTimestep)
		{
			BW_PROFILE_SCOPE("Physics");
			m_PhysicsWorld->SetGravity(m_Settings.Gravity);
			m_PhysicsWorld->SyncBodies();
			if (ScriptEngine::IsInitialized())
			{
				BW_PROFILE_SCOPE("Fixed Update Scripts");
				ScriptEngine::OnFixedUpdate(*this, fixedTimestep);
			}
			{
				BW_PROFILE_SCOPE("Physics Step");
				m_PhysicsWorld->Step(fixedTimestep);
			}
			if (ScriptEngine::IsInitialized())
			{
				BW_PROFILE_SCOPE("Contact Scripts");
				for (const ContactEvent& event : m_PhysicsWorld->GetContactEvents())
					ScriptEngine::OnContactEvent(*this, event);
			}
			Scripting::FlushQueuedScriptEvents();
			m_PhysicsAccumulator -= fixedTimestep;
		}

		// Particles and trails after physics, so they leave from where their entity ended up this frame.
		if (!m_GamePaused)
		{
			UpdateParticles(deltaTime);
			UpdateTrails(deltaTime);
			UpdateBeams(deltaTime);
			UpdateEffectShapes(deltaTime);
		}

		if (m_Audio)
		{
			BW_PROFILE_SCOPE("Audio");
			m_Audio->OnUpdate();
		}

		Scripting::FlushQueuedScriptEvents();
		FlushPendingDestroys();

		// time() is game time, so it stops while the game is paused; real_time() keeps going.
		if (!m_GamePaused)
			m_Time += deltaTime;
		m_FrameCount++;
	}

	namespace {

		// Loads (or reloads) the text's font; false when no font can be loaded.
		bool EnsureTextFont(TextComponent& text)
		{
			if (!text.RuntimeFont || text.LoadedFontPath != text.FontPath)
			{
				text.RuntimeFont = AssetManager::GetFont(text.FontPath);
				text.LoadedFontPath = text.FontPath;
				text.RuntimeLayoutKey = 0;
			}
			return text.RuntimeFont != nullptr;
		}

		TextLayoutSettings MakeTextLayoutSettings(const TextComponent& text, float size, const glm::vec2& boxSize)
		{
			TextLayoutSettings settings;
			settings.Size = std::max(size, 0.0f);
			settings.LineSpacing = text.LineSpacing;
			settings.BoxSize = glm::max(boxSize, glm::vec2(0.0f));
			settings.Wrap = text.Wrap;
			settings.Alignment = text.Alignment;
			settings.VerticalAlignment = text.VerticalAlignment;
			settings.Overflow = text.Overflow;
			return settings;
		}

	}

	const TextLayout* Scene::UpdateTextLayout(TextComponent& text)
	{
		return LayoutTextComponent(text, text.Size, text.BoxSize);
	}

	glm::vec2 Scene::MeasureTextSize(TextComponent& text)
	{
		if (!EnsureTextFont(text))
			return glm::vec2(0.0f);
		return MeasureText(*text.RuntimeFont, text.Text, MakeTextLayoutSettings(text, text.Size, glm::vec2(text.BoxSize.x, 0.0f)));
	}

	const TextLayout* Scene::LayoutTextComponent(TextComponent& text, float size, const glm::vec2& boxSize)
	{
		if (!EnsureTextFont(text))
			return nullptr;

		// Lay the text out again only when something that shapes it changed.
		uint64_t key = std::hash<std::string>()(text.Text);
		const auto combine = [&key](uint64_t value) { key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2); };
		combine(std::hash<const void*>()(text.RuntimeFont.get()));
		for (float value : { size, boxSize.x, boxSize.y, text.LineSpacing })
			combine(std::hash<float>()(value));
		combine((static_cast<uint64_t>(text.Alignment) << 24) | (static_cast<uint64_t>(text.VerticalAlignment) << 16)
			| (static_cast<uint64_t>(text.Overflow) << 8) | (text.Wrap ? 1u : 0u));
		key |= 1;	// 0 means "not laid out yet"
		if (key != text.RuntimeLayoutKey)
		{
			text.RuntimeLayout = LayoutText(*text.RuntimeFont, text.Text, MakeTextLayoutSettings(text, size, boxSize));
			text.RuntimeLayoutKey = key;
		}
		return &text.RuntimeLayout;
	}

	bool Scene::BakeNavigation(std::string& error)
	{
		// Mesh colliders bake from their meshes, which may not have been loaded yet (an entity made this frame).
		UpdateRuntimeCaches();
		Scope<NavMesh> navMesh = NavMesh::Build(CollectNavigationGeometry(*this), m_Settings.Navigation, error);
		if (!navMesh)
			return false;
		m_NavMesh = Ref<NavMesh>(std::move(navMesh));
		return true;
	}

	void Scene::OnUpdateEditor(Timestep timestep)
	{
		UpdateRuntimeCaches();
		UpdateAnimators(timestep.GetSeconds());
		UpdateParticles(timestep.GetSeconds());
		UpdateTrails(timestep.GetSeconds());
		UpdateBeams(timestep.GetSeconds());
		UpdateEffectShapes(timestep.GetSeconds());
	}

	void Scene::UpdateEffectShapes(float seconds)
	{
		BW_PROFILE_SCOPE("EffectShapes");
		auto shapes = m_Registry.view<EffectShapeComponent>();
		for (entt::entity handle : shapes)
			EffectShapeSystem::Update(shapes.get<EffectShapeComponent>(handle), seconds);
	}

	void Scene::UpdateBeams(float seconds)
	{
		BW_PROFILE_SCOPE("Beams");
		seconds = std::isfinite(seconds) ? std::max(seconds, 0.0f) : 0.0f;
		auto beams = m_Registry.view<BeamComponent>();
		for (entt::entity handle : beams)
		{
			BeamComponent& beam = beams.get<BeamComponent>(handle);
			Entity entity(handle, this);
			beam.RuntimeValid = false;
			beam.RuntimeHit = false;
			beam.RuntimeHitEntity = 0;
			// A long run's clock wraps, so its sines stay precise.
			beam.RuntimeTime = std::fmod(beam.RuntimeTime + seconds, 10000.0f);
			if (!beam.Active)
				continue;
			const glm::mat4 world = GetWorldTransform(entity);
			beam.RuntimeStart = glm::vec3(world[3]);
			switch (beam.EndMode)
			{
				case BeamEndMode::Point:
					beam.RuntimeEnd = glm::vec3(world * glm::vec4(beam.EndPoint, 1.0f));
					beam.RuntimeValid = true;
					break;
				case BeamEndMode::Entity:
					if (Entity target = TryGetEntityByUUID(beam.Target); target && target != entity)
					{
						beam.RuntimeEnd = glm::vec3(GetWorldTransform(target)[3]);
						beam.RuntimeValid = true;
					}
					break;
				case BeamEndMode::Raycast:
				{
					const glm::vec3 forward = -glm::vec3(world[2]);
					const float forwardLength = glm::length(forward);
					if (!(forwardLength > 1e-6f))
						break;
					const glm::vec3 direction = forward / forwardLength;
					beam.RuntimeEnd = beam.RuntimeStart + direction * beam.Length;
					RaycastHit hit;
					if (m_PhysicsWorld && m_PhysicsWorld->Raycast(beam.RuntimeStart, direction, beam.Length, hit, entity.GetUUID()))
					{
						beam.RuntimeEnd = hit.Point;
						beam.RuntimeHit = true;
						beam.RuntimeHitEntity = hit.Entity;
						beam.RuntimeHitNormal = hit.Normal;
					}
					beam.RuntimeValid = true;
					break;
				}
			}
		}
	}

	void Scene::UpdateTrails(float seconds)
	{
		BW_PROFILE_SCOPE("Trails");
		auto trails = m_Registry.view<TrailComponent>();
		for (entt::entity handle : trails)
			TrailSystem::Update(trails.get<TrailComponent>(handle), glm::vec3(GetWorldTransform(Entity(handle, this))[3]), seconds);
	}

	void Scene::UpdateParticles(float seconds)
	{
		BW_PROFILE_SCOPE("Particles");
		auto emitters = m_Registry.view<ParticleEmitterComponent>();
		for (entt::entity handle : emitters)
		{
			Entity entity(handle, this);
			ParticleEmitterComponent& emitter = emitters.get<ParticleEmitterComponent>(handle);
			// A new emitter, or a copy of another (a duplicate), gets its own entity's random sequence
			// the first time it updates, keeping whatever a script already asked of it.
			const UUID id = entity.GetUUID();
			if (emitter.RuntimeRandom == 0 || emitter.RuntimeSeed != static_cast<uint64_t>(id))
				ParticleSystem::Seed(emitter, id);
			ParticleSystem::Update(emitter, GetWorldTransform(entity), m_Settings.Gravity, seconds);
		}
	}

	namespace {

		// The clip an Animator starts with: its Clip, or the mesh's first. Null for an unknown name.
		Ref<AnimationClip> FindStartClip(const AnimatorComponent& animator, const MeshSkin& skin)
		{
			if (animator.Clip.empty())
				return skin.Clips.empty() ? nullptr : skin.Clips.front();
			return skin.FindClip(animator.Clip);
		}

	}

	void Scene::StartAnimator(Entity entity, AnimatorComponent& animator, const Ref<Mesh>& mesh, bool warnUnknownClip)
	{
		animator.RuntimeMesh = mesh;
		animator.RuntimePlayer.Reset();
		animator.RuntimeSkinMatrices.clear();
		const MeshSkin* skin = mesh ? mesh->GetSkin() : nullptr;
		if (!skin)
			return;
		const Ref<AnimationClip> startClip = FindStartClip(animator, *skin);
		if (!startClip && !animator.Clip.empty() && warnUnknownClip)
			BW_CORE_WARN("Animator on '{}': the mesh has no clip '{}'", entity.GetName(), animator.Clip);
		if (startClip && (animator.PlayOnStart || !m_IsRunning))
			animator.RuntimePlayer.Play(startClip, 0.0f, animator.Speed);
	}

	void Scene::UpdateAnimators(float seconds)
	{
		// Clips that reached their end, told to scripts once every Animator has moved (a handler may
		// add or remove Animators).
		std::vector<std::pair<UUID, std::string>> finished;
		auto animators = m_Registry.view<AnimatorComponent, MeshRendererComponent>();
		for (entt::entity handle : animators)
		{
			AnimatorComponent& animator = animators.get<AnimatorComponent>(handle);
			const Ref<Mesh>& mesh = animators.get<MeshRendererComponent>(handle).RuntimeMesh;
			const MeshSkin* skin = mesh ? mesh->GetSkin() : nullptr;
			const bool active = m_IsRunning || animator.RuntimePreview;
			if (!active || !skin)
			{
				if (animator.RuntimeMesh || !animator.RuntimeSkinMatrices.empty())
				{
					animator.RuntimePlayer.Reset();
					animator.RuntimeMesh = nullptr;
					animator.RuntimeSkinMatrices.clear();
				}
				continue;
			}

			// A new mesh (or the game's start) begins with the Animator's clip; the editor's preview
			// always plays it, and follows a change of clip.
			const Ref<AnimationClip> startClip = FindStartClip(animator, *skin);
			const bool newMesh = animator.RuntimeMesh != mesh;
			const bool clipChanged = !m_IsRunning && animator.RuntimePlayer.GetClip() != startClip;
			if (newMesh || clipChanged)
				StartAnimator(Entity(handle, this), animator, mesh, newMesh);

			if (animator.RuntimePlayer.Update(seconds, animator.Speed, animator.Loop) && m_IsRunning)
				finished.emplace_back(m_Registry.get<IDComponent>(handle).ID, animator.RuntimePlayer.GetClip()->GetName());
			if (!animator.RuntimePlayer.ComputeSkinMatrices(*skin->Rig, animator.RuntimeSkinMatrices))
				animator.RuntimeSkinMatrices.clear();
		}

		for (const auto& [id, clip] : finished)
		{
			Scripting::ScriptEntity entity;
			entity.SceneID = m_SceneID;
			entity.ID = id;
			Scripting::EmitScriptEvent("animation finished", { entity, clip });
		}
	}

	void Scene::UpdateRuntimeCaches()
	{
		// Without a GPU (dedicated servers, unit tests) there is nothing to load.
		if (!Renderer::IsInitialized())
			return;

		auto meshes = m_Registry.view<MeshRendererComponent>();
		for (entt::entity entity : meshes)
		{
			MeshRendererComponent& meshRenderer = meshes.get<MeshRendererComponent>(entity);
			if (meshRenderer.LoadedMeshPath != meshRenderer.MeshPath)
			{
				meshRenderer.RuntimeMesh = AssetManager::GetMesh(meshRenderer.MeshPath);
				meshRenderer.LoadedMeshPath = meshRenderer.MeshPath;
			}
			if (meshRenderer.LoadedMaterialPath != meshRenderer.MaterialPath)
			{
				meshRenderer.RuntimeMaterial = AssetManager::GetMaterial(meshRenderer.MaterialPath);
				meshRenderer.LoadedMaterialPath = meshRenderer.MaterialPath;
			}
		}

		// The procedural sky follows the scene's sun.
		glm::vec3 sunDirection(0.0f);
		bool hasSun = false;
		auto directionalLights = m_Registry.view<DirectionalLightComponent>();
		for (entt::entity entity : directionalLights)
		{
			// The light travels along -Z, so +Z points towards the sun.
			sunDirection = glm::normalize(glm::vec3(GetWorldTransform(Entity(entity, this))[2]));
			hasSun = true;
			break;
		}

		auto skies = m_Registry.view<SkyLightComponent>();
		for (entt::entity entity : skies)
		{
			SkyLightComponent& sky = skies.get<SkyLightComponent>(entity);
			if (sky.EnvironmentPath.empty())
			{
				if (hasSun && glm::length(sunDirection) > 0.5f)
					sky.Sky.SunDirection = glm::normalize(sunDirection);

				const bool changed = !sky.RuntimeEnvironment || !sky.LoadedEnvironmentPath.empty() || !(sky.LoadedSky == sky.Sky);
				if (changed)
				{
					EnvironmentQuality quality;
					quality.CubeSize = 256;
					quality.PrefilteredSize = 128;
					quality.PrefilterSamples = 128;
					sky.RuntimeEnvironment = Environment::CreateProcedural(sky.Sky, quality);
					sky.LoadedSky = sky.Sky;
					sky.LoadedEnvironmentPath.clear();
				}
			}
			else if (sky.LoadedEnvironmentPath != sky.EnvironmentPath)
			{
				sky.RuntimeEnvironment = AssetManager::GetEnvironment(sky.EnvironmentPath);
				sky.LoadedEnvironmentPath = sky.EnvironmentPath;
			}
		}
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		Entity fallback;
		for (entt::entity entity : view)
		{
			if (view.get<CameraComponent>(entity).Primary)
				return Entity(entity, this);
			if (!fallback)
				fallback = Entity(entity, this);
		}
		return fallback;
	}

	bool Scene::GetPrimaryCamera(float aspectRatio, RenderCamera& outCamera, bool withShake)
	{
		Entity cameraEntity = GetPrimaryCameraEntity();
		if (!cameraEntity)
			return false;

		const CameraComponent& camera = cameraEntity.GetComponent<CameraComponent>();
		const glm::mat4 world = GetWorldTransform(cameraEntity);
		glm::vec3 translation(0.0f), scale(1.0f);
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		Math::DecomposeTransform(world, translation, rotation, scale);

		// The view ignores scale so scaled camera parents do not distort the image.
		glm::mat4 cameraTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
		// A shake doesn't show while the game is paused (a pause menu over a tilted view would look broken);
		// it goes on when the game does.
		if (withShake && m_IsRunning && !m_GamePaused && m_CameraShake.IsShaking())
			cameraTransform *= m_CameraShake.GetOffset();
		outCamera.View = glm::inverse(cameraTransform);
		outCamera.Projection = camera.GetProjection(aspectRatio);
		outCamera.Position = glm::vec3(cameraTransform[3]);
		outCamera.NearClip = camera.NearClip;
		outCamera.FarClip = camera.FarClip;
		return true;
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = std::max(width, 1u);
		m_ViewportHeight = std::max(height, 1u);
	}

	void Scene::OnRender(SceneRenderer& renderer, const RenderCamera& camera, const std::function<void(SceneRenderer&)>& submitExtras, bool editorView)
	{
		BW_PROFILE_SCOPE("Scene Render");
		UpdateRuntimeCaches();

		// Entities of groups hidden in the editor, with everything under them (editor view only).
		std::unordered_set<entt::entity> hidden;
		if (editorView && std::any_of(m_Groups.begin(), m_Groups.end(), [](const SceneGroup& group) { return group.HiddenInEditor; }))
		{
			for (const SceneGroup& group : m_Groups)
			{
				if (!group.HiddenInEditor)
					continue;
				for (Entity entity : GetGroupEntities(group.ID, true))
					hidden.insert(entity);
			}
		}

		// Scene-level render settings (editor overlay switches stay with the renderer).
		SceneRendererSettings& settings = renderer.GetSettings();
		const bool showGrid = settings.ShowGrid;
		const bool showOutline = settings.ShowSelectionOutline;
		const bool showGlow = settings.ShowSelectionGlow;
		const bool showUI = settings.ShowUI;
		const float gridCellSize = settings.GridCellSize;
		const glm::vec4 outlineColor = settings.OutlineColor;
		settings = m_Settings.Rendering;
		settings.ShowGrid = showGrid;
		settings.ShowSelectionOutline = showOutline;
		settings.ShowSelectionGlow = showGlow;
		settings.ShowUI = showUI;
		settings.GridCellSize = gridCellSize;
		settings.OutlineColor = outlineColor;

		UpdateUILayout(glm::vec2(static_cast<float>(renderer.GetViewportWidth()), static_cast<float>(renderer.GetViewportHeight())));
		renderer.BeginScene(camera);

		EnvironmentSettings environment;
		environment.AmbientColor = m_Settings.AmbientColor;
		auto skies = m_Registry.view<SkyLightComponent>();
		for (entt::entity entity : skies)
		{
			const SkyLightComponent& sky = skies.get<SkyLightComponent>(entity);
			environment.EnvironmentMap = sky.RuntimeEnvironment;
			environment.Intensity = sky.Intensity;
			environment.Rotation = sky.Rotation;
			environment.SkyboxBlur = sky.SkyboxBlur;
			break;
		}
		renderer.SetEnvironment(environment);

		auto directionalLights = m_Registry.view<DirectionalLightComponent>();
		for (entt::entity entity : directionalLights)
		{
			if (hidden.contains(entity))
				continue;
			const DirectionalLightComponent& light = directionalLights.get<DirectionalLightComponent>(entity);
			const glm::mat4 world = GetWorldTransform(Entity(entity, this));
			DirectionalLightData data;
			data.Direction = -glm::normalize(glm::vec3(world[2]));
			data.Color = light.Color;
			data.Intensity = light.Intensity;
			data.CastShadows = light.CastShadows;
			renderer.SubmitDirectionalLight(data);
			break;
		}

		auto pointLights = m_Registry.view<PointLightComponent>();
		for (entt::entity entity : pointLights)
		{
			if (hidden.contains(entity))
				continue;
			const PointLightComponent& light = pointLights.get<PointLightComponent>(entity);
			PointLightData data;
			data.Position = GetWorldPosition(Entity(entity, this));
			data.Color = light.Color;
			data.Intensity = light.Intensity;
			data.Range = light.Range;
			renderer.SubmitPointLight(data);
		}

		auto spotLights = m_Registry.view<SpotLightComponent>();
		for (entt::entity entity : spotLights)
		{
			if (hidden.contains(entity))
				continue;
			const SpotLightComponent& light = spotLights.get<SpotLightComponent>(entity);
			const glm::mat4 world = GetWorldTransform(Entity(entity, this));
			SpotLightData data;
			data.Position = glm::vec3(world[3]);
			data.Direction = -glm::normalize(glm::vec3(world[2]));
			data.Color = light.Color;
			data.Intensity = light.Intensity;
			data.Range = light.Range;
			data.InnerAngle = light.InnerAngle;
			data.OuterAngle = light.OuterAngle;
			renderer.SubmitSpotLight(data);
		}

		auto meshes = m_Registry.view<MeshRendererComponent>();
		for (entt::entity handle : meshes)
		{
			const MeshRendererComponent& meshRenderer = meshes.get<MeshRendererComponent>(handle);
			if (!meshRenderer.Visible || !meshRenderer.RuntimeMesh || hidden.contains(handle))
				continue;
			Entity entity(handle, this);
			const AnimatorComponent* animator = m_Registry.try_get<AnimatorComponent>(handle);
			const bool posed = animator && animator->RuntimeMesh == meshRenderer.RuntimeMesh && !animator->RuntimeSkinMatrices.empty();
			renderer.SubmitMesh(meshRenderer.RuntimeMesh, GetWorldTransform(entity), entity.GetRenderID(),
				meshRenderer.RuntimeMaterial, meshRenderer.CastShadows, posed ? &animator->RuntimeSkinMatrices : nullptr);
		}

		// Decals, each filling its box scaled by its size.
		auto decals = m_Registry.view<DecalComponent>();
		for (entt::entity handle : decals)
		{
			DecalComponent& decal = decals.get<DecalComponent>(handle);
			if (decal.LoadedTexturePath != decal.TexturePath)
			{
				decal.RuntimeTexture = decal.TexturePath.empty() ? nullptr : AssetManager::GetTexture(decal.TexturePath, TextureColorSpace::SRGB);
				decal.LoadedTexturePath = decal.TexturePath;
			}
			if (hidden.contains(handle))
				continue;
			const glm::mat4 box = GetWorldTransform(Entity(handle, this)) * glm::scale(glm::mat4(1.0f), decal.Size);
			renderer.SubmitDecal(box, decal.Color, decal.RuntimeTexture, decal.MaxAngle, decal.SortOrder);
		}

		// World text: centered on its entity in the entity's XY plane, or turned toward the camera.
		const glm::mat3 cameraRotation = glm::mat3(glm::inverse(camera.View));
		auto texts = m_Registry.view<TextComponent>();
		for (entt::entity handle : texts)
		{
			TextComponent& text = texts.get<TextComponent>(handle);
			if (!text.Visible || text.Text.empty() || m_UIEntities.contains(handle) || hidden.contains(handle))
				continue;
			const TextLayout* layout = UpdateTextLayout(text);
			if (!layout)
				continue;

			Entity entity(handle, this);
			glm::mat4 world = GetWorldTransform(entity);
			if (text.FaceCamera)
			{
				const glm::vec3 scale(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
				glm::mat4 facing = glm::mat4(cameraRotation);
				facing[3] = world[3];
				world = glm::scale(facing, scale);
			}
			// The layout's box (y down) becomes a box centered on the entity with y up.
			const glm::vec2 box(text.BoxSize.x > 0.0f ? text.BoxSize.x : layout->Size.x, text.BoxSize.y > 0.0f ? text.BoxSize.y : layout->Size.y);
			const glm::mat4 boxToLocal = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f))
				* glm::translate(glm::mat4(1.0f), glm::vec3(-box.x * 0.5f, -box.y * 0.5f, 0.0f));

			TextStyle style;
			style.Color = text.Color;
			style.OutlineColor = text.OutlineColor;
			style.OutlineWidth = text.OutlineWidth;
			const glm::vec4 clip(0.0f, 0.0f, box.x, box.y);
			const bool clipped = text.Overflow == TextOverflow::Clip && text.BoxSize.x > 0.0f && text.BoxSize.y > 0.0f;
			renderer.SubmitText(text.RuntimeFont, *layout, world * boxToLocal, style, entity.GetRenderID(), clipped ? &clip : nullptr);
		}

		// Particles, each emitter as one batch.
		std::vector<ParticleQuad> quads;
		auto emitters = m_Registry.view<ParticleEmitterComponent>();
		for (entt::entity handle : emitters)
		{
			ParticleEmitterComponent& emitter = emitters.get<ParticleEmitterComponent>(handle);
			if (emitter.LoadedTexturePath != emitter.TexturePath)
			{
				emitter.RuntimeTexture = emitter.TexturePath.empty() ? nullptr : AssetManager::GetTexture(emitter.TexturePath, TextureColorSpace::SRGB);
				emitter.LoadedTexturePath = emitter.TexturePath;
			}
			if (emitter.RuntimeParticles.empty() || hidden.contains(handle))
				continue;

			const glm::mat4 entityToWorld = GetWorldTransform(Entity(handle, this));
			const glm::mat4 toWorld = emitter.WorldSpace ? glm::mat4(1.0f) : entityToWorld;
			const glm::mat3 turnToWorld(toWorld);
			// Flat particles lie across the emitter's +Y, wherever their own space is.
			const glm::vec3 flatNormal = glm::mat3(entityToWorld) * glm::vec3(0.0f, 1.0f, 0.0f);
			const ParticleQuad::FacingMode facing = emitter.Facing == ParticleFacing::Stretched ? ParticleQuad::FacingMode::Stretched
				: emitter.Facing == ParticleFacing::Flat ? ParticleQuad::FacingMode::Flat : ParticleQuad::FacingMode::Camera;
			quads.clear();
			quads.reserve(emitter.RuntimeParticles.size());
			for (const Particle& particle : emitter.RuntimeParticles)
			{
				ParticleQuad quad;
				quad.Position = emitter.WorldSpace ? particle.Position : glm::vec3(toWorld * glm::vec4(particle.Position, 1.0f));
				quad.Size = ParticleSystem::GetSize(emitter, particle);
				quad.Color = ParticleSystem::GetColor(emitter, particle);
				quad.TexRect = ParticleSystem::GetFrameRect(emitter, particle);
				quad.Rotation = particle.Rotation;
				quad.Facing = facing;
				if (facing == ParticleQuad::FacingMode::Stretched)
				{
					const glm::vec3 velocity = emitter.WorldSpace ? particle.Velocity : turnToWorld * particle.Velocity;
					quad.Axis = velocity;
					quad.Length = glm::length(velocity) * emitter.StretchAmount;
				}
				else if (facing == ParticleQuad::FacingMode::Flat)
				{
					quad.Axis = flatNormal;
				}
				quads.push_back(quad);
			}
			renderer.SubmitParticles(quads.data(), quads.size(), emitter.Blend == ParticleBlend::Additive, emitter.RuntimeTexture);
		}

		// Effect shapes, each as one surface at its point in its cycle.
		std::vector<EffectVertex> surface;
		auto effectShapes = m_Registry.view<EffectShapeComponent>();
		for (entt::entity handle : effectShapes)
		{
			EffectShapeComponent& shape = effectShapes.get<EffectShapeComponent>(handle);
			if (shape.LoadedTexturePath != shape.TexturePath)
			{
				shape.RuntimeTexture = shape.TexturePath.empty() ? nullptr : AssetManager::GetTexture(shape.TexturePath, TextureColorSpace::SRGB);
				shape.LoadedTexturePath = shape.TexturePath;
			}
			if (shape.RuntimeFinished || hidden.contains(handle))
				continue;
			const glm::mat4 world = GetWorldTransform(Entity(handle, this));
			EffectShapeSystem::BuildMesh(shape, world, surface);
			EffectMaterial material = EffectShapeSystem::BuildMaterial(shape, world);
			material.Texture = shape.RuntimeTexture;
			renderer.SubmitEffectMesh(surface.data(), surface.size(), material);
		}

		// Beams, each as one strip from its entity to where it ends.
		std::vector<StripPoint> strip;
		auto beams = m_Registry.view<BeamComponent>();
		for (entt::entity handle : beams)
		{
			BeamComponent& beam = beams.get<BeamComponent>(handle);
			if (beam.LoadedTexturePath != beam.TexturePath)
			{
				beam.RuntimeTexture = beam.TexturePath.empty() ? nullptr : AssetManager::GetTexture(beam.TexturePath, TextureColorSpace::SRGB);
				beam.LoadedTexturePath = beam.TexturePath;
			}
			if (!beam.RuntimeValid || hidden.contains(handle))
				continue;
			BeamSystem::BuildStrip(beam, strip);
			renderer.SubmitStrip(strip.data(), strip.size(), beam.Blend == ParticleBlend::Additive, beam.RuntimeTexture, true);
		}

		// Trails, each as one strip from its entity back along where it has been.
		auto trails = m_Registry.view<TrailComponent>();
		for (entt::entity handle : trails)
		{
			TrailComponent& trail = trails.get<TrailComponent>(handle);
			if (trail.LoadedTexturePath != trail.TexturePath)
			{
				trail.RuntimeTexture = trail.TexturePath.empty() ? nullptr : AssetManager::GetTexture(trail.TexturePath, TextureColorSpace::SRGB);
				trail.LoadedTexturePath = trail.TexturePath;
			}
			if (trail.RuntimePoints.empty() || hidden.contains(handle))
				continue;
			TrailSystem::BuildStrip(trail, glm::vec3(GetWorldTransform(Entity(handle, this))[3]), strip);
			renderer.SubmitStrip(strip.data(), strip.size(), trail.Blend == ParticleBlend::Additive, trail.RuntimeTexture,
				trail.TextureMode == TrailTextureMode::Tile);
		}

		SubmitUI(renderer);

		if (submitExtras)
			submitExtras(renderer);
		renderer.EndScene();
	}

}
