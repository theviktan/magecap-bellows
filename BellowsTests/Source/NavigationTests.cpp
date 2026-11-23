#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Navigation/NavMesh.h"
#include "Bellows/Navigation/SceneNavigation.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	void AddQuad(NavMeshGeometry& geometry, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
	{
		geometry.AddTriangle(a, b, c);
		geometry.AddTriangle(a, c, d);
	}

	// A flat floor from 'min' to 'max' (x and z) at height y, facing up.
	void AddFloor(NavMeshGeometry& geometry, float minX, float minZ, float maxX, float maxZ, float y = 0.0f)
	{
		AddQuad(geometry, { minX, y, minZ }, { minX, y, maxZ }, { maxX, y, maxZ }, { maxX, y, minZ });
	}

	// A closed box, as the scene's box colliders become.
	void AddBlock(NavMeshGeometry& geometry, const glm::vec3& min, const glm::vec3& max)
	{
		const glm::vec3 c[8] = {
			{ min.x, min.y, min.z }, { max.x, min.y, min.z }, { min.x, max.y, min.z }, { max.x, max.y, min.z },
			{ min.x, min.y, max.z }, { max.x, min.y, max.z }, { min.x, max.y, max.z }, { max.x, max.y, max.z } };
		AddQuad(geometry, c[2], c[6], c[7], c[3]);	// Top
		AddQuad(geometry, c[0], c[1], c[5], c[4]);	// Bottom
		AddQuad(geometry, c[0], c[2], c[3], c[1]);
		AddQuad(geometry, c[4], c[5], c[7], c[6]);
		AddQuad(geometry, c[0], c[4], c[6], c[2]);
		AddQuad(geometry, c[1], c[3], c[7], c[5]);
	}

	// A 20 x 20 m floor with a wall across the middle (x = 0), open only past z = 6.
	NavMeshGeometry WalledFloor()
	{
		NavMeshGeometry geometry;
		AddFloor(geometry, -10.0f, -10.0f, 10.0f, 10.0f);
		AddBlock(geometry, { -0.2f, 0.0f, -10.0f }, { 0.2f, 3.0f, 6.0f });
		return geometry;
	}

	Scope<NavMesh> Bake(const NavMeshGeometry& geometry, const NavMeshSettings& settings = {})
	{
		std::string error;
		Scope<NavMesh> navMesh = NavMesh::Build(geometry, settings, error);
		INFO(error);
		REQUIRE(navMesh);
		return navMesh;
	}

	float PathLength(const std::vector<glm::vec3>& path)
	{
		float length = 0.0f;
		for (size_t i = 1; i < path.size(); i++)
			length += glm::distance(path[i - 1], path[i]);
		return length;
	}

}

TEST_CASE("A navmesh is baked from triangles and finds paths around walls")
{
	const Scope<NavMesh> navMesh = Bake(WalledFloor());
	CHECK(navMesh->GetTileCount() == 1);
	CHECK(navMesh->GetPolygonCount() > 0);

	const glm::vec3 start(-5.0f, 0.0f, 0.0f), end(5.0f, 0.0f, 0.0f);
	const std::vector<glm::vec3> path = navMesh->FindPath(start, end);
	REQUIRE(path.size() >= 3);
	CHECK(glm::distance(path.front(), start) < 0.2f);
	CHECK(glm::distance(path.back(), end) < 0.2f);
	// Around the wall's open end: past z = 6, kept the agent's radius from it.
	bool aroundTheEnd = false;
	for (const glm::vec3& corner : path)
	{
		aroundTheEnd |= corner.z > 6.0f + 0.3f;
		CHECK(navMesh->IsNavigable(corner));
	}
	CHECK(aroundTheEnd);
	CHECK(PathLength(path) > 16.0f);	// Straight through would be 10 m; around the end it's two legs of about 8.3 m

	// The wall and the floor right beside it aren't walkable; open floor is.
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(0.0f, 0.0f, 0.0f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(0.0f, 3.0f, 0.0f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(0.3f, 0.0f, 0.0f)));
	CHECK(navMesh->IsNavigable(glm::vec3(3.0f, 0.0f, 3.0f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(3.0f, 2.0f, 3.0f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(30.0f, 0.0f, 0.0f)));

	// The nearest point to one beside the wall is just clear of it.
	const std::optional<glm::vec3> nearest = navMesh->FindNearestPoint(glm::vec3(0.1f, 0.0f, 0.0f), 2.0f);
	REQUIRE(nearest);
	CHECK(std::abs(nearest->x) > 0.4f);
	CHECK_FALSE(navMesh->FindNearestPoint(glm::vec3(50.0f, 0.0f, 0.0f), 2.0f));

	// Numbers that aren't: nothing found, nothing crashes.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(navMesh->FindPath(glm::vec3(nan), end).empty());
	CHECK_FALSE(navMesh->FindNearestPoint(glm::vec3(0.0f), nan));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(nan)));

	std::vector<glm::vec3> triangles;
	navMesh->GetTriangles(triangles);
	CHECK(triangles.size() % 3 == 0);
	CHECK(triangles.size() >= 3);
}

