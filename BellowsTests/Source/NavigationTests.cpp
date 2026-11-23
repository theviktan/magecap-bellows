#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Navigation/NavAgents.h"
#include "Bellows/Navigation/NavMesh.h"
#include "Bellows/Navigation/SceneNavigation.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>
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

	// A running scene on 'geometry''s navmesh, for agents to walk in.
	Scope<Scene> AgentScene(const NavMeshGeometry& geometry)
	{
		Scope<Scene> scene = CreateScope<Scene>("Agents");
		scene->SetNavMesh(Ref<NavMesh>(Bake(geometry).release()));
		return scene;
	}

	Entity AddAgent(Scene& scene, const char* name, const glm::vec3& position)
	{
		Entity entity = scene.CreateEntity(name);
		entity.GetComponent<TransformComponent>().Translation = position;
		entity.AddComponent<NavAgentComponent>();
		return entity;
	}

	void SendTo(Entity entity, const glm::vec3& destination)
	{
		NavAgentComponent& agent = entity.GetComponent<NavAgentComponent>();
		agent.RuntimeDestination = destination;
		agent.RuntimeDestinationChanged = true;
	}

	constexpr float c_Frame = 1.0f / 60.0f;

	// Plays frames until every agent given has arrived (has no destination), or 'maxFrames' pass.
	// Returns the frames it took, or -1.
	int WalkUntilArrived(Scene& scene, std::initializer_list<Entity> agents, int maxFrames, const std::function<void()>& eachFrame = {})
	{
		for (int frame = 1; frame <= maxFrames; frame++)
		{
			scene.OnUpdateRuntime(Timestep(c_Frame));
			if (eachFrame)
				eachFrame();
			bool arrived = true;
			for (Entity agent : agents)
				arrived &= !agent.GetComponent<NavAgentComponent>().RuntimeDestination.has_value();
			if (arrived)
				return frame;
		}
		return -1;
	}

	glm::vec3 Position(Entity entity) { return entity.GetComponent<TransformComponent>().Translation; }

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

TEST_CASE("[gpu] A mesh collider made just before a bake is part of it")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	Scene scene("MeshColliders");
	Entity floor = scene.CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().Scale = glm::vec3(10.0f, 1.0f, 10.0f);
	floor.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	floor.AddComponent<BoxColliderComponent>();
	// A 2 m block from the built-in cube, with no frame run yet to load its mesh.
	Entity block = scene.CreateEntity("Block");
	block.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 1.5f, 0.0f);
	block.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
	block.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Cube";
	block.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	block.AddComponent<MeshColliderComponent>();
	std::string error;
	REQUIRE(scene.BakeNavigation(error));
	CHECK_FALSE(scene.GetNavMesh()->IsNavigable(glm::vec3(0.0f, 0.5f, 0.0f)));
	CHECK(scene.GetNavMesh()->IsNavigable(glm::vec3(3.0f, 0.5f, 3.0f)));
}

