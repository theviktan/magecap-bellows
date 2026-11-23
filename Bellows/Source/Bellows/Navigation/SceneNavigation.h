#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Navigation/NavMesh.h"

#include <filesystem>
#include <string>

namespace Bellows {

	class Scene;

	// What agents walk on: the shapes of the scene's static colliders (not triggers), as triangles in
	// world space. Mesh colliders need their mesh loaded, so a scene without a renderer bakes without them.
	NavMeshGeometry CollectNavigationGeometry(Scene& scene);

	// The navmesh file that goes with a scene file ("Scenes/Level.bscene" -> "Scenes/Level.bnav").
	std::filesystem::path GetNavMeshPath(const std::filesystem::path& sceneFile);

	// The scene's navmesh, or null when it has none (quietly) or its file is broken (with an error logged).
	Ref<NavMesh> LoadSceneNavMesh(const std::filesystem::path& sceneFile);
	bool SaveSceneNavMesh(const NavMesh& navMesh, const std::filesystem::path& sceneFile);

}