TEST_CASE("A navmesh keeps to its agent's slope, step and clearance")
{
	NavMeshGeometry geometry;
	AddFloor(geometry, -10.0f, -10.0f, 10.0f, 10.0f);
	// A 20 degree ramp up to a platform 2 m high (walkable), and a 60 degree one (not).
	const float gentle = 2.0f / std::tan(glm::radians(20.0f));
	AddQuad(geometry, { -9.0f, 0.0f, 0.0f }, { -9.0f, 2.0f, gentle }, { -6.0f, 2.0f, gentle }, { -6.0f, 0.0f, 0.0f });
	AddBlock(geometry, { -9.0f, 0.0f, gentle }, { -6.0f, 2.0f, gentle + 3.0f });
	const float steep = 2.0f / std::tan(glm::radians(60.0f));
	AddQuad(geometry, { 6.0f, 0.0f, 0.0f }, { 6.0f, 2.0f, steep }, { 9.0f, 2.0f, steep }, { 9.0f, 0.0f, 0.0f });
	// A shelf 1 m above the floor: too low to stand under for a 1.8 m agent.
	AddBlock(geometry, { -2.0f, 1.0f, -8.0f }, { 2.0f, 1.2f, -4.0f });

	const Scope<NavMesh> navMesh = Bake(geometry);
	CHECK(navMesh->IsNavigable(glm::vec3(-7.5f, 1.0f, gentle / 2.0f)));
	CHECK(navMesh->IsNavigable(glm::vec3(-7.5f, 2.0f, gentle + 1.5f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(7.5f, 1.0f, steep / 2.0f)));
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(0.0f, 0.0f, -6.0f)));
	CHECK(navMesh->IsNavigable(glm::vec3(0.0f, 1.2f, -6.0f)));

	// The platform is reached by the ramp.
	const std::vector<glm::vec3> path = navMesh->FindPath(glm::vec3(0.0f), glm::vec3(-7.5f, 2.0f, gentle + 1.5f));
	REQUIRE_FALSE(path.empty());
	CHECK(glm::distance(path.back(), glm::vec3(-7.5f, 2.0f, gentle + 1.5f)) < 0.3f);

	// A shorter agent can stand under the shelf.
	NavMeshSettings small;
	small.AgentHeight = 0.6f;
	CHECK(Bake(geometry, small)->IsNavigable(glm::vec3(0.0f, 0.0f, -6.0f)));
}

TEST_CASE("A big world is baked in tiles, and paths cross them")
{
	NavMeshGeometry geometry;
	AddFloor(geometry, 0.0f, 0.0f, 100.0f, 40.0f);
	const Scope<NavMesh> navMesh = Bake(geometry);
	CHECK(navMesh->GetTileCount() == 8);	// 4 x 2 tiles of 32 m
	const std::vector<glm::vec3> path = navMesh->FindPath(glm::vec3(2.0f, 0.0f, 2.0f), glm::vec3(98.0f, 0.0f, 38.0f));
	REQUIRE(path.size() >= 2);
	CHECK(glm::distance(path.back(), glm::vec3(98.0f, 0.0f, 38.0f)) < 0.2f);
	// A straight floor: the straight line, however many tiles it crosses.
	CHECK(PathLength(path) == doctest::Approx(glm::distance(glm::vec3(2.0f, 0.0f, 2.0f), glm::vec3(98.0f, 0.0f, 38.0f))).epsilon(0.01));
}