TEST_CASE("A NavAgent walks around a wall to its destination, at its speed, facing the way it goes")
{
	Scope<Scene> scene = AgentScene(WalledFloor());
	Entity walker = AddAgent(*scene, "Walker", { -5.0f, 0.0f, -5.0f });
	NavAgentComponent& agent = walker.GetComponent<NavAgentComponent>();
	scene->OnRuntimeStart();
	REQUIRE(scene->GetNavAgents());

	// Standing: nothing moves it sideways. With no colliders to stand on, it stands on the navmesh,
	// which lies up to a cell above the triangles it was baked from.
	for (int i = 0; i < 10; i++)
		scene->OnUpdateRuntime(Timestep(c_Frame));
	CHECK(glm::distance(glm::vec2(Position(walker).x, Position(walker).z), glm::vec2(-5.0f, -5.0f)) < 0.01f);
	CHECK(Position(walker).y >= 0.0f);
	CHECK(Position(walker).y <= scene->GetNavMesh()->GetCellHeight() * 2.0f);
	CHECK(scene->GetNavAgents()->GetAgentCount() == 1);

	SendTo(walker, { 5.0f, 0.0f, -5.0f });
	glm::vec3 previous = Position(walker);
	float fastest = 0.0f, walked = 0.0f;
	bool crossedWall = false, facedAhead = false;
	const int frames = WalkUntilArrived(*scene, { walker }, 1200, [&]()
	{
		const glm::vec3 now = Position(walker);
		fastest = std::max(fastest, glm::distance(now, previous) / c_Frame);
		walked += glm::distance(now, previous);
		// The wall runs along x = 0 up to z = 6: only past it may the agent cross.
		if ((previous.x < 0.0f) != (now.x < 0.0f) && now.z < 6.0f)
			crossedWall = true;
		// Mid-walk, at speed, it faces where it walks (forward is -Z).
		const glm::vec3 velocity = agent.RuntimeVelocity;
		if (glm::length(velocity) > 3.0f)
		{
			const glm::vec3 forward = walker.GetComponent<TransformComponent>().GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
			if (glm::dot(glm::normalize(forward), glm::normalize(velocity)) > 0.95f)
				facedAhead = true;
		}
		previous = now;
	});
	INFO("frames ", frames, ", at ", Position(walker).x, ", ", Position(walker).z);
	REQUIRE(frames > 0);
	CHECK_FALSE(crossedWall);
	CHECK(facedAhead);
	CHECK(walked > 20.0f);								// Around the wall's end, not through it
	CHECK(fastest <= agent.Speed * 1.05f);
	CHECK(fastest > agent.Speed * 0.8f);
	CHECK(glm::distance(Position(walker), glm::vec3(5.0f, 0.0f, -5.0f)) <= agent.StoppingDistance + 0.05f);
	CHECK(Position(walker).y <= scene->GetNavMesh()->GetCellHeight() * 2.0f);

	// Arrived, it slows to a stop and stays.
	for (int i = 0; i < 60; i++)
		scene->OnUpdateRuntime(Timestep(c_Frame));
	CHECK(glm::length(agent.RuntimeVelocity) < 0.05f);
	CHECK(glm::distance(Position(walker), glm::vec3(5.0f, 0.0f, -5.0f)) < 0.5f);
	scene->OnRuntimeStop();
	CHECK_FALSE(scene->GetNavAgents());
}

