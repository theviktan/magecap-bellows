#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Math/AABB.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;

namespace Bellows {

	// The agents a navmesh is baked for. A wider or taller agent than this can still walk it, but may
	// brush walls or ceilings.
	struct NavMeshSettings
	{
		float AgentRadius = 0.4f;		// Meters kept from walls
		float AgentHeight = 1.8f;		// Meters of headroom needed
		float MaxClimb = 0.4f;			// Highest step walked up, meters
		float MaxSlope = 45.0f;			// Steepest ground walked, degrees

		static constexpr float MinRadius = 0.05f, MaxRadius = 5.0f;
		static constexpr float MinHeight = 0.1f, MaxHeight = 20.0f;
		static constexpr float MaxClimbLimit = 5.0f;
		static constexpr float MaxSlopeLimit = 89.0f;

		// The same settings, every value held to its range (and a NaN to the default).
		NavMeshSettings Clamped() const;
		bool operator==(const NavMeshSettings&) const = default;
	};

	// Triangles to bake from, in world space.
	struct NavMeshGeometry
	{
		std::vector<glm::vec3> Vertices;
		std::vector<uint32_t> Indices;	// Three per triangle
		AABB Bounds;

		void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
		size_t GetTriangleCount() const { return Indices.size() / 3; }
		// Changes when any triangle does: a baked navmesh with another hash is out of date.
		uint64_t ComputeHash(const NavMeshSettings& settings) const;
	};

	// A walkable surface for agents, baked with Recast and searched with Detour. It's built in square
	// tiles, so a big world is baked piece by piece.
	class NavMesh
	{
	public:
		static constexpr float TileSize = 32.0f;				// Meters
		static constexpr int MaxTiles = 16384;					// 4096 x 4096 m in all (any shape)
		static constexpr size_t MaxTriangles = 4'000'000;
		static constexpr size_t MaxPathCorners = 256;
		static constexpr unsigned short WalkableFlag = 1;		// Detour flags of polygons agents walk on

		~NavMesh();
		NavMesh(const NavMesh&) = delete;
		NavMesh& operator=(const NavMesh&) = delete;

		// Bakes 'geometry'. Null, with the reason in 'error', when nothing could be baked.
		static Scope<NavMesh> Build(const NavMeshGeometry& geometry, const NavMeshSettings& settings, std::string& error);
		// Reads what Save wrote. Null, with the reason in 'error', for a file that isn't a whole, valid navmesh.
		static Scope<NavMesh> Load(const std::vector<uint8_t>& bytes, std::string& error);
		std::vector<uint8_t> Save() const;

		const NavMeshSettings& GetSettings() const { return m_Settings; }
		// The NavMeshGeometry::ComputeHash of what it was baked from.
		uint64_t GetSourceHash() const { return m_SourceHash; }
		int GetTileCount() const;
		int GetPolygonCount() const;
		// The height of its voxel cells, meters: its surface lies up to this far above the ground it was baked from.
		float GetCellHeight() const;

		// The corners of the shortest path from start to end, both ends included; empty when either end
		// is off the navmesh or nothing connects them. When the end can't be reached, the path goes as
		// close as it can.
		std::vector<glm::vec3> FindPath(const glm::vec3& start, const glm::vec3& end) const;
		// The nearest point on the navmesh within 'searchDistance' meters (sideways; agents' height up
		// and down), if any.
		std::optional<glm::vec3> FindNearestPoint(const glm::vec3& point, float searchDistance) const;
		// True when an agent can stand at 'point': over the navmesh, within a step of its surface.
		bool IsNavigable(const glm::vec3& point) const;

		// Every walkable triangle, three corners each, for drawing the navmesh.
		void GetTriangles(std::vector<glm::vec3>& outCorners) const;

		dtNavMesh* GetDetourNavMesh() const { return m_NavMesh; }
	private:
		NavMesh() = default;
		bool Init(const glm::vec3& origin, int tilesX, int tilesZ, std::string& error);
	private:
		NavMeshSettings m_Settings;
		uint64_t m_SourceHash = 0;
		glm::vec3 m_Origin { 0.0f };
		int m_TilesX = 0;
		int m_TilesZ = 0;
		dtNavMesh* m_NavMesh = nullptr;
		dtNavMeshQuery* m_Query = nullptr;
	};

}