TEST_CASE("A navmesh can't be baked from nothing, too much or broken geometry")
{
	std::string error;
	CHECK_FALSE(NavMesh::Build(NavMeshGeometry {}, {}, error));
	CHECK(error.find("nothing to walk on") != std::string::npos);

	// 157 x 157 tiles of 32 m: more than the 16,384 a navmesh can have. A long, narrow world is fine.
	NavMeshGeometry huge;
	AddFloor(huge, 0.0f, 0.0f, 5000.0f, 5000.0f);
	CHECK_FALSE(NavMesh::Build(huge, {}, error));
	CHECK(error.find("16384 tiles") != std::string::npos);

	NavMeshGeometry broken;
	broken.AddTriangle(glm::vec3(0.0f), glm::vec3(std::numeric_limits<float>::infinity()), glm::vec3(1.0f));
	CHECK_FALSE(NavMesh::Build(broken, {}, error));

	// Only walls: nothing to stand on.
	NavMeshGeometry wall;
	AddQuad(wall, { 0.0f, 0.0f, 0.0f }, { 0.0f, 3.0f, 0.0f }, { 5.0f, 3.0f, 0.0f }, { 5.0f, 0.0f, 0.0f });
	CHECK_FALSE(NavMesh::Build(wall, {}, error));
	CHECK(error.find("walk on") != std::string::npos);

	// Settings out of range are held to it.
	NavMeshSettings odd;
	odd.AgentRadius = -1.0f;
	odd.AgentHeight = std::numeric_limits<float>::quiet_NaN();
	odd.MaxSlope = 120.0f;
	const NavMeshSettings clamped = odd.Clamped();
	CHECK(clamped.AgentRadius == doctest::Approx(NavMeshSettings::MinRadius));
	CHECK(clamped.AgentHeight == doctest::Approx(NavMeshSettings().AgentHeight));
	CHECK(clamped.MaxSlope == doctest::Approx(NavMeshSettings::MaxSlopeLimit));
}

TEST_CASE("A navmesh is saved and loaded whole, and broken files are refused")
{
	NavMeshGeometry geometry = WalledFloor();
	AddFloor(geometry, 10.0f, -10.0f, 40.0f, 10.0f);	// A second tile
	const Scope<NavMesh> baked = Bake(geometry);
	REQUIRE(baked->GetTileCount() == 2);
	const std::vector<uint8_t> bytes = baked->Save();

	std::string error;
	const Scope<NavMesh> loaded = NavMesh::Load(bytes, error);
	INFO(error);
	REQUIRE(loaded);
	CHECK(loaded->GetTileCount() == 2);
	CHECK(loaded->GetPolygonCount() == baked->GetPolygonCount());
	CHECK(loaded->GetSourceHash() == baked->GetSourceHash());
	CHECK(loaded->GetSettings() == baked->GetSettings());
	CHECK(loaded->FindPath(glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(30.0f, 0.0f, 0.0f)) == baked->FindPath(glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(30.0f, 0.0f, 0.0f)));
	CHECK(loaded->Save() == bytes);

	auto refused = [&](std::vector<uint8_t> broken, const char* reason)
	{
		CAPTURE(reason);
		std::string message;
		CHECK_FALSE(NavMesh::Load(broken, message));
		CHECK_FALSE(message.empty());
	};
	refused({}, "empty");
	refused(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 20), "too short");
	refused(std::vector<uint8_t>(bytes.begin(), bytes.end() - 4), "cut short");
	std::vector<uint8_t> extra = bytes;
	extra.push_back(0);
	refused(extra, "data after the last tile");
	std::vector<uint8_t> magic = bytes;
	magic[0] = 'X';
	refused(magic, "not a navmesh");
	std::vector<uint8_t> version = bytes;
	version[4] = 99;
	refused(version, "another version");
	// The first tile's polygon count (in Detour's header, after the file header and the tile's size).
	std::vector<uint8_t> counts = bytes;
	const int32_t lots = 1 << 24;
	std::memcpy(counts.data() + 56 + 4 + 6 * 4, &lots, sizeof(lots));
	refused(counts, "counts out of range");
	// Two copies of the same tile.
	std::vector<uint8_t> twice = bytes;
	uint32_t firstSize = 0;
	std::memcpy(&firstSize, bytes.data() + 56, 4);
	twice.insert(twice.end(), bytes.begin() + 56, bytes.begin() + 56 + 4 + firstSize);
	uint32_t tileCount = 3;
	std::memcpy(twice.data() + 44, &tileCount, 4);	// The file header's tile count
	refused(twice, "two tiles in one place");
}

