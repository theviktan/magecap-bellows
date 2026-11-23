#include "bwpch.h"
#include "Bellows/Navigation/NavMesh.h"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <climits>
#include <cstring>

namespace Bellows {

	namespace {

		constexpr char c_Magic[4] = { 'B', 'N', 'A', 'V' };
		constexpr uint32_t c_FileVersion = 1;
		constexpr size_t c_MaxTileBytes = 64 * 1024 * 1024;
		constexpr int c_MaxCount = 1 << 20;		// Of any one thing in a tile
		constexpr unsigned short c_WalkableFlag = NavMesh::WalkableFlag;
		constexpr int c_QueryNodes = 4096;
		// Coordinates Detour's distance math stays finite with (its squared distances stay below FLT_MAX).
		constexpr float c_MaxCoordinate = 1e7f;

		// What a .bnav file starts with (little-endian, as every platform Bellows runs on is).
		struct FileHeader
		{
			char Magic[4];
			uint32_t Version;
			float AgentRadius, AgentHeight, MaxClimb, MaxSlope;
			float Origin[3];
			int32_t TilesX, TilesZ;
			uint32_t TileCount;
			uint64_t SourceHash;
		};
		static_assert(sizeof(FileHeader) == 56, "FileHeader is written as it is in memory, so it has no padding");

		float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }

		// The size of each cell Recast samples the world in: fine enough for the agent's radius, coarse
		// enough that a 32 m tile stays a few hundred cells across.
		float CellSize(const NavMeshSettings& settings) { return std::clamp(settings.AgentRadius / 2.0f, 0.05f, 0.5f); }
		float CellHeight(const NavMeshSettings& settings) { return std::clamp(CellSize(settings) / 2.0f, 0.025f, 0.25f); }

		int TileBits(int tiles) { return std::min(static_cast<int>(dtIlog2(dtNextPow2(static_cast<unsigned int>(std::max(tiles, 1))))), 14); }

		size_t Align4(size_t size) { return (size + 3) & ~size_t(3); }

		bool AllFinite(const float* values, size_t count)
		{
			for (size_t i = 0; i < count; i++)
			{
				if (!std::isfinite(values[i]))
					return false;
			}
			return true;
		}

		bool AllWithinWorld(const float* values, size_t count)
		{
			for (size_t i = 0; i < count; i++)
			{
				if (!(std::abs(values[i]) <= c_MaxCoordinate))
					return false;
			}
			return true;
		}

