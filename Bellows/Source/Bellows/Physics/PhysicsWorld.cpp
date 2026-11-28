#include "bwpch.h"
#include "Bellows/Physics/PhysicsWorld.h"

#include "Bellows/Math/Math.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <thread>

namespace Bellows {

	namespace {

		namespace Layers
		{
			constexpr JPH::ObjectLayer NonMoving = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers
		{
			constexpr JPH::BroadPhaseLayer NonMoving(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr uint32_t Count = 2;
		}

		class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				if (layer == Layers::NonMoving)
					return broadPhaseLayer == BroadPhaseLayers::Moving;
				return true;
			}
		};

		class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				// Static bodies never need to test against each other.
				return !(a == Layers::NonMoving && b == Layers::NonMoving);
			}
		};

		// Collects contact begin/end notifications from Jolt's worker threads and
		// reduces them to one enter/exit per body pair.
		class ContactListener final : public JPH::ContactListener
		{
		public:
			struct PairEvent
			{
				bool Begin = true;
				JPH::BodyID BodyA;
				JPH::BodyID BodyB;
				bool IsTrigger = false;
			};

			void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override
			{
				std::scoped_lock lock(m_Mutex);
				const uint64_t key = MakeKey(body1.GetID(), body2.GetID());
				PairState& state = m_Pairs[key];
				state.IsTrigger = body1.IsSensor() || body2.IsSensor();
				if (++state.Count == 1)
					m_Events.push_back({ true, body1.GetID(), body2.GetID(), state.IsTrigger });
			}

			void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
			{
				std::scoped_lock lock(m_Mutex);
				const uint64_t key = MakeKey(pair.GetBody1ID(), pair.GetBody2ID());
				auto it = m_Pairs.find(key);
				if (it == m_Pairs.end())
					return;
				if (--it->second.Count <= 0)
				{
					m_Events.push_back({ false, pair.GetBody1ID(), pair.GetBody2ID(), it->second.IsTrigger });
					m_Pairs.erase(it);
				}
			}

			std::vector<PairEvent> TakeEvents()
			{
				std::scoped_lock lock(m_Mutex);
				std::vector<PairEvent> events;
				events.swap(m_Events);
				return events;
			}

			// Forgets every contact involving a removed body (no exit event is generated).
			void ForgetBody(JPH::BodyID body)
			{
				std::scoped_lock lock(m_Mutex);
				for (auto it = m_Pairs.begin(); it != m_Pairs.end();)
				{
					const uint32_t a = static_cast<uint32_t>(it->first >> 32);
					const uint32_t b = static_cast<uint32_t>(it->first & 0xFFFFFFFF);
					if (a == body.GetIndexAndSequenceNumber() || b == body.GetIndexAndSequenceNumber())
						it = m_Pairs.erase(it);
					else
						++it;
				}
				std::erase_if(m_Events, [body](const PairEvent& e) { return e.BodyA == body || e.BodyB == body; });
			}
		private:
			struct PairState
			{
				int Count = 0;
				bool IsTrigger = false;
			};

			static uint64_t MakeKey(JPH::BodyID a, JPH::BodyID b)
			{
				uint32_t first = a.GetIndexAndSequenceNumber();
				uint32_t second = b.GetIndexAndSequenceNumber();
				if (first > second)
					std::swap(first, second);
				return (static_cast<uint64_t>(first) << 32) | second;
			}

			std::mutex m_Mutex;
			std::unordered_map<uint64_t, PairState> m_Pairs;
			std::vector<PairEvent> m_Events;
		};

		JPH::Vec3 ToJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
		JPH::Quat ToJolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
		glm::vec3 FromJolt(const JPH::Vec3& v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
		glm::quat FromJolt(const JPH::Quat& q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

		void JoltTrace(const char* format, ...)
		{
			char buffer[1024];
			va_list arguments;
			va_start(arguments, format);
			vsnprintf(buffer, sizeof(buffer), format, arguments);
			va_end(arguments);
			BW_CORE_TRACE("[Jolt] {}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			BW_CORE_ERROR("[Jolt] Assertion failed: {} ({}) at {}:{}", expression, message ? message : "", file, line);
			Log::OnAssertionFailed();
			return false;
		}
#endif

		int s_GlobalReferenceCount = 0;

	}

	struct PhysicsWorld::JoltState
	{
		Scope<JPH::TempAllocatorImpl> TempAllocator;
		Scope<JPH::JobSystemThreadPool> JobSystem;
		BroadPhaseLayerInterface BroadPhaseLayers;
		ObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseFilter;
		ObjectLayerPairFilter ObjectPairFilter;
		ContactListener Contacts;
		JPH::PhysicsSystem System;
		std::unordered_map<UUID, JPH::BodyID> Bodies;
		std::unordered_map<UUID, glm::mat4> LastStaticTransforms;
	};

	void PhysicsWorld::InitializeGlobals()
	{
		if (s_GlobalReferenceCount++ > 0)
			return;
		JPH::RegisterDefaultAllocator();
		JPH::Trace = JoltTrace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();
	}

	void PhysicsWorld::ShutdownGlobals()
	{
		if (--s_GlobalReferenceCount > 0)
			return;
		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	PhysicsWorld::PhysicsWorld(Scene& scene)
		: m_Scene(scene)
	{
		InitializeGlobals();

		m_Jolt = CreateScope<JoltState>();
		m_Jolt->TempAllocator = CreateScope<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
		const int threadCount = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 4);
		m_Jolt->JobSystem = CreateScope<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threadCount);

		constexpr JPH::uint maxBodies = 16384;
		constexpr JPH::uint maxBodyPairs = 16384;
		constexpr JPH::uint maxContactConstraints = 8192;
		m_Jolt->System.Init(maxBodies, 0, maxBodyPairs, maxContactConstraints,
			m_Jolt->BroadPhaseLayers, m_Jolt->ObjectVsBroadPhaseFilter, m_Jolt->ObjectPairFilter);
		m_Jolt->System.SetContactListener(&m_Jolt->Contacts);
		SetGravity(scene.GetSettings().Gravity);
	}

	PhysicsWorld::~PhysicsWorld()
	{
		JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
		for (const auto& [uuid, bodyID] : m_Jolt->Bodies)
		{
			bodies.RemoveBody(bodyID);
			bodies.DestroyBody(bodyID);
		}
		m_Jolt->Bodies.clear();
		m_Jolt.reset();
		ShutdownGlobals();
	}

	void PhysicsWorld::SyncBodies()
	{
		std::vector<Entity> pending;
		auto view = m_Scene.GetAllEntitiesWith<RigidBodyComponent>();
		for (entt::entity handle : view)
		{
			Entity entity(handle, &m_Scene);
			if (m_Scene.IsPendingDestroy(entity))
				continue;
			if (!m_Jolt->Bodies.contains(entity.GetUUID()))
				pending.push_back(entity);
		}
		for (Entity entity : pending)
			CreateBody(entity);
		if (!pending.empty())
			m_Jolt->System.OptimizeBroadPhase();
	}

	void PhysicsWorld::CreateBody(Entity entity)
	{
		RigidBodyComponent& rigidBody = entity.GetComponent<RigidBodyComponent>();

		glm::vec3 translation(0.0f), scale(1.0f);
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (!Math::DecomposeTransform(m_Scene.GetWorldTransform(entity), translation, rotation, scale))
		{
			BW_CORE_ERROR("Physics: entity '{}' has a degenerate transform; no body created", entity.GetName());
			return;
		}
		scale = glm::abs(scale);

		struct ShapePart
		{
			JPH::ShapeRefC Shape;
			glm::vec3 Offset;
		};
		std::vector<ShapePart> parts;

		auto addShape = [&](const JPH::ShapeSettings& settings, const glm::vec3& offset)
		{
			JPH::ShapeSettings::ShapeResult result = settings.Create();
			if (result.HasError())
			{
				BW_CORE_ERROR("Physics: failed to create a shape for '{}': {}", entity.GetName(), result.GetError().c_str());
				return;
			}
			parts.push_back({ result.Get(), offset * scale });
		};

		if (auto* box = entity.TryGetComponent<BoxColliderComponent>())
		{
			const glm::vec3 halfExtents = glm::max(box->HalfExtents * scale, glm::vec3(0.001f));
			const float convexRadius = std::min(0.05f, std::min({ halfExtents.x, halfExtents.y, halfExtents.z }) * 0.5f);
			addShape(JPH::BoxShapeSettings(ToJolt(halfExtents), convexRadius), box->Offset);
		}
		if (auto* sphere = entity.TryGetComponent<SphereColliderComponent>())
		{
			const float radius = std::max(sphere->Radius * std::max({ scale.x, scale.y, scale.z }), 0.001f);
			addShape(JPH::SphereShapeSettings(radius), sphere->Offset);
		}
		if (auto* capsule = entity.TryGetComponent<CapsuleColliderComponent>())
		{
			const float radius = std::max(capsule->Radius * std::max(scale.x, scale.z), 0.001f);
			const float halfHeight = std::max(capsule->HalfHeight * scale.y, 0.001f);
			addShape(JPH::CapsuleShapeSettings(halfHeight, radius), capsule->Offset);
		}
		if (entity.HasComponent<MeshColliderComponent>())
		{
			if (rigidBody.Type != RigidBodyType::Static)
			{
				BW_CORE_WARN("Physics: mesh colliders only work on static bodies ('{}')", entity.GetName());
			}
			else if (auto* meshRenderer = entity.TryGetComponent<MeshRendererComponent>(); meshRenderer && meshRenderer->RuntimeMesh)
			{
				const Ref<Mesh>& mesh = meshRenderer->RuntimeMesh;
				JPH::VertexList vertices;
				JPH::IndexedTriangleList triangles;
				for (const Submesh& submesh : mesh->GetSubmeshes())
				{
					const glm::mat4 vertexTransform = glm::scale(glm::mat4(1.0f), scale) * submesh.Transform;
					const JPH::uint32 baseVertex = static_cast<JPH::uint32>(vertices.size());
					for (uint32_t i = 0; i < submesh.VertexCount; i++)
					{
						const glm::vec3 position = glm::vec3(vertexTransform * glm::vec4(mesh->GetVertices()[submesh.BaseVertex + i].Position, 1.0f));
						vertices.push_back(JPH::Float3(position.x, position.y, position.z));
					}
					for (uint32_t i = 0; i + 2 < submesh.IndexCount; i += 3)
					{
						const auto& indices = mesh->GetIndices();
						triangles.push_back(JPH::IndexedTriangle(baseVertex + indices[submesh.BaseIndex + i],
							baseVertex + indices[submesh.BaseIndex + i + 1], baseVertex + indices[submesh.BaseIndex + i + 2]));
					}
				}
				if (!triangles.empty())
					addShape(JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)), glm::vec3(0.0f));
			}
		}

		if (parts.empty())
		{
			BW_CORE_WARN("Physics: '{}' has a RigidBody but no collider; it will not simulate", entity.GetName());
			return;
		}

		JPH::ShapeRefC shape;
		if (parts.size() == 1)
		{
			if (glm::length(parts[0].Offset) > 1e-6f)
				shape = JPH::RotatedTranslatedShapeSettings(ToJolt(parts[0].Offset), JPH::Quat::sIdentity(), parts[0].Shape).Create().Get();
			else
				shape = parts[0].Shape;
		}
		else
		{
			JPH::StaticCompoundShapeSettings compound;
			for (const ShapePart& part : parts)
				compound.AddShape(ToJolt(part.Offset), JPH::Quat::sIdentity(), part.Shape);
			JPH::ShapeSettings::ShapeResult result = compound.Create();
			if (result.HasError())
			{
				BW_CORE_ERROR("Physics: failed to build compound shape for '{}': {}", entity.GetName(), result.GetError().c_str());
				return;
			}
			shape = result.Get();
		}

		JPH::EMotionType motionType = JPH::EMotionType::Dynamic;
		if (rigidBody.Type == RigidBodyType::Static)
			motionType = JPH::EMotionType::Static;
		else if (rigidBody.Type == RigidBodyType::Kinematic)
			motionType = JPH::EMotionType::Kinematic;

		const JPH::ObjectLayer layer = rigidBody.Type == RigidBodyType::Static ? Layers::NonMoving : Layers::Moving;
		JPH::BodyCreationSettings settings(shape, JPH::RVec3(translation.x, translation.y, translation.z), ToJolt(rotation), motionType, layer);
		settings.mFriction = rigidBody.Friction;
		settings.mRestitution = rigidBody.Restitution;
		settings.mLinearDamping = rigidBody.LinearDamping;
		settings.mAngularDamping = rigidBody.AngularDamping;
		settings.mGravityFactor = rigidBody.GravityScale;
		settings.mIsSensor = rigidBody.IsTrigger;
		settings.mUserData = static_cast<uint64_t>(entity.GetUUID());
		if (rigidBody.IsTrigger && motionType != JPH::EMotionType::Dynamic)
			settings.mCollideKinematicVsNonDynamic = true;
		if (rigidBody.ContinuousCollision)
			settings.mMotionQuality = JPH::EMotionQuality::LinearCast;

		if (motionType == JPH::EMotionType::Dynamic)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = std::max(rigidBody.Mass, 0.001f);

			JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::All;
			if (rigidBody.LockRotationX) dofs &= ~JPH::EAllowedDOFs::RotationX;
			if (rigidBody.LockRotationY) dofs &= ~JPH::EAllowedDOFs::RotationY;
			if (rigidBody.LockRotationZ) dofs &= ~JPH::EAllowedDOFs::RotationZ;
			settings.mAllowedDOFs = dofs;
		}

		JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
		const JPH::BodyID bodyID = bodies.CreateAndAddBody(settings,
			motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		if (bodyID.IsInvalid())
		{
			BW_CORE_ERROR("Physics: body limit reached; '{}' was not added", entity.GetName());
			return;
		}

		rigidBody.RuntimeBodyID = bodyID.GetIndexAndSequenceNumber();
		m_Jolt->Bodies[entity.GetUUID()] = bodyID;
		if (motionType == JPH::EMotionType::Static)
			m_Jolt->LastStaticTransforms[entity.GetUUID()] = m_Scene.GetWorldTransform(entity);
	}

	void PhysicsWorld::DestroyBody(Entity entity)
	{
		auto it = m_Jolt->Bodies.find(entity.GetUUID());
		if (it == m_Jolt->Bodies.end())
			return;

		JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
		m_Jolt->Contacts.ForgetBody(it->second);
		bodies.RemoveBody(it->second);
		bodies.DestroyBody(it->second);
		m_Jolt->LastStaticTransforms.erase(it->first);
		m_Jolt->Bodies.erase(it);

		if (auto* rigidBody = entity.TryGetComponent<RigidBodyComponent>())
			rigidBody->RuntimeBodyID = 0xFFFFFFFF;
	}

	void PhysicsWorld::Step(float timestep)
	{
		JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();

		// Script-driven motion of kinematic and static bodies.
		for (const auto& [uuid, bodyID] : m_Jolt->Bodies)
		{
			Entity entity = m_Scene.TryGetEntityByUUID(uuid);
			if (!entity)
				continue;
			const RigidBodyComponent& rigidBody = entity.GetComponent<RigidBodyComponent>();
			if (rigidBody.Type == RigidBodyType::Dynamic)
				continue;

			const glm::mat4 worldTransform = m_Scene.GetWorldTransform(entity);
			glm::vec3 translation(0.0f), scale(1.0f);
			glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
			if (!Math::DecomposeTransform(worldTransform, translation, rotation, scale))
				continue;

			if (rigidBody.Type == RigidBodyType::Kinematic)
			{
				bodies.MoveKinematic(bodyID, JPH::RVec3(translation.x, translation.y, translation.z), ToJolt(rotation), timestep);
			}
			else
			{
				glm::mat4& last = m_Jolt->LastStaticTransforms[uuid];
				if (last != worldTransform)
				{
					bodies.SetPositionAndRotation(bodyID, JPH::RVec3(translation.x, translation.y, translation.z), ToJolt(rotation), JPH::EActivation::DontActivate);
					last = worldTransform;
				}
			}
		}

		const JPH::EPhysicsUpdateError error = m_Jolt->System.Update(timestep, 1, m_Jolt->TempAllocator.get(), m_Jolt->JobSystem.get());
		if (error != JPH::EPhysicsUpdateError::None)
			BW_CORE_WARN("Physics: update reported error flags {}", static_cast<uint32_t>(error));

		// Write simulated transforms back to the entities.
		for (const auto& [uuid, bodyID] : m_Jolt->Bodies)
		{
			Entity entity = m_Scene.TryGetEntityByUUID(uuid);
			if (!entity || entity.GetComponent<RigidBodyComponent>().Type != RigidBodyType::Dynamic)
				continue;
			if (!bodies.IsActive(bodyID))
				continue;

			JPH::RVec3 position;
			JPH::Quat rotation;
			bodies.GetPositionAndRotation(bodyID, position, rotation);

			glm::vec3 currentTranslation(0.0f), scale(1.0f);
			glm::quat currentRotation(1.0f, 0.0f, 0.0f, 0.0f);
			Math::DecomposeTransform(m_Scene.GetWorldTransform(entity), currentTranslation, currentRotation, scale);
			const glm::mat4 world = Math::ComposeTransform(glm::vec3(position.GetX(), position.GetY(), position.GetZ()), FromJolt(rotation), scale);
			m_Scene.SetWorldTransform(entity, world);
		}

		// Translate body pairs into entity events.
		m_ContactEvents.clear();
		for (const ContactListener::PairEvent& pairEvent : m_Jolt->Contacts.TakeEvents())
		{
			ContactEvent event;
			if (pairEvent.IsTrigger)
				event.Type = pairEvent.Begin ? ContactEventType::TriggerEnter : ContactEventType::TriggerExit;
			else
				event.Type = pairEvent.Begin ? ContactEventType::CollisionEnter : ContactEventType::CollisionExit;
			event.EntityA = UUID(bodies.GetUserData(pairEvent.BodyA));
			event.EntityB = UUID(bodies.GetUserData(pairEvent.BodyB));
			if (event.EntityA.IsValid() && event.EntityB.IsValid())
				m_ContactEvents.push_back(event);
		}
	}

	namespace {

		class StaticBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::BroadPhaseLayer layer) const override { return layer == BroadPhaseLayers::NonMoving; }
		};

		class StaticObjectFilter final : public JPH::ObjectLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer) const override { return layer == Layers::NonMoving; }
		};

		class SolidBodyFilter final : public JPH::BodyFilter
		{
		public:
			explicit SolidBodyFilter(JPH::BodyID ignore) : m_Ignore(ignore) {}
			bool ShouldCollide(const JPH::BodyID& body) const override { return body != m_Ignore; }
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
		private:
			JPH::BodyID m_Ignore;
		};

	}

	bool PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit, UUID ignore) const
	{
		return CastRay(origin, direction, maxDistance, false, ignore, outHit);
	}

	bool PhysicsWorld::RaycastStatic(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& outHit, UUID ignore) const
	{
		return CastRay(origin, direction, maxDistance, true, ignore, outHit);
	}

	bool PhysicsWorld::CastRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, bool staticOnly, UUID ignore, RaycastHit& outHit) const
	{
		const float length = glm::length(direction);
		if (length < 1e-6f || maxDistance <= 0.0f)
			return false;
		const glm::vec3 normalizedDirection = direction / length;

		JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), ToJolt(normalizedDirection * maxDistance));
		JPH::RayCastResult result;
		JPH::BodyID ignoreBody;
		if (auto it = m_Jolt->Bodies.find(ignore); ignore != 0 && it != m_Jolt->Bodies.end())
			ignoreBody = it->second;
		const JPH::IgnoreSingleBodyFilter skipIgnored(ignoreBody);
		const bool hit = staticOnly
			? m_Jolt->System.GetNarrowPhaseQuery().CastRay(ray, result, StaticBroadPhaseFilter(), StaticObjectFilter(), SolidBodyFilter(ignoreBody))
			: m_Jolt->System.GetNarrowPhaseQuery().CastRay(ray, result, JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), skipIgnored);
		if (!hit)
			return false;

		const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
		outHit.Point = glm::vec3(point.GetX(), point.GetY(), point.GetZ());
		outHit.Distance = result.mFraction * maxDistance;
		outHit.Entity = UUID(m_Jolt->System.GetBodyInterface().GetUserData(result.mBodyID));

		JPH::BodyLockRead lock(m_Jolt->System.GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
			outHit.Normal = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
		return true;
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		m_Jolt->System.SetGravity(ToJolt(gravity));
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return FromJolt(m_Jolt->System.GetGravity());
	}

	bool PhysicsWorld::HasBody(Entity entity) const
	{
		return entity && m_Jolt->Bodies.contains(entity.GetUUID());
	}

	uint32_t PhysicsWorld::GetBodyCount() const
	{
		return static_cast<uint32_t>(m_Jolt->Bodies.size());
	}

	void PhysicsWorld::ApplyBodySettings(Entity entity)
	{
		auto it = m_Jolt->Bodies.find(entity.GetUUID());
		if (it == m_Jolt->Bodies.end() || !entity.HasComponent<RigidBodyComponent>())
			return;
		const RigidBodyComponent& rigidBody = entity.GetComponent<RigidBodyComponent>();
		JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
		bodies.SetFriction(it->second, rigidBody.Friction);
		bodies.SetRestitution(it->second, rigidBody.Restitution);
		bodies.SetGravityFactor(it->second, rigidBody.GravityScale);

		JPH::BodyLockWrite lock(m_Jolt->System.GetBodyLockInterface(), it->second);
		if (lock.Succeeded() && lock.GetBody().GetMotionProperties())
		{
			lock.GetBody().GetMotionProperties()->SetLinearDamping(rigidBody.LinearDamping);
			lock.GetBody().GetMotionProperties()->SetAngularDamping(rigidBody.AngularDamping);
		}
	}

	void PhysicsWorld::AddForce(Entity entity, const glm::vec3& force)
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
			m_Jolt->System.GetBodyInterface().AddForce(it->second, ToJolt(force));
	}

	void PhysicsWorld::AddImpulse(Entity entity, const glm::vec3& impulse)
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
			m_Jolt->System.GetBodyInterface().AddImpulse(it->second, ToJolt(impulse));
	}

	void PhysicsWorld::AddTorque(Entity entity, const glm::vec3& torque)
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
			m_Jolt->System.GetBodyInterface().AddTorque(it->second, ToJolt(torque));
	}

	void PhysicsWorld::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
		{
			JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
			bodies.SetLinearVelocity(it->second, ToJolt(velocity));
			bodies.ActivateBody(it->second);
		}
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
			return FromJolt(m_Jolt->System.GetBodyInterface().GetLinearVelocity(it->second));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
		{
			JPH::BodyInterface& bodies = m_Jolt->System.GetBodyInterface();
			bodies.SetAngularVelocity(it->second, ToJolt(velocity));
			bodies.ActivateBody(it->second);
		}
	}

	glm::vec3 PhysicsWorld::GetAngularVelocity(Entity entity) const
	{
		if (auto it = m_Jolt->Bodies.find(entity.GetUUID()); it != m_Jolt->Bodies.end())
			return FromJolt(m_Jolt->System.GetBodyInterface().GetAngularVelocity(it->second));
		return glm::vec3(0.0f);
	}

	void PhysicsWorld::TeleportToEntity(Entity entity)
	{
		auto it = m_Jolt->Bodies.find(entity.GetUUID());
		if (it == m_Jolt->Bodies.end())
			return;

		glm::vec3 translation(0.0f), scale(1.0f);
		glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
		if (!Math::DecomposeTransform(m_Scene.GetWorldTransform(entity), translation, rotation, scale))
			return;
		m_Jolt->System.GetBodyInterface().SetPositionAndRotation(it->second, JPH::RVec3(translation.x, translation.y, translation.z),
			ToJolt(rotation), JPH::EActivation::Activate);
	}

}