TEST_CASE("A scene bakes its navmesh from its static colliders")
{
	const Ref<Scene> scene = CreateRef<Scene>("Navigation");
	auto addStatic = [&](const char* name, const glm::vec3& position, const glm::vec3& scale)
	{
		Entity entity = scene->CreateEntity(name);
		entity.GetComponent<TransformComponent>().Translation = position;
		entity.GetComponent<TransformComponent>().Scale = scale;
		entity.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
		entity.AddComponent<BoxColliderComponent>();
		return entity;
	};
	addStatic("Floor", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
	addStatic("Wall", glm::vec3(0.0f, 1.5f, -2.0f), glm::vec3(0.4f, 3.0f, 16.0f));
	// Neither a dynamic body nor a trigger is walked on.
	Entity crate = addStatic("Crate", glm::vec3(5.0f, 0.5f, 5.0f), glm::vec3(1.0f));
	crate.GetComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
	Entity zone = addStatic("Zone", glm::vec3(-5.0f, 0.5f, 5.0f), glm::vec3(2.0f));
	zone.GetComponent<RigidBodyComponent>().IsTrigger = true;
	// A static sphere and capsule become round obstacles.
	Entity rock = scene->CreateEntity("Rock");
	rock.GetComponent<TransformComponent>().Translation = glm::vec3(5.0f, 0.0f, -5.0f);
	rock.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	rock.AddComponent<SphereColliderComponent>().Radius = 1.0f;
	Entity post = scene->CreateEntity("Post");
	post.GetComponent<TransformComponent>().Translation = glm::vec3(-5.0f, 1.0f, -5.0f);
	post.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	post.AddComponent<CapsuleColliderComponent>();

	const NavMeshGeometry geometry = CollectNavigationGeometry(*scene);
	CHECK(geometry.GetTriangleCount() == 2 * 12 + 2 * (13 * 12 * 2));	// Two boxes; a sphere and a capsule, 13 bands of 12 quads each
	CHECK(geometry.Bounds.Max.y == doctest::Approx(3.0f));

	std::string error;
	REQUIRE(scene->BakeNavigation(error));
	const Ref<NavMesh>& navMesh = scene->GetNavMesh();
	REQUIRE(navMesh);
	CHECK(navMesh->IsNavigable(glm::vec3(5.0f, 0.0f, 5.0f)));		// Under the dynamic crate: open floor
	CHECK(navMesh->IsNavigable(glm::vec3(-5.0f, 0.0f, 5.0f)));		// Inside the trigger: open floor
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(5.0f, 0.0f, -5.0f)));	// Under the rock
	CHECK_FALSE(navMesh->IsNavigable(glm::vec3(-5.0f, 0.0f, -5.0f)));	// At the post
	CHECK(navMesh->GetSourceHash() == geometry.ComputeHash(scene->GetSettings().Navigation));

	// Moving the wall changes what the navmesh was baked from.
	scene->FindEntityByName("Wall").GetComponent<TransformComponent>().Translation.x = 1.0f;
	CHECK(CollectNavigationGeometry(*scene).ComputeHash(scene->GetSettings().Navigation) != navMesh->GetSourceHash());

	// Play's copy walks the same navmesh; a failed bake keeps the old one.
	CHECK(Scene::Copy(scene)->GetNavMesh() == navMesh);
	const Ref<Scene> empty = CreateRef<Scene>("Empty");
	empty->SetNavMesh(navMesh);
	CHECK_FALSE(empty->BakeNavigation(error));
	CHECK(empty->GetNavMesh() == navMesh);
}