		// Detour reads a tile's arrays by the counts in its header and trusts every index in them, so a
		// tile from a file is checked through before Detour sees it. Only what Build makes is accepted:
		// one layer, ground polygons, no off-mesh links.
		bool CheckTile(const unsigned char* data, size_t size, int tilesX, int tilesZ, int polyBits, std::string& error)
		{
			if (size < sizeof(dtMeshHeader))
			{
				error = "a tile is cut short";
				return false;
			}
			dtMeshHeader header;
			std::memcpy(&header, data, sizeof(header));
			if (header.magic != DT_NAVMESH_MAGIC || header.version != DT_NAVMESH_VERSION)
			{
				error = "a tile isn't navmesh data of this version";
				return false;
			}
			const auto inRange = [](int value, int min, int max) { return value >= min && value <= max; };
			if (!inRange(header.x, 0, tilesX - 1) || !inRange(header.y, 0, tilesZ - 1) || header.layer != 0
				|| !inRange(header.polyCount, 1, std::min(c_MaxCount, 1 << polyBits)) || !inRange(header.vertCount, 3, 0xFFFE)
				|| !inRange(header.maxLinkCount, 1, c_MaxCount) || header.detailMeshCount != header.polyCount
				|| !inRange(header.detailVertCount, 0, c_MaxCount) || !inRange(header.detailTriCount, 1, c_MaxCount)
				|| !inRange(header.bvNodeCount, 0, c_MaxCount) || header.offMeshConCount != 0 || header.offMeshBase != header.polyCount)
			{
				error = "a tile's counts are out of range";
				return false;
			}
			if (!std::isfinite(header.walkableHeight) || !std::isfinite(header.walkableRadius) || !std::isfinite(header.walkableClimb)
				|| !AllWithinWorld(header.bmin, 3) || !AllWithinWorld(header.bmax, 3) || !std::isfinite(header.bvQuantFactor) || header.bvQuantFactor <= 0.0f)
			{
				error = "a tile has values that aren't numbers";
				return false;
			}
			// Detour turns positions in the tile's box into 16-bit cells for its search tree.
			for (int axis = 0; axis < 3; axis++)
			{
				const float cells = (header.bmax[axis] - header.bmin[axis]) * header.bvQuantFactor;
				if (!(cells >= 0.0f && cells <= 65535.0f))
				{
					error = "a tile's box doesn't fit its search tree";
					return false;
				}
			}

			// The same layout as dtNavMesh::addTile.
			const size_t headerSize = Align4(sizeof(dtMeshHeader));
			const size_t vertsSize = Align4(sizeof(float) * 3 * static_cast<size_t>(header.vertCount));
			const size_t polysSize = Align4(sizeof(dtPoly) * static_cast<size_t>(header.polyCount));
			const size_t linksSize = Align4(sizeof(dtLink) * static_cast<size_t>(header.maxLinkCount));
			const size_t detailMeshesSize = Align4(sizeof(dtPolyDetail) * static_cast<size_t>(header.detailMeshCount));
			const size_t detailVertsSize = Align4(sizeof(float) * 3 * static_cast<size_t>(header.detailVertCount));
			const size_t detailTrisSize = Align4(4 * static_cast<size_t>(header.detailTriCount));
			const size_t bvTreeSize = Align4(sizeof(dtBVNode) * static_cast<size_t>(header.bvNodeCount));
			if (size != headerSize + vertsSize + polysSize + linksSize + detailMeshesSize + detailVertsSize + detailTrisSize + bvTreeSize)
			{
				error = "a tile's size doesn't match its contents";
				return false;
			}

			const unsigned char* at = data + headerSize;
			const float* verts = reinterpret_cast<const float*>(at);
			at += vertsSize;
			const dtPoly* polys = reinterpret_cast<const dtPoly*>(at);
			at += polysSize + linksSize;
			const dtPolyDetail* detailMeshes = reinterpret_cast<const dtPolyDetail*>(at);
			at += detailMeshesSize;
			const float* detailVerts = reinterpret_cast<const float*>(at);
			at += detailVertsSize;
			const unsigned char* detailTris = at;
			at += detailTrisSize;
			const dtBVNode* bvTree = reinterpret_cast<const dtBVNode*>(at);

			if (!AllWithinWorld(verts, 3 * static_cast<size_t>(header.vertCount)) || !AllWithinWorld(detailVerts, 3 * static_cast<size_t>(header.detailVertCount)))
			{
				error = "a tile has positions that aren't numbers";
				return false;
			}
			for (int p = 0; p < header.polyCount; p++)
			{
				const dtPoly& poly = polys[p];
				if (poly.vertCount < 3 || poly.vertCount > DT_VERTS_PER_POLYGON || poly.getType() != DT_POLYTYPE_GROUND)
				{
					error = "a tile has a broken polygon";
					return false;
				}
				for (int v = 0; v < poly.vertCount; v++)
				{
					const unsigned short neighbor = poly.neis[v];
					const bool neighborOk = (neighbor & DT_EXT_LINK) ? (neighbor & 0xFF) <= 7 : neighbor <= header.polyCount;
					if (poly.verts[v] >= header.vertCount || !neighborOk)
					{
						error = "a polygon points past its tile's data";
						return false;
					}
				}
				const dtPolyDetail& detail = detailMeshes[p];
				if (detail.triCount == 0 || static_cast<uint64_t>(detail.vertBase) + detail.vertCount > static_cast<uint64_t>(header.detailVertCount)
					|| static_cast<uint64_t>(detail.triBase) + detail.triCount > static_cast<uint64_t>(header.detailTriCount))
				{
					error = "a polygon's detail points past its tile's data";
					return false;
				}
				// Detour's nearest-point search needs real triangles, and at least one edge on the polygon's border.
				bool hasBoundary = false;
				for (unsigned int t = 0; t < detail.triCount; t++)
				{
					const unsigned char* triangle = detailTris + (static_cast<size_t>(detail.triBase) + t) * 4;
					for (int k = 0; k < 3; k++)
					{
						if (triangle[k] >= poly.vertCount + detail.vertCount)
						{
							error = "a detail triangle points past its polygon";
							return false;
						}
					}
					if (triangle[0] == triangle[1] || triangle[1] == triangle[2] || triangle[0] == triangle[2])
					{
						error = "a detail triangle has two corners in one place";
						return false;
					}
					hasBoundary |= (triangle[3] & ((DT_DETAIL_EDGE_BOUNDARY << 0) | (DT_DETAIL_EDGE_BOUNDARY << 2) | (DT_DETAIL_EDGE_BOUNDARY << 4))) != 0;
				}
				if (!hasBoundary)
				{
					error = "a polygon's detail mesh has no edge on its border";
					return false;
				}
			}
			for (int n = 0; n < header.bvNodeCount; n++)
			{
				const int index = bvTree[n].i;
				const bool ok = index >= 0 ? index < header.polyCount : index != INT_MIN && n + static_cast<int64_t>(-index) <= header.bvNodeCount;
				if (!ok)
				{
					error = "a tile's search tree points past its data";
					return false;
				}
			}
			return true;
		}

