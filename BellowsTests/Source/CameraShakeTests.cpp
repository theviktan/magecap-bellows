#include <doctest/doctest.h>

#include "Bellows/Scene/CameraShake.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <glm/gtc/matrix_access.hpp>

using namespace Bellows;

TEST_CASE("A camera shake fades out over its length, and the stronger shake wins")
{
	CameraShake shake;
	CHECK_FALSE(shake.IsShaking());
	CHECK(shake.GetOffset() == glm::mat4(1.0f));

	shake.Start(1.0f, 1.0f);
	CHECK(shake.IsShaking());
	CHECK(shake.GetCurrentStrength() == doctest::Approx(1.0f));
	shake.Update(0.5f);
	CHECK(shake.GetCurrentStrength() == doctest::Approx(0.25f));

	// What is left (0.25) is weaker than 0.3, so the new shake takes over; 0.1 is weaker, so it's ignored.
	shake.Start(0.1f, 2.0f);
	CHECK(shake.GetCurrentStrength() == doctest::Approx(0.25f));
	shake.Start(0.3f, 2.0f);
	CHECK(shake.GetCurrentStrength() == doctest::Approx(0.3f));

	// Every offset stays within the strength's reach, and the shake moves the view frame to frame.
	float largestMove = 0.0f;
	glm::vec3 previous(0.0f);
	bool moved = false;
	for (int frame = 0; frame < 120; frame++)
	{
		const glm::mat4 offset = shake.GetOffset();
		const glm::vec3 move(offset[3]);
		largestMove = std::max(largestMove, glm::length(move));
		moved |= frame > 0 && move != previous;
		previous = move;
		shake.Update(1.0f / 60.0f);
	}
	CHECK(moved);
	CHECK(largestMove <= 0.3f * CameraShake::MetersPerStrength * std::sqrt(2.0f) + 1e-4f);
	CHECK(largestMove > 0.0f);

	shake.Update(10.0f);
	CHECK_FALSE(shake.IsShaking());
	CHECK(shake.GetOffset() == glm::mat4(1.0f));

	// Out-of-range values are held to the limits, and nothing starts from zero.
	shake.Start(100.0f, 100.0f);
	CHECK(shake.GetCurrentStrength() == doctest::Approx(CameraShake::MaxStrength));
	shake.Clear();
	shake.Start(0.0f, 1.0f);
	shake.Start(1.0f, 0.0f);
	shake.Start(std::numeric_limits<float>::quiet_NaN(), 1.0f);
	CHECK_FALSE(shake.IsShaking());
}

TEST_CASE("Camera shake moves only the rendered view, holds while paused, and ends with play")
{
	Scene scene("Shake");
	Entity camera = scene.CreateEntity("Camera");
	camera.AddComponent<CameraComponent>().Primary = true;
	camera.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 2.0f, 8.0f);
	scene.OnRuntimeStart();

	RenderCamera steady;
	REQUIRE(scene.GetPrimaryCamera(1.0f, steady, true));
	scene.GetCameraShake().Start(2.0f, 1.0f);
	scene.OnUpdateRuntime(Timestep(0.1f));

	RenderCamera shaken, plain;
	REQUIRE(scene.GetPrimaryCamera(1.0f, shaken, true));
	REQUIRE(scene.GetPrimaryCamera(1.0f, plain));
	CHECK(shaken.View != steady.View);
	CHECK(plain.View == steady.View);
	CHECK(camera.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 2.0f, 8.0f));
	CHECK(glm::distance(shaken.Position, glm::vec3(0.0f, 2.0f, 8.0f)) <= 2.0f * CameraShake::MetersPerStrength * std::sqrt(2.0f) + 1e-4f);

	// Paused: the view is steady and the shake keeps what it has left for later.
	scene.SetGamePaused(true);
	const float left = scene.GetCameraShake().GetCurrentStrength();
	scene.OnUpdateRuntime(Timestep(0.5f));
	RenderCamera paused;
	REQUIRE(scene.GetPrimaryCamera(1.0f, paused, true));
	CHECK(paused.View == steady.View);
	CHECK(scene.GetCameraShake().GetCurrentStrength() == doctest::Approx(left));
	scene.SetGamePaused(false);

	scene.OnRuntimeStop();
	CHECK_FALSE(scene.GetCameraShake().IsShaking());
}
