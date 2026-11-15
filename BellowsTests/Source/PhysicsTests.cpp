#include <doctest/doctest.h>

#include "Bellows/Physics/PhysicsWorld.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

using namespace Bellows;

namespace {

	Entity CreateGround(Scene& scene)
	{
		Entity ground = scene.CreateEntity("Ground");
		ground.GetTransform().Translation = { 0.0f, -0.5f, 0.0f };
		ground.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
		ground.AddComponent<BoxColliderComponent>().HalfExtents = { 20.0f, 0.5f, 20.0f };
		return ground;
	}

	Entity CreateBox(Scene& scene, const char* name, const glm::vec3& position)
	{
		Entity box = scene.CreateEntity(name);
		box.GetTransform().Translation = position;
		box.AddComponent<RigidBodyComponent>();
		box.AddComponent<BoxColliderComponent>();
		return box;
	}

	void Simulate(Scene& scene, float seconds)
	{
		const float timestep = scene.GetSettings().FixedTimestep;
		for (float t = 0.0f; t < seconds; t += timestep)
			scene.OnUpdateRuntime(timestep);
	}

}

TEST_CASE("Dynamic bodies fall and come to rest on static ground")
{
	Ref<Scene> scene = CreateRef<Scene>();
	CreateGround(*scene);
	Entity box = CreateBox(*scene, "Box", { 0.0f, 5.0f, 0.0f });

	scene->OnRuntimeStart();
	REQUIRE(scene->GetPhysicsWorld());
	CHECK(scene->GetPhysicsWorld()->GetBodyCount() == 2);

	Simulate(*scene, 0.25f);
	CHECK(box.GetTransform().Translation.y < 5.0f);

	Simulate(*scene, 3.0f);
	CHECK(box.GetTransform().Translation.y == doctest::Approx(0.5f).epsilon(0.02));
	CHECK(glm::length(scene->GetPhysicsWorld()->GetLinearVelocity(box)) < 0.05f);
	scene->OnRuntimeStop();
	CHECK(scene->GetPhysicsWorld() == nullptr);
}

TEST_CASE("Collision and trigger events fire once per contact pair")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity ground = CreateGround(*scene);
	Entity box = CreateBox(*scene, "Box", { 0.0f, 2.0f, 0.0f });
	Entity trigger = scene->CreateEntity("Trigger");
	trigger.GetTransform().Translation = { 0.0f, 1.0f, 0.0f };
	auto& triggerBody = trigger.AddComponent<RigidBodyComponent>();
	triggerBody.Type = RigidBodyType::Static;
	triggerBody.IsTrigger = true;
	trigger.AddComponent<BoxColliderComponent>().HalfExtents = { 1.0f, 0.25f, 1.0f };

	scene->OnRuntimeStart();
	int collisionEnters = 0;
	int triggerEnters = 0;
	int triggerExits = 0;
	const float timestep = scene->GetSettings().FixedTimestep;
	for (int frame = 0; frame < 180; frame++)
	{
		scene->OnUpdateRuntime(timestep);
		for (const ContactEvent& event : scene->GetPhysicsWorld()->GetContactEvents())
		{
			const bool involvesBox = event.EntityA == box.GetUUID() || event.EntityB == box.GetUUID();
			if (!involvesBox)
				continue;
			const bool withGround = event.EntityA == ground.GetUUID() || event.EntityB == ground.GetUUID();
			const bool withTrigger = event.EntityA == trigger.GetUUID() || event.EntityB == trigger.GetUUID();
			if (event.Type == ContactEventType::CollisionEnter && withGround) collisionEnters++;
			if (event.Type == ContactEventType::TriggerEnter && withTrigger) triggerEnters++;
			if (event.Type == ContactEventType::TriggerExit && withTrigger) triggerExits++;
		}
	}
	CHECK(collisionEnters == 1);
	CHECK(triggerEnters == 1);
	CHECK(triggerExits == 1);
	scene->OnRuntimeStop();
}

TEST_CASE("Raycasts report the closest hit")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity ground = CreateGround(*scene);
	Entity box = CreateBox(*scene, "Box", { 0.0f, 3.0f, 0.0f });
	box.GetComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
	scene->OnRuntimeStart();

	RaycastHit hit;
	REQUIRE(scene->GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f, hit));
	CHECK(hit.Entity == box.GetUUID());
	CHECK(hit.Point.y == doctest::Approx(3.5f).epsilon(1e-3));
	CHECK(hit.Normal.y == doctest::Approx(1.0f).epsilon(1e-3));
	CHECK(hit.Distance == doctest::Approx(6.5f).epsilon(1e-3));

	REQUIRE(scene->GetPhysicsWorld()->Raycast({ 5.0f, 10.0f, 0.0f }, { 0.0f, -2.0f, 0.0f }, 100.0f, hit));
	CHECK(hit.Entity == ground.GetUUID());
	CHECK_FALSE(scene->GetPhysicsWorld()->Raycast({ 0.0f, 10.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 100.0f, hit));
	scene->OnRuntimeStop();
}