		struct RecastDeleter
		{
			void operator()(rcHeightfield* p) const { rcFreeHeightField(p); }
			void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); }
			void operator()(rcContourSet* p) const { rcFreeContourSet(p); }
			void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); }
			void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); }
		};
		template<typename T>
		using RecastPtr = std::unique_ptr<T, RecastDeleter>;

		// Builds one tile's Detour data from the triangles that touch it; empty when nothing walkable is there.
		std::vector<unsigned char> BuildTile(const NavMeshGeometry& geometry, const std::vector<int>& triangles, const NavMeshSettings& settings,
			const glm::vec3& tileMin, const glm::vec3& tileMax, int tileX, int tileZ, int maxPolys)
		{
			rcContext context(false);
			rcConfig config {};
			config.cs = CellSize(settings);
			config.ch = CellHeight(settings);
			config.walkableSlopeAngle = settings.MaxSlope;
			config.walkableHeight = static_cast<int>(std::ceil(settings.AgentHeight / config.ch));
			config.walkableClimb = static_cast<int>(std::floor(settings.MaxClimb / config.ch));
			config.walkableRadius = static_cast<int>(std::ceil(settings.AgentRadius / config.cs));
			config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
			config.maxSimplificationError = 1.3f;
			config.minRegionArea = 8 * 8;
			config.mergeRegionArea = 20 * 20;
			config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
			config.tileSize = static_cast<int>(NavMesh::TileSize / config.cs);
			// Neighboring tiles overlap by a border, so walls near a tile's edge are seen from both sides.
			config.borderSize = config.walkableRadius + 3;
			config.width = config.tileSize + config.borderSize * 2;
			config.height = config.width;
			config.detailSampleDist = config.cs * 6.0f;
			config.detailSampleMaxError = config.ch;
			const float border = static_cast<float>(config.borderSize) * config.cs;
			config.bmin[0] = tileMin.x - border;
			config.bmin[1] = tileMin.y;
			config.bmin[2] = tileMin.z - border;
			config.bmax[0] = tileMax.x + border;
			config.bmax[1] = tileMax.y;
			config.bmax[2] = tileMax.z + border;

			RecastPtr<rcHeightfield> solid(rcAllocHeightfield());
			if (!solid || !rcCreateHeightfield(&context, *solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
				return {};

			std::vector<int> indices;
			indices.reserve(triangles.size() * 3);
			for (int triangle : triangles)
			{
				for (int k = 0; k < 3; k++)
					indices.push_back(static_cast<int>(geometry.Indices[static_cast<size_t>(triangle) * 3 + k]));
			}
			const int triangleCount = static_cast<int>(triangles.size());
			const float* vertices = &geometry.Vertices[0].x;
			const int vertexCount = static_cast<int>(geometry.Vertices.size());
			std::vector<unsigned char> areas(triangles.size(), 0);
			rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices, vertexCount, indices.data(), triangleCount, areas.data());
			if (!rcRasterizeTriangles(&context, vertices, vertexCount, indices.data(), areas.data(), triangleCount, *solid, config.walkableClimb))
				return {};
			rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *solid);
			rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
			rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);

			RecastPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
			if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid, *compact))
				return {};
			solid.reset();
			if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact)
				|| !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
				return {};

			RecastPtr<rcContourSet> contours(rcAllocContourSet());
			if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) || contours->nconts == 0)
				return {};
			RecastPtr<rcPolyMesh> polyMesh(rcAllocPolyMesh());
			if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh))
				return {};
			RecastPtr<rcPolyMeshDetail> detail(rcAllocPolyMeshDetail());
			if (!detail || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail))
				return {};
			if (polyMesh->npolys == 0 || polyMesh->nverts == 0)
				return {};
			if (polyMesh->nverts >= 0xFFFF || polyMesh->npolys > maxPolys)
			{
				BW_CORE_WARN("Navigation: tile ({}, {}) is too detailed to bake; agents can't walk there", tileX, tileZ);
				return {};
			}
			for (int p = 0; p < polyMesh->npolys; p++)
			{
				polyMesh->flags[p] = polyMesh->areas[p] == RC_WALKABLE_AREA ? c_WalkableFlag : 0;
				polyMesh->areas[p] = 0;
			}

			dtNavMeshCreateParams params {};
			params.verts = polyMesh->verts;
			params.vertCount = polyMesh->nverts;
			params.polys = polyMesh->polys;
			params.polyAreas = polyMesh->areas;
			params.polyFlags = polyMesh->flags;
			params.polyCount = polyMesh->npolys;
			params.nvp = polyMesh->nvp;
			params.detailMeshes = detail->meshes;
			params.detailVerts = detail->verts;
			params.detailVertsCount = detail->nverts;
			params.detailTris = detail->tris;
			params.detailTriCount = detail->ntris;
			params.walkableHeight = settings.AgentHeight;
			params.walkableRadius = settings.AgentRadius;
			params.walkableClimb = settings.MaxClimb;
			params.tileX = tileX;
			params.tileY = tileZ;
			params.tileLayer = 0;
			rcVcopy(params.bmin, polyMesh->bmin);
			rcVcopy(params.bmax, polyMesh->bmax);
			params.cs = config.cs;
			params.ch = config.ch;
			params.buildBvTree = true;

			unsigned char* data = nullptr;
			int size = 0;
			if (!dtCreateNavMeshData(&params, &data, &size))
				return {};
			std::vector<unsigned char> result(data, data + size);
			dtFree(data);
			return result;
		}

		// Hands a copy of 'data' to the navmesh, which frees it with the tile.
		bool AddTile(dtNavMesh& navMesh, const unsigned char* data, size_t size)
		{
			unsigned char* copy = static_cast<unsigned char*>(dtAlloc(size, DT_ALLOC_PERM));
			if (!copy)
				return false;
			std::memcpy(copy, data, size);
			if (dtStatusFailed(navMesh.addTile(copy, static_cast<int>(size), DT_TILE_FREE_DATA, 0, nullptr)))
			{
				dtFree(copy);
				return false;
			}
			return true;
		}

	}

	NavMeshSettings NavMeshSettings::Clamped() const
	{
		const NavMeshSettings defaults;
		NavMeshSettings result;
		result.AgentRadius = std::clamp(Finite(AgentRadius, defaults.AgentRadius), MinRadius, MaxRadius);
		result.AgentHeight = std::clamp(Finite(AgentHeight, defaults.AgentHeight), MinHeight, MaxHeight);
		result.MaxClimb = std::clamp(Finite(MaxClimb, defaults.MaxClimb), 0.0f, MaxClimbLimit);
		result.MaxSlope = std::clamp(Finite(MaxSlope, defaults.MaxSlope), 0.0f, MaxSlopeLimit);
		return result;
	}

	void NavMeshGeometry::AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
	{
		const uint32_t base = static_cast<uint32_t>(Vertices.size());
		for (const glm::vec3& corner : { a, b, c })
		{
			Vertices.push_back(corner);
			Bounds.Expand(corner);
		}
		Indices.insert(Indices.end(), { base, base + 1, base + 2 });
	}

	uint64_t NavMeshGeometry::ComputeHash(const NavMeshSettings& settings) const
	{
		// FNV-1a over the triangles' corners (in order) and the settings, exact to the bit.
		uint64_t hash = 14695981039346656037ull;
		const auto add = [&hash](const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
				hash = (hash ^ bytes[i]) * 1099511628211ull;
		};
		const NavMeshSettings clamped = settings.Clamped();
		add(&clamped.AgentRadius, sizeof(float));
		add(&clamped.AgentHeight, sizeof(float));
		add(&clamped.MaxClimb, sizeof(float));
		add(&clamped.MaxSlope, sizeof(float));
		for (uint32_t index : Indices)
			add(&Vertices[index], sizeof(glm::vec3));
		return hash;
	}

	NavMesh::~NavMesh()
	{
		dtFreeNavMeshQuery(m_Query);
		dtFreeNavMesh(m_NavMesh);
	}

	bool NavMesh::Init(const glm::vec3& origin, int tilesX, int tilesZ, std::string& error)
	{
		m_Origin = origin;
		m_TilesX = tilesX;
		m_TilesZ = tilesZ;
		const int tileBits = TileBits(tilesX * tilesZ);
		dtNavMeshParams params {};
		params.orig[0] = origin.x;
		params.orig[1] = origin.y;
		params.orig[2] = origin.z;
		params.tileWidth = TileSize;
		params.tileHeight = TileSize;
		params.maxTiles = tilesX * tilesZ;
		params.maxPolys = 1 << (22 - tileBits);
		m_NavMesh = dtAllocNavMesh();
		m_Query = dtAllocNavMeshQuery();
		if (!m_NavMesh || !m_Query || dtStatusFailed(m_NavMesh->init(&params)) || dtStatusFailed(m_Query->init(m_NavMesh, c_QueryNodes)))
		{
			error = "there wasn't memory for the navmesh";
			return false;
		}
		return true;
	}

	Scope<NavMesh> NavMesh::Build(const NavMeshGeometry& geometry, const NavMeshSettings& settings, std::string& error)
	{
		if (geometry.GetTriangleCount() == 0 || !geometry.Bounds.IsValid())
		{
			error = "there is nothing to walk on (navigation is baked from static colliders)";
			return nullptr;
		}
		if (geometry.GetTriangleCount() > MaxTriangles)
		{
			error = "the scene has " + std::to_string(geometry.GetTriangleCount()) + " triangles to walk on; at most " + std::to_string(MaxTriangles) + " can be baked";
			return nullptr;
		}
		if (!AllFinite(&geometry.Bounds.Min.x, 3) || !AllFinite(&geometry.Bounds.Max.x, 3))
		{
			error = "the scene's colliders have positions that aren't numbers";
			return nullptr;
		}

		const AABB& bounds = geometry.Bounds;
		const int tilesX = std::max(1, static_cast<int>(std::ceil((bounds.Max.x - bounds.Min.x) / TileSize)));
		const int tilesZ = std::max(1, static_cast<int>(std::ceil((bounds.Max.z - bounds.Min.z) / TileSize)));
		if (static_cast<int64_t>(tilesX) * tilesZ > MaxTiles)
		{
			error = "the walkable area is " + std::to_string(static_cast<int>(bounds.Max.x - bounds.Min.x)) + " x "
				+ std::to_string(static_cast<int>(bounds.Max.z - bounds.Min.z)) + " m, more than 16384 tiles of 32 m (4096 x 4096 m)";
			return nullptr;
		}

		Scope<NavMesh> navMesh(new NavMesh());
		navMesh->m_Settings = settings.Clamped();
		navMesh->m_SourceHash = geometry.ComputeHash(settings);
		if (!navMesh->Init(bounds.Min, tilesX, tilesZ, error))
			return nullptr;

		// Each triangle goes to every tile its footprint (with the tiles' borders) touches.
		const NavMeshSettings& clamped = navMesh->m_Settings;
		const float border = (std::ceil(clamped.AgentRadius / CellSize(clamped)) + 3.0f) * CellSize(clamped);
		std::vector<std::vector<int>> tileTriangles(static_cast<size_t>(tilesX) * tilesZ);
		for (size_t t = 0; t < geometry.GetTriangleCount(); t++)
		{
			AABB box;
			for (int k = 0; k < 3; k++)
				box.Expand(geometry.Vertices[geometry.Indices[t * 3 + k]]);
			const int x0 = std::clamp(static_cast<int>(std::floor((box.Min.x - border - bounds.Min.x) / TileSize)), 0, tilesX - 1);
			const int x1 = std::clamp(static_cast<int>(std::floor((box.Max.x + border - bounds.Min.x) / TileSize)), 0, tilesX - 1);
			const int z0 = std::clamp(static_cast<int>(std::floor((box.Min.z - border - bounds.Min.z) / TileSize)), 0, tilesZ - 1);
			const int z1 = std::clamp(static_cast<int>(std::floor((box.Max.z + border - bounds.Min.z) / TileSize)), 0, tilesZ - 1);
			for (int z = z0; z <= z1; z++)
			{
				for (int x = x0; x <= x1; x++)
					tileTriangles[static_cast<size_t>(z) * tilesX + x].push_back(static_cast<int>(t));
			}
		}

		const int maxPolys = 1 << (22 - TileBits(tilesX * tilesZ));
		for (int z = 0; z < tilesZ; z++)
		{
			for (int x = 0; x < tilesX; x++)
			{
				const std::vector<int>& triangles = tileTriangles[static_cast<size_t>(z) * tilesX + x];
				if (triangles.empty())
					continue;
				const glm::vec3 tileMin(bounds.Min.x + x * TileSize, bounds.Min.y - 1.0f, bounds.Min.z + z * TileSize);
				const glm::vec3 tileMax(tileMin.x + TileSize, bounds.Max.y + 1.0f, tileMin.z + TileSize);
				const std::vector<unsigned char> data = BuildTile(geometry, triangles, clamped, tileMin, tileMax, x, z, maxPolys);
				if (!data.empty() && !AddTile(*navMesh->m_NavMesh, data.data(), data.size()))
					BW_CORE_WARN("Navigation: tile ({}, {}) could not be added", x, z);
			}
		}
		if (navMesh->GetPolygonCount() == 0)
		{
			error = "nothing in the scene is wide, flat and open enough for the agents to walk on";
			return nullptr;
		}
		return navMesh;
	}

	std::vector<uint8_t> NavMesh::Save() const
	{
		std::vector<uint8_t> bytes(sizeof(FileHeader));
		FileHeader header {};
		std::memcpy(header.Magic, c_Magic, 4);
		header.Version = c_FileVersion;
		header.AgentRadius = m_Settings.AgentRadius;
		header.AgentHeight = m_Settings.AgentHeight;
		header.MaxClimb = m_Settings.MaxClimb;
		header.MaxSlope = m_Settings.MaxSlope;
		header.Origin[0] = m_Origin.x;
		header.Origin[1] = m_Origin.y;
		header.Origin[2] = m_Origin.z;
		header.TilesX = m_TilesX;
		header.TilesZ = m_TilesZ;
		header.SourceHash = m_SourceHash;

		const dtNavMesh* navMesh = m_NavMesh;
		for (int i = 0; i < navMesh->getMaxTiles(); i++)
		{
			const dtMeshTile* tile = navMesh->getTile(i);
			if (!tile || !tile->header || tile->dataSize <= 0)
				continue;
			const uint32_t size = static_cast<uint32_t>(tile->dataSize);
			const size_t at = bytes.size();
			bytes.resize(at + sizeof(size) + size);
			std::memcpy(bytes.data() + at, &size, sizeof(size));
			std::memcpy(bytes.data() + at + sizeof(size), tile->data, size);
			header.TileCount++;
		}
		std::memcpy(bytes.data(), &header, sizeof(header));
		return bytes;
	}

	Scope<NavMesh> NavMesh::Load(const std::vector<uint8_t>& bytes, std::string& error)
	{
		FileHeader header {};
		if (bytes.size() < sizeof(header))
		{
			error = "the file is too short to be a navmesh";
			return nullptr;
		}
		std::memcpy(&header, bytes.data(), sizeof(header));
		if (std::memcmp(header.Magic, c_Magic, 4) != 0)
		{
			error = "the file isn't a navmesh";
			return nullptr;
		}
		if (header.Version != c_FileVersion)
		{
			error = "the navmesh was baked by another version of Bellows; bake it again";
			return nullptr;
		}
		NavMeshSettings settings;
		settings.AgentRadius = header.AgentRadius;
		settings.AgentHeight = header.AgentHeight;
		settings.MaxClimb = header.MaxClimb;
		settings.MaxSlope = header.MaxSlope;
		if (!(settings.Clamped() == settings) || !AllFinite(header.Origin, 3) || header.TilesX < 1 || header.TilesZ < 1
			|| static_cast<int64_t>(header.TilesX) * header.TilesZ > MaxTiles || header.TileCount > static_cast<uint32_t>(header.TilesX * header.TilesZ))
		{
			error = "the navmesh's settings are out of range";
			return nullptr;
		}

		Scope<NavMesh> navMesh(new NavMesh());
		navMesh->m_Settings = settings;
		navMesh->m_SourceHash = header.SourceHash;
		if (!navMesh->Init(glm::vec3(header.Origin[0], header.Origin[1], header.Origin[2]), header.TilesX, header.TilesZ, error))
			return nullptr;
		const int polyBits = 22 - TileBits(header.TilesX * header.TilesZ);

		size_t at = sizeof(header);
		for (uint32_t t = 0; t < header.TileCount; t++)
		{
			uint32_t size = 0;
			if (bytes.size() - at < sizeof(size))
			{
				error = "the file is cut short";
				return nullptr;
			}
			std::memcpy(&size, bytes.data() + at, sizeof(size));
			at += sizeof(size);
			if (size > c_MaxTileBytes || bytes.size() - at < size)
			{
				error = "the file is cut short";
				return nullptr;
			}
			if (size < sizeof(dtMeshHeader))
			{
				error = "a tile is cut short";
				return nullptr;
			}
			// A copy, aligned for the checks and for Detour.
			std::vector<uint32_t> aligned((size + 3) / 4);
			std::memcpy(aligned.data(), bytes.data() + at, size);
			const unsigned char* tile = reinterpret_cast<const unsigned char*>(aligned.data());
			if (!CheckTile(tile, size, header.TilesX, header.TilesZ, polyBits, error))
				return nullptr;
			if (!AddTile(*navMesh->m_NavMesh, tile, size))
			{
				error = "a tile couldn't be added (two tiles in one place?)";
				return nullptr;
			}
			at += size;
		}
		if (at != bytes.size())
		{
			error = "the file has data after its last tile";
			return nullptr;
		}
		return navMesh;
	}

	int NavMesh::GetTileCount() const
	{
		int count = 0;
		const dtNavMesh* navMesh = m_NavMesh;
		for (int i = 0; i < navMesh->getMaxTiles(); i++)
		{
			const dtMeshTile* tile = navMesh->getTile(i);
			count += tile && tile->header ? 1 : 0;
		}
		return count;
	}

	float NavMesh::GetCellHeight() const
	{
		return CellHeight(m_Settings);
	}

	int NavMesh::GetPolygonCount() const
	{
		int count = 0;
		const dtNavMesh* navMesh = m_NavMesh;
		for (int i = 0; i < navMesh->getMaxTiles(); i++)
		{
			const dtMeshTile* tile = navMesh->getTile(i);
			count += tile && tile->header ? tile->header->polyCount : 0;
		}
		return count;
	}

	std::vector<glm::vec3> NavMesh::FindPath(const glm::vec3& start, const glm::vec3& end) const
	{
		if (!AllFinite(&start.x, 3) || !AllFinite(&end.x, 3))
			return {};
		dtQueryFilter filter;
		filter.setIncludeFlags(c_WalkableFlag);
		const float extents[3] = { 2.0f, m_Settings.AgentHeight, 2.0f };
		dtPolyRef startRef = 0, endRef = 0;
		float startPoint[3], endPoint[3];
		m_Query->findNearestPoly(&start.x, extents, &filter, &startRef, startPoint);
		m_Query->findNearestPoly(&end.x, extents, &filter, &endRef, endPoint);
		if (!startRef || !endRef)
			return {};

		std::vector<dtPolyRef> polys(MaxPathCorners);
		int polyCount = 0;
		if (dtStatusFailed(m_Query->findPath(startRef, endRef, startPoint, endPoint, &filter, polys.data(), &polyCount, static_cast<int>(polys.size())))
			|| polyCount == 0)
			return {};
		// An end that can't be reached: as close as the last polygon gets.
		if (polys[static_cast<size_t>(polyCount - 1)] != endRef)
			m_Query->closestPointOnPoly(polys[static_cast<size_t>(polyCount - 1)], endPoint, endPoint, nullptr);

		std::vector<float> corners(MaxPathCorners * 3);
		int cornerCount = 0;
		if (dtStatusFailed(m_Query->findStraightPath(startPoint, endPoint, polys.data(), polyCount, corners.data(), nullptr, nullptr,
			&cornerCount, static_cast<int>(MaxPathCorners))))
			return {};
		std::vector<glm::vec3> path;
		path.reserve(static_cast<size_t>(cornerCount));
		for (int i = 0; i < cornerCount; i++)
			path.emplace_back(corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]);
		return path;
	}

	std::optional<glm::vec3> NavMesh::FindNearestPoint(const glm::vec3& point, float searchDistance) const
	{
		if (!AllFinite(&point.x, 3) || !std::isfinite(searchDistance) || searchDistance < 0.0f)
			return std::nullopt;
		dtQueryFilter filter;
		filter.setIncludeFlags(c_WalkableFlag);
		const float extents[3] = { searchDistance, std::max(searchDistance, m_Settings.AgentHeight), searchDistance };
		dtPolyRef ref = 0;
		glm::vec3 nearest(0.0f);
		m_Query->findNearestPoly(&point.x, extents, &filter, &ref, &nearest.x);
		if (!ref || glm::length(glm::vec2(nearest.x - point.x, nearest.z - point.z)) > searchDistance)
			return std::nullopt;
		return nearest;
	}

	bool NavMesh::IsNavigable(const glm::vec3& point) const
	{
		if (!AllFinite(&point.x, 3))
			return false;
		dtQueryFilter filter;
		filter.setIncludeFlags(c_WalkableFlag);
		const float cell = CellSize(m_Settings);
		const float extents[3] = { cell, m_Settings.MaxClimb + CellHeight(m_Settings), cell };
		dtPolyRef ref = 0;
		glm::vec3 nearest(0.0f);
		m_Query->findNearestPoly(&point.x, extents, &filter, &ref, &nearest.x);
		return ref && glm::length(glm::vec2(nearest.x - point.x, nearest.z - point.z)) < 0.01f;
	}

	void NavMesh::GetTriangles(std::vector<glm::vec3>& outCorners) const
	{
		outCorners.clear();
		const dtNavMesh* navMesh = m_NavMesh;
		for (int i = 0; i < navMesh->getMaxTiles(); i++)
		{
			const dtMeshTile* tile = navMesh->getTile(i);
			if (!tile || !tile->header)
				continue;
			for (int p = 0; p < tile->header->polyCount; p++)
			{
				const dtPoly& poly = tile->polys[p];
				if (!(poly.flags & c_WalkableFlag))
					continue;
				const dtPolyDetail& detail = tile->detailMeshes[p];
				for (int t = 0; t < detail.triCount; t++)
				{
					const unsigned char* triangle = &tile->detailTris[(detail.triBase + t) * 4];
					for (int k = 0; k < 3; k++)
					{
						const float* corner = triangle[k] < poly.vertCount ? &tile->verts[poly.verts[triangle[k]] * 3]
							: &tile->detailVerts[(detail.vertBase + triangle[k] - poly.vertCount) * 3];
						outCorners.emplace_back(corner[0], corner[1], corner[2]);
					}
				}
			}
		}
	}

}
