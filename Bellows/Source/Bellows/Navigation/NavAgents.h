#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/UUID.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class dtCrowd;

namespace Bellows {

	class NavMesh;
	class Scene;

	// An agent that stopped at the end of its path in an update.
	struct NavAgentArrival
	{
		UUID Entity = 0;
		bool Reached = true;	// False: the destination can't be reached, and it stopped as close as it could
	};

	// Moves a running scene's NavAgent entities with Detour's crowd: each follows its path, slows down
	// to arrive, and steers around walls and other agents. It's play-time state only, built for the
	// scene's navmesh and rebuilt when that changes (a bake while the game runs, a scene change).
	class SceneNavAgents
	{
	public:
		static constexpr int MaxAgents = 1024;
		static constexpr float MaxStep = 1.0f / 20.0f;	// Seconds per crowd step; a slow frame takes several
		static constexpr int MaxStepsPerUpdate = 4;
		static constexpr float PlacementDistance = 2.0f;	// An agent this far from the navmesh (sideways) is put on it

		SceneNavAgents() = default;
		~SceneNavAgents();
		SceneNavAgents(const SceneNavAgents&) = delete;
		SceneNavAgents& operator=(const SceneNavAgents&) = delete;

		// Moves every agent on by 'seconds' and writes where each got to into its transform (and its
		// RuntimeVelocity). Destinations set since the last update are used first; an agent that
		// arrives (or, when its destination can't be reached, gets as close as it can) loses its
		// destination and stands. Returns those agents, in the scene's order.
		std::vector<NavAgentArrival> Update(Scene& scene, float seconds);

		// Agents on the navmesh being moved.
		int GetAgentCount() const { return static_cast<int>(m_Agents.size()); }
	private:
		struct Agent
		{
			int Index = -1;					// In the crowd
			glm::vec3 LastPosition { 0.0f };	// The world position the last update wrote
			bool NearGoal = false;			// Within its slowing-down distance of its destination
		};

		void Reset(const Ref<NavMesh>& navMesh);
		void Remove(UUID entity);
		void WarnOnce(UUID entity, const char* message, const std::string& name);
	private:
		Ref<NavMesh> m_NavMesh;
		dtCrowd* m_Crowd = nullptr;
		std::unordered_map<UUID, Agent> m_Agents;
		std::unordered_set<UUID> m_Warned;			// Entities already warned about, so the log isn't flooded
		bool m_WarnedNoNavMesh = false;
	};

}
