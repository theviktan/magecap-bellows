#include "bwpch.h"
#include "Bellows/Navigation/NavAgents.h"

#include "Bellows/Math/Math.h"
#include "Bellows/Navigation/NavMesh.h"
#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <DetourCrowd.h>
#include <DetourNavMeshQuery.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Bellows {

	namespace {

		// A teleport: a script or the editor moved the entity further than this since the last update.
		constexpr float c_TeleportDistance = 0.001f;
		// Slower than this (m/s, sideways), an agent keeps facing where it faces.
		constexpr float c_TurnMinSpeed = 0.05f;

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		// 'nearGoal': within its slowing-down distance of where it's going. Separation pushes agents
		// apart over their whole query range, while the pull toward a goal fades as an agent slows to
		// arrive, so near the goal a bystander could hold it short forever; there only avoidance steers.
		dtCrowdAgentParams MakeParams(const NavAgentComponent& agent, bool nearGoal)
		{
			dtCrowdAgentParams params {};
			params.radius = agent.Radius;
			params.height = agent.Height;
			params.maxAcceleration = agent.Acceleration;
			params.maxSpeed = agent.Speed;
			// Detour's own sample uses these multiples of the radius: far enough to see what's coming,
			// near enough to stay cheap in a crowd.
			params.collisionQueryRange = agent.Radius * 12.0f;
			params.pathOptimizationRange = agent.Radius * 30.0f;
			params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
			if (agent.AvoidOthers)
			{
				params.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
				if (!nearGoal)
				{
					params.updateFlags |= DT_CROWD_SEPARATION;
					params.separationWeight = 2.0f;
				}
			}
			params.obstacleAvoidanceType = 0;
			params.queryFilterType = 0;
			return params;
		}

		// Turns 'rotation' about world up, at most 'maxRadians', so its forward (-Z) heads along 'velocity'.
		glm::quat TurnToward(const glm::quat& rotation, const glm::vec3& velocity, float maxRadians)
		{
			const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
			if (forward.x * forward.x + forward.z * forward.z < 1e-8f)
				return rotation;	// Facing straight up or down: no heading to turn
			const float current = std::atan2(-forward.x, -forward.z);
			const float target = std::atan2(-velocity.x, -velocity.z);
			float delta = std::remainder(target - current, glm::two_pi<float>());
			delta = std::clamp(delta, -maxRadians, maxRadians);
			return glm::angleAxis(delta, glm::vec3(0.0f, 1.0f, 0.0f)) * rotation;
		}

	}

	SceneNavAgents::~SceneNavAgents()
	{
		dtFreeCrowd(m_Crowd);
	}

	void SceneNavAgents::Reset(const Ref<NavMesh>& navMesh)
	{
		dtFreeCrowd(m_Crowd);
		m_Crowd = nullptr;
		m_Agents.clear();
		m_NavMesh = navMesh;
		if (!navMesh)
			return;

		m_Crowd = dtAllocCrowd();
		if (!m_Crowd || !m_Crowd->init(MaxAgents, NavAgentComponent::MaxRadius, navMesh->GetDetourNavMesh()))
		{
			BW_CORE_ERROR("Navigation: couldn't start moving agents on the navmesh (out of memory)");
			dtFreeCrowd(m_Crowd);
			m_Crowd = nullptr;
			return;
		}
		m_Crowd->getEditableFilter(0)->setIncludeFlags(NavMesh::WalkableFlag);
	}

	void SceneNavAgents::Remove(UUID entity)
	{
		auto it = m_Agents.find(entity);
		if (it == m_Agents.end())
			return;
		if (m_Crowd)
			m_Crowd->removeAgent(it->second.Index);
		m_Agents.erase(it);
	}

	void SceneNavAgents::WarnOnce(UUID entity, const char* message, const std::string& name)
	{
		if (m_Warned.insert(entity).second)
			BW_CORE_WARN("Navigation: '{}' {}", name, message);
	}

	std::vector<NavAgentArrival> SceneNavAgents::Update(Scene& scene, float seconds)
	{
		std::vector<NavAgentArrival> arrivals;

		// A new navmesh (baked while the game runs, or a new scene's) gets a new crowd. The agents join
		// it below, and walk on toward the destinations they had.
		if (scene.GetNavMesh() != m_NavMesh)
		{
			Reset(scene.GetNavMesh());
			for (entt::entity handle : scene.GetAllEntitiesWith<NavAgentComponent>())
			{
				NavAgentComponent& agent = scene.GetRegistry().get<NavAgentComponent>(handle);
				agent.RuntimeDestinationChanged = agent.RuntimeDestination.has_value();
			}
		}

		// Agents whose entity, or whose NavAgent, is gone.
		std::vector<UUID> gone;
		for (const auto& [uuid, agent] : m_Agents)
		{
			Entity entity = scene.TryGetEntityByUUID(uuid);
			if (!entity || !entity.HasComponent<NavAgentComponent>() || scene.IsPendingDestroy(entity))
				gone.push_back(uuid);
		}
		for (UUID uuid : gone)
			Remove(uuid);

		auto view = scene.GetAllEntitiesWith<NavAgentComponent>();
		if (!m_Crowd)
		{
			if (!view.empty() && !m_WarnedNoNavMesh)
			{
				BW_CORE_WARN("Navigation: this scene has NavAgents but no navmesh to walk on: bake one in Scene Settings > Navigation");
				m_WarnedNoNavMesh = true;
			}
			for (entt::entity handle : view)
				view.get<NavAgentComponent>(handle).RuntimeVelocity = glm::vec3(0.0f);
			return arrivals;
		}

		const dtNavMeshQuery* query = m_Crowd->getNavMeshQuery();
		const dtQueryFilter* filter = m_Crowd->getFilter(0);
		auto findNearest = [&](const glm::vec3& point, float height, dtPolyRef& outRef, glm::vec3& outPoint)
		{
			const float extents[3] = { PlacementDistance, std::max(PlacementDistance, height), PlacementDistance };
			outRef = 0;
			return dtStatusSucceed(query->findNearestPoly(&point.x, extents, filter, &outRef, &outPoint.x)) && outRef != 0;
		};

		// Join, follow teleports, take new settings and destinations.
		std::vector<Entity> moving;
		for (entt::entity handle : view)
		{
			Entity entity(handle, &scene);
			if (scene.IsPendingDestroy(entity))
				continue;
			NavAgentComponent& agent = view.get<NavAgentComponent>(handle);
			agent.Clamp();
			agent.RuntimeVelocity = glm::vec3(0.0f);
			const UUID uuid = entity.GetUUID();

			const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>();
			if (body && body->Type == RigidBodyType::Dynamic)
			{
				Remove(uuid);
				WarnOnce(uuid, "has a NavAgent on a dynamic rigid body, so it stands still: make the body Kinematic (or remove it) to let the agent walk", entity.GetName());
				continue;
			}

			const glm::vec3 position = scene.GetWorldPosition(entity);
			if (!IsFinite(position))
			{
				Remove(uuid);
				continue;
			}
			const glm::vec3 feet = position - glm::vec3(0.0f, agent.BaseOffset, 0.0f);

			auto it = m_Agents.find(uuid);
			if (it != m_Agents.end() && glm::distance(position, it->second.LastPosition) > c_TeleportDistance)
			{
				// Moved by something else: it rejoins where it was put, still heading where it was going.
				Remove(uuid);
				it = m_Agents.end();
				agent.RuntimeDestinationChanged |= agent.RuntimeDestination.has_value();
			}
			if (it == m_Agents.end())
			{
				dtPolyRef ref = 0;
				glm::vec3 start(0.0f);
				if (!findNearest(feet, agent.Height, ref, start))
				{
					WarnOnce(uuid, "is too far from the navmesh to walk on it", entity.GetName());
					continue;
				}
				const dtCrowdAgentParams params = MakeParams(agent, false);
				const int index = m_Crowd->addAgent(&start.x, &params);
				if (index < 0)
				{
					WarnOnce(uuid, "can't walk: the scene already has the most NavAgents (1024) moving", entity.GetName());
					continue;
				}
				m_Warned.erase(uuid);
				it = m_Agents.emplace(uuid, Agent { index, position }).first;
				agent.RuntimeDestinationChanged |= agent.RuntimeDestination.has_value();
			}
			else
			{
				const dtCrowdAgentParams params = MakeParams(agent, it->second.NearGoal && agent.RuntimeDestination.has_value());
				m_Crowd->updateAgentParameters(it->second.Index, &params);
			}

			if (agent.RuntimeDestinationChanged)
			{
				agent.RuntimeDestinationChanged = false;
				dtPolyRef ref = 0;
				glm::vec3 target(0.0f);
				if (agent.RuntimeDestination && IsFinite(*agent.RuntimeDestination) && findNearest(*agent.RuntimeDestination, agent.Height, ref, target))
				{
					m_Crowd->requestMoveTarget(it->second.Index, ref, &target.x);
				}
				else
				{
					// Stopped, or sent somewhere no agent can stand: it stands where it is.
					agent.RuntimeDestination.reset();
					m_Crowd->resetMoveTarget(it->second.Index);
				}
			}
			moving.push_back(entity);
		}

		// Small steps, so a slow frame doesn't send agents through walls.
		if (std::isfinite(seconds) && seconds > 0.0f)
		{
			const int steps = std::min(MaxStepsPerUpdate, static_cast<int>(std::ceil(seconds / MaxStep)));
			const float step = std::min(seconds / static_cast<float>(steps), MaxStep);
			for (int i = 0; i < steps; i++)
				m_Crowd->update(step, nullptr);
		}

		// Where each got to. The navmesh keeps heights in whole cells, so it lies up to a cell above the
		// colliders it was baked from (and its detail up to another cell off): the ground found there is
		// where the agent stands.
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		const float cell = m_NavMesh->GetCellHeight();
		for (Entity entity : moving)
		{
			NavAgentComponent& agent = entity.GetComponent<NavAgentComponent>();
			Agent& state = m_Agents.at(entity.GetUUID());
			const dtCrowdAgent* crowdAgent = m_Crowd->getAgent(state.Index);
			const glm::vec3 feet(crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2]);
			const glm::vec3 velocity(crowdAgent->vel[0], crowdAgent->vel[1], crowdAgent->vel[2]);
			if (!IsFinite(feet) || !IsFinite(velocity))
				continue;
			agent.RuntimeVelocity = velocity;

			glm::vec3 translation(0.0f), scale(1.0f);
			glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
			if (Math::DecomposeTransform(scene.GetWorldTransform(entity), translation, rotation, scale))
			{
				const float sidewaysSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
				if (agent.TurnSpeed > 0.0f && sidewaysSpeed > c_TurnMinSpeed && std::isfinite(seconds) && seconds > 0.0f)
					rotation = TurnToward(rotation, velocity, glm::radians(agent.TurnSpeed) * seconds);
				glm::vec3 ground = feet;
				RaycastHit hit;
				if (physics && physics->RaycastStatic(feet + glm::vec3(0.0f, cell, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), cell * 3.0f, hit, entity.GetUUID()))
					ground.y = hit.Point.y;
				translation = ground + glm::vec3(0.0f, agent.BaseOffset, 0.0f);
				scene.SetWorldTransform(entity, glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale));
			}
			state.LastPosition = scene.GetWorldPosition(entity);

			// Arrived: at the end of a path that's found, within the stopping distance (sideways, and
			// within its height up or down). Detour keeps replanning a path that can't reach its goal,
			// so a replan doesn't stop an arrival: the path's end stays where the agent is going.
			state.NearGoal = false;
			if (agent.RuntimeDestination && crowdAgent->targetState == DT_CROWDAGENT_TARGET_VALID)
			{
				const float* end = crowdAgent->corridor.getTarget();
				const glm::vec2 offset(end[0] - feet.x, end[2] - feet.z);
				state.NearGoal = glm::length(offset) <= agent.Radius * 2.0f + agent.StoppingDistance;
				if (glm::length(offset) <= agent.StoppingDistance && std::abs(end[1] - feet.y) <= agent.Height)
				{
					arrivals.push_back({ entity.GetUUID(), !crowdAgent->partial });
					agent.RuntimeDestination.reset();
					m_Crowd->resetMoveTarget(state.Index);
				}
			}
			else if (agent.RuntimeDestination && crowdAgent->targetState == DT_CROWDAGENT_TARGET_FAILED)
			{
				// No path at all: it stays where it is, as close as it can get, and scripts waiting hear so.
				arrivals.push_back({ entity.GetUUID(), false });
				agent.RuntimeDestination.reset();
				m_Crowd->resetMoveTarget(state.Index);
			}
		}
		return arrivals;
	}

}
