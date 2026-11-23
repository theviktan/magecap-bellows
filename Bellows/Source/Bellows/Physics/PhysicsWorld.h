#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Bellows {

	class Scene;
	class Entity;

	enum class ContactEventType : uint8_t
	{
		CollisionEnter = 0,
		CollisionExit,
		TriggerEnter,
		TriggerExit,
	};

	struct ContactEvent
	{
		ContactEventType Type = ContactEventType::CollisionEnter;
		UUID EntityA = 0;
		UUID EntityB = 0;
	};

	struct RaycastHit
	{
		UUID Entity = 0;
		glm::vec3 Point { 0.0f };
		glm::vec3 Normal { 0.0f };
		float Distance = 0.0f;
	};

	// Rigid-body simulation for one running scene, backed by Jolt Physics.
	// Bodies are created from RigidBody + collider components when play starts or when they appear later.
	class PhysicsWorld
	{
	public:
		explicit PhysicsWorld(Scene& scene);
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Creates bodies for entities that do not have one yet.
		void SyncBodies();
		void DestroyBody(Entity entity);

		// Pushes kinematic/static transforms to Jolt, steps the simulation and writes dynamic transforms back.
		void Step(float timestep);

		// Contact events collected during the last Step (in order).
		const std::vector<ContactEvent>& GetContactEvents() const { return m_ContactEvents; }

		bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit) const;
		// The same, hitting only the ground a navmesh is baked from: static bodies that aren't triggers,
		// other than 'ignore''s.
		bool RaycastStatic(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit, UUID ignore = 0) const;

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		// Body control (no-ops for entities without a body)
		void AddForce(Entity entity, const glm::vec3& force);
		void AddImpulse(Entity entity, const glm::vec3& impulse);
		void AddTorque(Entity entity, const glm::vec3& torque);
		void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(Entity entity) const;
		void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetAngularVelocity(Entity entity) const;
		// Teleports the body to the entity's current world transform.
		void TeleportToEntity(Entity entity);
		// Applies the RigidBody's friction, restitution, damping and gravity scale to the live body.
		// (Type, mass, locks and colliders take effect when the body is created.)
		void ApplyBodySettings(Entity entity);
		bool HasBody(Entity entity) const;

		uint32_t GetBodyCount() const;

		// Jolt global state (allocator, factory, type registry); reference counted.
		static void InitializeGlobals();
		static void ShutdownGlobals();
	private:
		bool CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool staticOnly, UUID ignore, RaycastHit& outHit) const;
		void CreateBody(Entity entity);
	private:
		struct JoltState;
		Scope<JoltState> m_Jolt;
		Scene& m_Scene;
		std::vector<ContactEvent> m_ContactEvents;
	};

}
