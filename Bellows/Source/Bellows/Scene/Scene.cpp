#include "bwpch.h"
#include "Bellows/Scene/Scene.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Scene/Entity.h"
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
		scene->m_ViewportWidth = source->m_ViewportWidth;
		scene->m_ViewportHeight = source->m_ViewportHeight;

		auto view = source->m_Registry.view<IDComponent>();
		for (entt::entity entity : view)
		{
			const UUID uuid = source->m_Registry.get<IDComponent>(entity).ID;
			const std::string& name = source->m_Registry.get<TagComponent>(entity).Tag;
			scene->CreateEntityWithUUID(uuid, name);
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

		std::function<Entity(Entity, Entity)> duplicate = [&](Entity source, Entity parent) -> Entity
		{
			Entity copy = CreateEntity(source.GetName());
			CopyEntityComponents(AllComponents {}, copy, source);
			if (parent)
				SetParent(copy, parent, false);
			for (Entity child : source.GetChildren())
				duplicate(child, copy);
			return copy;
		};

		Entity copy = duplicate(entity, entity.GetParent());

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
		m_Time = 0.0f;
		m_FrameCount = 0;
		m_PhysicsAccumulator = 0.0f;
		s_ActiveScene = this;

		UpdateRuntimeCaches();

		m_PhysicsWorld = CreateScope<PhysicsWorld>(*this);
		m_PhysicsWorld->SyncBodies();

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
		m_Audio.reset();
		m_PhysicsWorld.reset();
		m_IsRunning = false;
		if (s_ActiveScene == this)
			s_ActiveScene = nullptr;
	}

	void Scene::OnUpdateRuntime(Timestep timestep)
	{
		if (!m_IsRunning)
			return;

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

		if (ScriptEngine::IsInitialized())
		{
			ScriptEngine::InstantiatePendingScripts(*this);
			ScriptEngine::OnUpdate(*this, deltaTime);
		}

		// Fixed-step physics. At most a few steps per frame so a slow frame cannot spiral.
		const float fixedTimestep = std::clamp(m_Settings.FixedTimestep, 1.0f / 480.0f, 1.0f / 15.0f);
		m_PhysicsAccumulator = std::min(m_PhysicsAccumulator + deltaTime, fixedTimestep * 5.0f);
		while (m_PhysicsAccumulator >= fixedTimestep)
		{
			m_PhysicsWorld->SetGravity(m_Settings.Gravity);
			m_PhysicsWorld->SyncBodies();
			if (ScriptEngine::IsInitialized())
				ScriptEngine::OnFixedUpdate(*this, fixedTimestep);
			m_PhysicsWorld->Step(fixedTimestep);
			if (ScriptEngine::IsInitialized())
			{
				for (const ContactEvent& event : m_PhysicsWorld->GetContactEvents())
					ScriptEngine::OnContactEvent(*this, event);
			}
			m_PhysicsAccumulator -= fixedTimestep;
		}

		if (m_Audio)
			m_Audio->OnUpdate();

		FlushPendingDestroys();

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

	void Scene::OnUpdateEditor(Timestep timestep)
	{
		UpdateRuntimeCaches();
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

	bool Scene::GetPrimaryCamera(float aspectRatio, RenderCamera& outCamera)
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
		const glm::mat4 cameraTransform = glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation);
		outCamera.View = glm::inverse(cameraTransform);
		outCamera.Projection = camera.GetProjection(aspectRatio);
		outCamera.Position = translation;
		outCamera.NearClip = camera.NearClip;
		outCamera.FarClip = camera.FarClip;
		return true;
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = std::max(width, 1u);
		m_ViewportHeight = std::max(height, 1u);
	}

	void Scene::OnRender(SceneRenderer& renderer, const RenderCamera& camera, const std::function<void(SceneRenderer&)>& submitExtras)
	{
		UpdateRuntimeCaches();

		// Scene-level render settings (editor overlay switches stay with the renderer).
		SceneRendererSettings& settings = renderer.GetSettings();
		const bool showGrid = settings.ShowGrid;
		const bool showOutline = settings.ShowSelectionOutline;
		const bool showUI = settings.ShowUI;
		const float gridCellSize = settings.GridCellSize;
		const glm::vec4 outlineColor = settings.OutlineColor;
		settings = m_Settings.Rendering;
		settings.ShowGrid = showGrid;
		settings.ShowSelectionOutline = showOutline;
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
			if (!meshRenderer.Visible || !meshRenderer.RuntimeMesh)
				continue;
			Entity entity(handle, this);
			renderer.SubmitMesh(meshRenderer.RuntimeMesh, GetWorldTransform(entity), entity.GetRenderID(),
				meshRenderer.RuntimeMaterial, meshRenderer.CastShadows);
		}

		// World text: centered on its entity in the entity's XY plane, or turned toward the camera.
		const glm::mat3 cameraRotation = glm::mat3(glm::inverse(camera.View));
		auto texts = m_Registry.view<TextComponent>();
		for (entt::entity handle : texts)
		{
			TextComponent& text = texts.get<TextComponent>(handle);
			if (!text.Visible || text.Text.empty() || m_UIEntities.contains(handle))
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

		SubmitUI(renderer);

		if (submitExtras)
			submitExtras(renderer);
		renderer.EndScene();
	}

	namespace {

		// Where an entity sits in the hierarchy: its index among the roots, then among each parent's
		// children down to it. Comparing these orders entities as the hierarchy panel lists them.
		std::vector<size_t> GetHierarchyPath(Entity entity, const std::vector<UUID>& roots)
		{
			std::vector<size_t> path;
			for (Entity current = entity; current;)
			{
				const UUID id = current.GetUUID();
				Entity parent = current.GetParent();
				const std::vector<UUID>& siblings = parent ? parent.GetComponent<RelationshipComponent>().Children : roots;
				path.push_back(static_cast<size_t>(std::find(siblings.begin(), siblings.end(), id) - siblings.begin()));
				current = parent;
			}
			std::reverse(path.begin(), path.end());
			return path;
		}

	}

	void Scene::UpdateUILayout(const glm::vec2& screenSize)
	{
		m_UIElements.clear();
		m_UIEntities.clear();

		// Canvases draw by SortOrder, and in hierarchy order where the sort orders are equal.
		struct CanvasEntry
		{
			entt::entity Handle;
			int SortOrder;
			std::vector<size_t> Path;
		};
		std::vector<CanvasEntry> canvases;
		auto canvasView = m_Registry.view<UICanvasComponent>();
		for (entt::entity handle : canvasView)
			canvases.push_back({ handle, canvasView.get<UICanvasComponent>(handle).SortOrder, GetHierarchyPath(Entity(handle, this), m_RootEntities) });
		std::sort(canvases.begin(), canvases.end(), [](const CanvasEntry& a, const CanvasEntry& b)
		{
			return a.SortOrder != b.SortOrder ? a.SortOrder < b.SortOrder : a.Path < b.Path;
		});

		const UIRectangle screen { glm::vec2(0.0f), glm::max(screenSize, glm::vec2(0.0f)) };
		std::function<void(entt::entity, const UIRectangle&, float, bool)> visit =
			[&](entt::entity handle, const UIRectangle& parentRect, float scale, bool visible)
		{
			m_UIEntities.insert(handle);
			UIRectangle rect = parentRect;
			if (auto* ui = m_Registry.try_get<UIRectComponent>(handle))
			{
				rect = ResolveUIRect(ui->AnchorMin, ui->AnchorMax, ui->OffsetMin, ui->OffsetMax, parentRect, scale);
				ui->RuntimeRect = rect;
				visible = visible && ui->Visible;
			}
			if (visible)
				m_UIElements.push_back({ handle, rect, scale });
			for (UUID childID : m_Registry.get<RelationshipComponent>(handle).Children)
			{
				// A canvas below starts a UI of its own.
				auto it = m_EntityMap.find(childID);
				if (it != m_EntityMap.end() && !m_Registry.all_of<UICanvasComponent>(it->second))
					visit(it->second, rect, scale, visible);
			}
		};

		for (const CanvasEntry& entry : canvases)
		{
			const UICanvasComponent& canvas = m_Registry.get<UICanvasComponent>(entry.Handle);
			const float scale = ComputeUIScale(canvas.ReferenceSize, canvas.ScaleMode, screen.GetSize());
			m_UIEntities.insert(entry.Handle);
			if (canvas.Visible)
				m_UIElements.push_back({ entry.Handle, screen, scale });
			for (UUID childID : m_Registry.get<RelationshipComponent>(entry.Handle).Children)
			{
				auto it = m_EntityMap.find(childID);
				if (it != m_EntityMap.end() && !m_Registry.all_of<UICanvasComponent>(it->second))
					visit(it->second, screen, scale, canvas.Visible);
			}
		}
	}

	bool Scene::IsUIEntity(Entity entity) const
	{
		return entity && m_UIEntities.contains(static_cast<entt::entity>(entity));
	}

	std::vector<Entity> Scene::GetVisibleUIEntities()
	{
		std::vector<Entity> entities;
		entities.reserve(m_UIElements.size());
		for (const UIElement& element : m_UIElements)
			entities.emplace_back(element.Handle, this);
		return entities;
	}

	bool Scene::IsInsideUICanvas(Entity entity)
	{
		for (Entity current = entity; current; current = current.GetParent())
		{
			if (current.HasComponent<UICanvasComponent>())
				return true;
		}
		return false;
	}

	void Scene::SubmitUI(SceneRenderer& renderer)
	{
		for (const UIElement& element : m_UIElements)
		{
			Entity entity(element.Handle, this);
			if (auto* image = m_Registry.try_get<UIImageComponent>(element.Handle); image && image->Visible)
			{
				if (image->LoadedTexturePath != image->TexturePath)
				{
					// UI is drawn into the finished (sRGB) image, so image pixels are used as they are.
					image->RuntimeTexture = image->TexturePath.empty() ? nullptr : AssetManager::GetTexture(image->TexturePath, TextureColorSpace::Linear);
					image->LoadedTexturePath = image->TexturePath;
				}
				UIRectStyle style;
				style.Color = image->Color;
				style.CornerRadius = image->CornerRadius * element.Scale;
				style.BorderWidth = image->BorderWidth * element.Scale;
				style.BorderColor = image->BorderColor;
				renderer.SubmitUIRect(element.Rect.Min, element.Rect.Max, style, image->RuntimeTexture, entity.GetRenderID());
			}

			if (auto* text = m_Registry.try_get<TextComponent>(element.Handle); text && text->Visible && !text->Text.empty())
			{
				const TextLayout* layout = LayoutTextComponent(*text, text->Size * element.Scale, element.Rect.GetSize());
				if (!layout)
					continue;
				TextStyle style;
				style.Color = text->Color;
				style.OutlineColor = text->OutlineColor;
				style.OutlineWidth = text->OutlineWidth;
				const glm::vec4 clip(element.Rect.Min, element.Rect.Max);
				renderer.SubmitUIText(text->RuntimeFont, *layout, element.Rect.Min, style, entity.GetRenderID(),
					text->Overflow == TextOverflow::Clip ? &clip : nullptr);
			}
		}
	}

}