TEST_CASE("NavAgents pass each other, stop when told, and hold while the game is paused")
{
	NavMeshGeometry geometry;
	AddFloor(geometry, -10.0f, -4.0f, 10.0f, 4.0f);
	Scope<Scene> scene = AgentScene(geometry);
	Entity east = AddAgent(*scene, "East", { -6.0f, 0.0f, 0.0f });
	Entity west = AddAgent(*scene, "West", { 6.0f, 0.0f, 0.0f });
	scene->OnRuntimeStart();

	SUBCASE("Head on, they steer around each other and both arrive")
	{
		SendTo(east, { 6.0f, 0.0f, 0.0f });
		SendTo(west, { -6.0f, 0.0f, 0.0f });
		float closest = 100.0f;
		const int frames = WalkUntilArrived(*scene, { east, west }, 1200, [&]() { closest = std::min(closest, glm::distance(Position(east), Position(west))); });
		REQUIRE(frames > 0);
		CHECK(closest > 0.6f);		// Their radii add to 0.8 m
		CHECK(Position(east).x > 5.5f);
		CHECK(Position(west).x < -5.5f);
	}
	SUBCASE("Another agent standing beside its destination doesn't hold it short")
	{
		// West stands 1.2 m from it: well inside the range agents keep apart over.
		SendTo(east, { 6.0f, 0.0f, -1.2f });
		REQUIRE(WalkUntilArrived(*scene, { east }, 600) > 0);
		CHECK(glm::distance(glm::vec2(Position(east).x, Position(east).z), glm::vec2(6.0f, -1.2f)) <= 0.15f);
		CHECK(glm::distance(Position(west), glm::vec3(6.0f, Position(west).y, 0.0f)) < 0.5f);
	}
	SUBCASE("Stopped, it comes to rest where it is")
	{
		SendTo(east, { 5.0f, 0.0f, -2.0f });
		for (int i = 0; i < 60; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		const float halfway = Position(east).x;
		CHECK(halfway > -5.0f);
		NavAgentComponent& agent = east.GetComponent<NavAgentComponent>();
		agent.RuntimeDestination.reset();
		agent.RuntimeDestinationChanged = true;
		for (int i = 0; i < 60; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(glm::length(agent.RuntimeVelocity) < 0.05f);
		CHECK(Position(east).x < halfway + 2.0f);		// Slowing down at 8 m/s², not walking on
	}
	SUBCASE("The game's pause holds it")
	{
		SendTo(east, { 5.0f, 0.0f, -2.0f });
		for (int i = 0; i < 20; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		const glm::vec3 paused = Position(east);
		scene->SetGamePaused(true);
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(Position(east) == paused);
		scene->SetGamePaused(false);
		REQUIRE(WalkUntilArrived(*scene, { east }, 600) > 0);
	}
	SUBCASE("Sent where no agent can stand, it stays put")
	{
		SendTo(east, { 0.0f, 0.0f, 50.0f });
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK_FALSE(east.GetComponent<NavAgentComponent>().RuntimeDestination);
		CHECK(glm::distance(glm::vec2(Position(east).x, Position(east).z), glm::vec2(-6.0f, 0.0f)) < 0.01f);
	}
	SUBCASE("Moved by a script mid-walk, it walks on from where it was put")
	{
		SendTo(east, { 5.0f, 0.0f, -2.0f });
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		east.GetComponent<TransformComponent>().Translation = { 3.0f, 0.0f, 3.0f };
		scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(glm::distance(glm::vec2(Position(east).x, Position(east).z), glm::vec2(3.0f, 3.0f)) < 0.2f);
		REQUIRE(WalkUntilArrived(*scene, { east }, 600) > 0);
		CHECK(glm::distance(Position(east), glm::vec3(5.0f, Position(east).y, -2.0f)) < 0.2f);
	}
	SUBCASE("A destroyed agent leaves the crowd")
	{
		scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(scene->GetNavAgents()->GetAgentCount() == 2);
		scene->DestroyEntity(west);
		scene->OnUpdateRuntime(Timestep(c_Frame));
		scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(scene->GetNavAgents()->GetAgentCount() == 1);
		east.RemoveComponent<NavAgentComponent>();
		scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(scene->GetNavAgents()->GetAgentCount() == 0);
	}
	scene->OnRuntimeStop();
}

TEST_CASE("A NavAgent moves a kinematic or static body, keeps its base offset, and leaves a dynamic body to physics")
{
	// A collider floor this time, so the agent stands on the ground itself, not on the navmesh just above it.
	Scope<Scene> scene = CreateScope<Scene>("Bodies");
	Entity floor = scene->CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, -0.5f, 0.0f);
	floor.GetComponent<TransformComponent>().Scale = glm::vec3(20.0f, 1.0f, 8.0f);
	floor.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	floor.AddComponent<BoxColliderComponent>();
	std::string error;
	REQUIRE(scene->BakeNavigation(error));

	Entity capsule = AddAgent(*scene, "Capsule", { -6.0f, 0.9f, -2.0f });
	capsule.GetComponent<NavAgentComponent>().BaseOffset = 0.9f;
	capsule.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Kinematic;
	capsule.AddComponent<CapsuleColliderComponent>();

	Entity ball = AddAgent(*scene, "Ball", { -6.0f, 0.5f, 2.0f });
	RigidBodyComponent& body = ball.AddComponent<RigidBodyComponent>();
	body.Type = RigidBodyType::Dynamic;
	body.GravityScale = 0.0f;
	ball.AddComponent<SphereColliderComponent>();

	// A static body moved by its agent: its own box must not count as the ground under it.
	Entity crate = AddAgent(*scene, "Crate", { -6.0f, 0.5f, 0.0f });
	crate.GetComponent<NavAgentComponent>().BaseOffset = 0.5f;
	crate.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	crate.AddComponent<BoxColliderComponent>();

	scene->OnRuntimeStart();
	SendTo(capsule, { 6.0f, 0.0f, -2.0f });
	SendTo(ball, { 6.0f, 0.0f, 2.0f });
	SendTo(crate, { 0.0f, 0.0f, 0.0f });
	for (int i = 0; i < 300 && capsule.GetComponent<NavAgentComponent>().RuntimeDestination; i++)
		scene->OnUpdateRuntime(Timestep(c_Frame));

	CHECK_FALSE(capsule.GetComponent<NavAgentComponent>().RuntimeDestination);
	CHECK(Position(capsule).x > 5.5f);
	CHECK(Position(capsule).y == doctest::Approx(0.9f).epsilon(0.002));	// Its origin rides 0.9 m over the floor
	CHECK(Position(ball).x == doctest::Approx(-6.0f));
	CHECK(Position(crate).x > -1.0f);
	CHECK(Position(crate).y == doctest::Approx(0.5f).epsilon(0.002));
	CHECK(scene->GetNavAgents()->GetAgentCount() == 2);
	scene->OnRuntimeStop();
}

TEST_CASE("NavAgents wait for a navmesh, and walk on when one is baked during play")
{
	Scene scene("Bare");
	Entity floor = scene.CreateEntity("Floor");
	floor.GetComponent<TransformComponent>().Scale = glm::vec3(20.0f, 1.0f, 20.0f);
	floor.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, -0.5f, 0.0f);
	floor.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	floor.AddComponent<BoxColliderComponent>();
	Entity walker = AddAgent(scene, "Walker", { -4.0f, 0.0f, 0.0f });

	scene.OnRuntimeStart();
	SendTo(walker, { 4.0f, 0.0f, 0.0f });
	for (int i = 0; i < 30; i++)
		scene.OnUpdateRuntime(Timestep(c_Frame));
	CHECK(Position(walker) == glm::vec3(-4.0f, 0.0f, 0.0f));
	CHECK(walker.GetComponent<NavAgentComponent>().RuntimeDestination);	// Kept for when it can walk
	CHECK(scene.GetNavAgents()->GetAgentCount() == 0);

	std::string error;
	REQUIRE(scene.BakeNavigation(error));
	REQUIRE(WalkUntilArrived(scene, { walker }, 600) > 0);
	CHECK(Position(walker).x == doctest::Approx(4.0f).epsilon(0.05));
	scene.OnRuntimeStop();
}

TEST_CASE("A NavAgent is saved with its scene, and loaded settings are held to their ranges")
{
	Scene scene("Saved");
	Entity entity = scene.CreateEntity("Guard");
	NavAgentComponent& agent = entity.AddComponent<NavAgentComponent>();
	agent.Speed = 2.5f;
	agent.Acceleration = 12.0f;
	agent.Radius = 0.3f;
	agent.Height = 1.6f;
	agent.BaseOffset = 0.8f;
	agent.StoppingDistance = 0.5f;
	agent.TurnSpeed = 180.0f;
	agent.AvoidOthers = false;
	Json json = SceneSerializer::SerializeToJson(scene);

	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	auto view = loaded.GetAllEntitiesWith<NavAgentComponent>();
	REQUIRE(view.size() == 1);
	const NavAgentComponent& read = view.get<NavAgentComponent>(*view.begin());
	CHECK(read.Speed == 2.5f);
	CHECK(read.Acceleration == 12.0f);
	CHECK(read.Radius == 0.3f);
	CHECK(read.Height == 1.6f);
	CHECK(read.BaseOffset == 0.8f);
	CHECK(read.StoppingDistance == 0.5f);
	CHECK(read.TurnSpeed == 180.0f);
	CHECK_FALSE(read.AvoidOthers);

	// A hand-edited file: too fast, too small, and a number JSON can't hold as a float.
	Json& saved = json["Entities"][0]["NavAgent"];
	saved["Speed"] = 500.0;
	saved["Radius"] = 0.0;
	saved["Acceleration"] = -3.0;
	saved["TurnSpeed"] = 1e300;
	Scene clamped("Clamped");
	REQUIRE(SceneSerializer::DeserializeFromJson(clamped, json));
	auto clampedView = clamped.GetAllEntitiesWith<NavAgentComponent>();
	REQUIRE(clampedView.size() == 1);
	const NavAgentComponent& held = clampedView.get<NavAgentComponent>(*clampedView.begin());
	CHECK(held.Speed == NavAgentComponent::MaxSpeed);
	CHECK(held.Radius == NavAgentComponent::MinRadius);
	CHECK(held.Acceleration == NavAgentComponent::MinAcceleration);
	CHECK(std::isfinite(held.TurnSpeed));

	NavAgentComponent nan;
	nan.Speed = std::numeric_limits<float>::quiet_NaN();
	nan.Height = std::numeric_limits<float>::infinity();
	nan.Clamp();
	CHECK(nan.Speed == NavAgentComponent().Speed);
	CHECK(nan.Height == NavAgentComponent().Height);
}

TEST_CASE("An agent that arrives sends 'destination reached', saying whether it got there")
{
	// Two floors with a gap no agent can cross.
	NavMeshGeometry geometry;
	AddFloor(geometry, -10.0f, -3.0f, 0.0f, 3.0f);
	AddFloor(geometry, 3.0f, -3.0f, 10.0f, 3.0f);
	Scope<Scene> scene = AgentScene(geometry);
	Entity walker = AddAgent(*scene, "Walker", { -8.0f, 0.0f, 0.0f });

	std::vector<std::pair<Scripting::ScriptEntity, bool>> reached;
	bool aboutTheAgent = true;
	const Scripting::ScriptEventListenerID listener = Scripting::AddScriptEventListener(
		[&](const std::string& name, const Scripting::ScriptValueList& values, const Scripting::ScriptEventDelivery& delivery)
		{
			if (name == "destination reached" && values.size() == 2)
			{
				reached.emplace_back(std::get<Scripting::ScriptEntity>(values[0].Data), std::get<bool>(values[1].Data));
				// Every script hears it (no Target), and it names the agent as its source.
				aboutTheAgent &= !delivery.Target && delivery.Source && delivery.Source->ID == walker.GetUUID();
			}
		});
	scene->OnRuntimeStart();

	SUBCASE("At the spot it was sent to")
	{
		SendTo(walker, { -2.0f, 0.0f, 1.0f });
		REQUIRE(WalkUntilArrived(*scene, { walker }, 600) > 0);
		REQUIRE(reached.size() == 1);
		CHECK(reached[0].first.ID == walker.GetUUID());
		CHECK(reached[0].second);
		CHECK(aboutTheAgent);
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(reached.size() == 1);		// Once
	}
	SUBCASE("On the other floor: as close as it can get, and it says so")
	{
		SendTo(walker, { 6.0f, 0.0f, 0.0f });
		REQUIRE(WalkUntilArrived(*scene, { walker }, 900) > 0);
		REQUIRE(reached.size() == 1);
		CHECK_FALSE(reached[0].second);
		CHECK(Position(walker).x > -1.0f);
		CHECK(Position(walker).x < 0.0f);
	}
	SUBCASE("Stopped on the way, it sends nothing")
	{
		SendTo(walker, { -2.0f, 0.0f, 1.0f });
		for (int i = 0; i < 30; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		NavAgentComponent& agent = walker.GetComponent<NavAgentComponent>();
		agent.RuntimeDestination.reset();
		agent.RuntimeDestinationChanged = true;
		for (int i = 0; i < 120; i++)
			scene->OnUpdateRuntime(Timestep(c_Frame));
		CHECK(reached.empty());
	}
	scene->OnRuntimeStop();
	Scripting::RemoveScriptEventListener(listener);
}
