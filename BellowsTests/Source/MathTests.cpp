#include <doctest/doctest.h>

#include "Bellows/Math/AABB.h"
#include "Bellows/Math/Math.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Bellows;

namespace {

	bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
	{
		return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
	}

}

TEST_CASE("Transforms compose and decompose losslessly")
{
	const glm::vec3 translation(1.5f, -2.0f, 3.25f);
	const glm::quat rotation = glm::normalize(glm::quat(glm::radians(glm::vec3(30.0f, 45.0f, -60.0f))));
	const glm::vec3 scale(2.0f, 0.5f, 1.25f);

	const glm::mat4 transform = Math::ComposeTransform(translation, rotation, scale);
	glm::vec3 outTranslation(0.0f), outScale(1.0f);
	glm::quat outRotation(1.0f, 0.0f, 0.0f, 0.0f);
	REQUIRE(Math::DecomposeTransform(transform, outTranslation, outRotation, outScale));
	CHECK(Near(outTranslation, translation));
	CHECK(Near(outScale, scale));
	CHECK(std::abs(std::abs(glm::dot(outRotation, rotation)) - 1.0f) < 1e-4f);
}

TEST_CASE("Projection uses Y-up clip space with depth 0..1")
{
	const glm::mat4 projection = Math::PerspectiveProjection(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
	const glm::vec4 nearPoint = projection * glm::vec4(0.0f, 0.0f, -0.1f, 1.0f);
	const glm::vec4 farPoint = projection * glm::vec4(0.0f, 0.0f, -100.0f, 1.0f);
	CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f).epsilon(1e-4));

	const glm::vec4 above = projection * glm::vec4(0.0f, 1.0f, -5.0f, 1.0f);
	CHECK(above.y / above.w > 0.0f);
}

TEST_CASE("Screen rays and world-to-screen are inverse operations")
{
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = Math::PerspectiveProjection(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
	const glm::mat4 viewProjection = projection * view;
	const glm::vec2 viewport(800.0f, 800.0f);

	glm::vec2 screen(0.0f);
	REQUIRE(Math::WorldToScreenPoint(glm::vec3(0.0f), viewProjection, viewport, screen));
	CHECK(screen.x == doctest::Approx(400.0f));
	CHECK(screen.y == doctest::Approx(400.0f));

	// A point above the target appears above the screen center (smaller Y, top-left origin).
	REQUIRE(Math::WorldToScreenPoint(glm::vec3(0.0f, 1.0f, 0.0f), viewProjection, viewport, screen));
	CHECK(screen.y < 400.0f);

	glm::vec3 origin(0.0f), direction(0.0f);
	Math::ScreenPointToRay(glm::vec2(400.0f, 400.0f), viewport, glm::inverse(viewProjection), origin, direction);
	const glm::vec3 expected = glm::normalize(glm::vec3(0.0f) - glm::vec3(0.0f, 2.0f, 5.0f));
	CHECK(Near(direction, expected, 1e-3f));

	CHECK_FALSE(Math::WorldToScreenPoint(glm::vec3(0.0f, 2.0f, 10.0f), viewProjection, viewport, screen));
}

TEST_CASE("AABB expands and transforms")
{
	AABB box;
	CHECK_FALSE(box.IsValid());
	box.Expand(glm::vec3(-1.0f, 0.0f, 2.0f));
	box.Expand(glm::vec3(1.0f, 3.0f, -2.0f));
	REQUIRE(box.IsValid());
	CHECK(box.GetCenter() == glm::vec3(0.0f, 1.5f, 0.0f));
	CHECK(box.GetSize() == glm::vec3(2.0f, 3.0f, 4.0f));

	const AABB moved = box.Transformed(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)));
	CHECK(Near(moved.Min, glm::vec3(9.0f, 0.0f, -2.0f)));

	const AABB rotated = AABB(glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f)).Transformed(glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0)));
	CHECK(rotated.Max.x == doctest::Approx(std::sqrt(2.0f)).epsilon(1e-4));
}