TEST_CASE("Velocity, impulses, kinematic motion and runtime body changes")
{
	Ref<Scene> scene = CreateRef<Scene>();
	scene->GetSettings().Gravity = glm::vec3(0.0f);
	Entity box = CreateBox(*scene, "Box", { 0.0f, 0.0f, 0.0f });
	Entity mover = CreateBox(*scene, "Mover", { 10.0f, 0.0f, 0.0f });
	mover.GetComponent<RigidBodyComponent>().Type = RigidBodyType::Kinematic;
	scene->OnRuntimeStart();
	PhysicsWorld& physics = *scene->GetPhysicsWorld();

	physics.SetLinearVelocity(box, { 2.0f, 0.0f, 0.0f });
	Simulate(*scene, 1.0f);
	CHECK(box.GetTransform().Translation.x == doctest::Approx(2.0f).epsilon(0.05));

	physics.SetLinearVelocity(box, glm::vec3(0.0f));
	physics.AddImpulse(box, { 0.0f, 0.0f, 3.0f });
	CHECK(physics.GetLinearVelocity(box).z == doctest::Approx(3.0f).epsilon(0.01));

	// Kinematic bodies follow their entity's transform.
	mover.GetTransform().Translation = { 10.0f, 4.0f, 0.0f };
	Simulate(*scene, 0.1f);
	RaycastHit hit;
	REQUIRE(physics.Raycast({ 10.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f, hit));
	CHECK(hit.Point.y == doctest::Approx(4.5f).epsilon(0.01));

	// Bodies appear and disappear with entities during play.
	Entity spawned = CreateBox(*scene, "Spawned", { -5.0f, 0.0f, 0.0f });
	Simulate(*scene, 0.05f);
	CHECK(physics.HasBody(spawned));
	CHECK(physics.GetBodyCount() == 3);
	scene->DestroyEntity(spawned);
	Simulate(*scene, 0.05f);
	CHECK(physics.GetBodyCount() == 2);
	scene->OnRuntimeStop();
}

TEST_CASE("Compound and capsule colliders, locked rotation and child bodies")
{
	Ref<Scene> scene = CreateRef<Scene>();
	CreateGround(*scene);
	Entity parent = scene->CreateEntity("Parent");
	parent.GetTransform().Translation = { 3.0f, 0.0f, 0.0f };
	Entity capsule = scene->CreateChildEntity(parent, "Capsule");
	capsule.GetTransform().Translation = { 0.0f, 4.0f, 0.0f };
	auto& body = capsule.AddComponent<RigidBodyComponent>();
	body.LockRotationX = body.LockRotationY = body.LockRotationZ = true;
	capsule.AddComponent<CapsuleColliderComponent>();
	capsule.AddComponent<SphereColliderComponent>().Offset = { 0.0f, 1.5f, 0.0f };

	scene->OnRuntimeStart();
	Simulate(*scene, 3.0f);
	// Capsule bottom (half height 0.5 + radius 0.5) rests on the ground; world X is preserved through the parent.
	CHECK(scene->GetWorldPosition(capsule).y == doctest::Approx(1.0f).epsilon(0.03));
	CHECK(scene->GetWorldPosition(capsule).x == doctest::Approx(3.0f).epsilon(0.01));
	CHECK(glm::length(capsule.GetTransform().GetRotationEuler()) < 1e-3f);
	scene->OnRuntimeStop();
}

TEST_CASE("Rigid body settings edited during play apply to the live body")
{
	Ref<Scene> scene = CreateRef<Scene>();
	Entity box = CreateBox(*scene, "Box", { 0.0f, 10.0f, 0.0f });
	scene->OnRuntimeStart();

	// Without gravity the body hovers in place.
	box.GetComponent<RigidBodyComponent>().GravityScale = 0.0f;
	scene->GetPhysicsWorld()->ApplyBodySettings(box);
	Simulate(*scene, 0.5f);
	CHECK(box.GetTransform().Translation.y == doctest::Approx(10.0f));

	// Damping slows a moving body down.
	box.GetComponent<RigidBodyComponent>().LinearDamping = 5.0f;
	scene->GetPhysicsWorld()->ApplyBodySettings(box);
	scene->GetPhysicsWorld()->SetLinearVelocity(box, { 10.0f, 0.0f, 0.0f });
	Simulate(*scene, 0.5f);
	CHECK(scene->GetPhysicsWorld()->GetLinearVelocity(box).x < 2.0f);

	// Restoring gravity makes it fall again.
	box.GetComponent<RigidBodyComponent>().GravityScale = 1.0f;
	scene->GetPhysicsWorld()->ApplyBodySettings(box);
	Simulate(*scene, 0.5f);
	CHECK(box.GetTransform().Translation.y < 9.5f);
	scene->OnRuntimeStop();
}
