#pragma once

#include "Bellows/Core/Assert.h"
#include "Bellows/Core/UUID.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Scene.h"

#include <entt/entt.hpp>

namespace Bellows {

	// Lightweight handle to an entity in a Scene. Copy freely; it does not own anything.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene)
			: m_EntityHandle(handle), m_Scene(scene) {}

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			BW_CORE_ASSERT(!HasComponent<T>(), "Entity already has component");
			return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			return m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template<typename T>
		T& GetComponent()
		{
			BW_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		const T& GetComponent() const
		{
			BW_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		T* TryGetComponent()
		{
			return m_Scene->m_Registry.try_get<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent() const
		{
			return m_Scene && m_Scene->m_Registry.valid(m_EntityHandle) && m_Scene->m_Registry.all_of<T>(m_EntityHandle);
		}

		template<typename T>
		void RemoveComponent()
		{
			BW_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		bool IsValid() const { return m_Scene && m_EntityHandle != entt::null && m_Scene->m_Registry.valid(m_EntityHandle); }
		explicit operator bool() const { return IsValid(); }
		operator entt::entity() const { return m_EntityHandle; }

		// Non-zero ID used by the renderer's picking buffer (entt handle + 1).
		uint32_t GetRenderID() const { return static_cast<uint32_t>(m_EntityHandle) + 1; }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }
		void SetName(const std::string& name) { GetComponent<TagComponent>().Tag = name; }
		TransformComponent& GetTransform() { return GetComponent<TransformComponent>(); }

		Entity GetParent() const;
		std::vector<Entity> GetChildren() const;
		bool IsDescendantOf(Entity ancestor) const;

		Scene* GetScene() const { return m_Scene; }
		entt::entity GetHandle() const { return m_EntityHandle; }

		bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
		bool operator!=(const Entity& other) const { return !(*this == other); }
	private:
		entt::entity m_EntityHandle = entt::null;
		Scene* m_Scene = nullptr;
	};

}