TEST_CASE("A scene's navigation settings and navmesh file go with it")
{
	const Ref<Scene> scene = CreateRef<Scene>("Saved");
	Entity floor = scene->CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().Scale = glm::vec3(10.0f, 1.0f, 10.0f);
	floor.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	floor.AddComponent<BoxColliderComponent>();
	scene->GetSettings().Navigation.AgentRadius = 0.25f;
	scene->GetSettings().Navigation.MaxSlope = 30.0f;
	std::string error;
	REQUIRE(scene->BakeNavigation(error));

	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Navigation";
	REQUIRE(FileSystem::CreateDirectories(folder));
	const std::filesystem::path sceneFile = folder / "Level.bscene";
	CHECK(GetNavMeshPath(sceneFile) == folder / "Level.bnav");
	REQUIRE(SceneSerializer::Serialize(*scene, sceneFile));
	REQUIRE(SaveSceneNavMesh(*scene->GetNavMesh(), sceneFile));

	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::Deserialize(loaded, sceneFile));
	CHECK(loaded.GetSettings().Navigation.AgentRadius == doctest::Approx(0.25f));
	CHECK(loaded.GetSettings().Navigation.MaxSlope == doctest::Approx(30.0f));
	REQUIRE(loaded.GetNavMesh());
	CHECK(loaded.GetNavMesh()->GetSourceHash() == scene->GetNavMesh()->GetSourceHash());
	CHECK(loaded.GetNavMesh()->GetSettings().AgentRadius == doctest::Approx(0.25f));

	// No file: no navmesh, and nothing to complain about. A broken file is refused.
	std::error_code removed;
	std::filesystem::remove(GetNavMeshPath(sceneFile), removed);
	CHECK_FALSE(LoadSceneNavMesh(sceneFile));
	REQUIRE(FileSystem::WriteTextFile(GetNavMeshPath(sceneFile), "not a navmesh"));
	CHECK_FALSE(LoadSceneNavMesh(sceneFile));
}

TEST_CASE("Changing scenes during play brings the new scene's navmesh")
{
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "NavigationChange" / "Assets";
	REQUIRE(FileSystem::CreateDirectories(assets / "Scenes"));
	const std::filesystem::path previous = AssetManager::GetAssetDirectory();
	AssetManager::SetAssetDirectory(assets);

	// "Walk" has a navmesh beside it; "Plain" has none.
	const Ref<Scene> walk = CreateRef<Scene>("Walk");
	Entity floor = walk->CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().Scale = glm::vec3(10.0f, 1.0f, 10.0f);
	floor.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	floor.AddComponent<BoxColliderComponent>();
	std::string error;
	REQUIRE(walk->BakeNavigation(error));
	REQUIRE(SceneSerializer::Serialize(*walk, assets / "Scenes" / "Walk.bscene"));
	REQUIRE(SaveSceneNavMesh(*walk->GetNavMesh(), assets / "Scenes" / "Walk.bscene"));
	const Ref<Scene> plain = CreateRef<Scene>("Plain");
	REQUIRE(SceneSerializer::Serialize(*plain, assets / "Scenes" / "Plain.bscene"));

	Scene playing("Playing");
	playing.OnRuntimeStart();
	CHECK_FALSE(playing.GetNavMesh());
	const std::optional<Json> walkJson = SceneSerializer::ReadSceneFile(assets / "Scenes" / "Walk.bscene");
	REQUIRE(walkJson);
	REQUIRE(playing.ChangeScene(*walkJson, "Scenes/Walk.bscene"));
	REQUIRE(playing.GetNavMesh());
	CHECK(playing.GetNavMesh()->GetSourceHash() == walk->GetNavMesh()->GetSourceHash());
	const std::optional<Json> plainJson = SceneSerializer::ReadSceneFile(assets / "Scenes" / "Plain.bscene");
	REQUIRE(plainJson);
	REQUIRE(playing.ChangeScene(*plainJson, "Scenes/Plain.bscene"));
	CHECK_FALSE(playing.GetNavMesh());
	playing.OnRuntimeStop();
	AssetManager::SetAssetDirectory(previous);
}
