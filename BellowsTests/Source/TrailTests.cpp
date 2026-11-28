#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scene/TrailSystem.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace Bellows;

TEST_CASE("A trail leaves points as it moves, ages them out and keeps at most MaxPoints")
{
	TrailComponent trail;
	trail.Lifetime = 1.0f;
	trail.MinDistance = 0.5f;

	TrailSystem::Update(trail, glm::vec3(0.0f), 0.1f);
	REQUIRE(trail.RuntimePoints.size() == 1);
	// Too close to the last point: nothing new.
	TrailSystem::Update(trail, glm::vec3(0.3f, 0.0f, 0.0f), 0.1f);
	CHECK(trail.RuntimePoints.size() == 1);
	TrailSystem::Update(trail, glm::vec3(0.6f, 0.0f, 0.0f), 0.1f);
	CHECK(trail.RuntimePoints.size() == 2);
	CHECK(trail.RuntimePoints.back().Position == glm::vec3(0.6f, 0.0f, 0.0f));
	CHECK(trail.RuntimePoints.front().Age == doctest::Approx(0.2f));

	// Standing still, the oldest age out after Lifetime, newest last.
	for (int i = 0; i < 9; i++)
		TrailSystem::Update(trail, glm::vec3(0.6f, 0.0f, 0.0f), 0.1f);
	CHECK(trail.RuntimePoints.size() == 1);
	CHECK(trail.RuntimePoints[0].Position == glm::vec3(0.6f, 0.0f, 0.0f));

	// Not emitting: no new points, and the rest fade away.
	trail.Emitting = false;
	TrailSystem::Update(trail, glm::vec3(5.0f, 0.0f, 0.0f), 0.05f);		// The point left is 0.9 s old: not yet gone
	CHECK(trail.RuntimePoints.size() == 1);
	for (int i = 0; i < 12; i++)
		TrailSystem::Update(trail, glm::vec3(5.0f, 0.0f, 0.0f), 0.1f);
	CHECK(trail.RuntimePoints.empty());

	// The cap drops the oldest.
	trail.Emitting = true;
	trail.MaxPoints = 4;
	trail.Lifetime = 60.0f;
	for (int i = 0; i < 10; i++)
		TrailSystem::Update(trail, glm::vec3(static_cast<float>(i), 0.0f, 0.0f), 0.01f);
	REQUIRE(trail.RuntimePoints.size() == 4);
	CHECK(trail.RuntimePoints.front().Position.x == 6.0f);
	CHECK(trail.RuntimePoints.back().Position.x == 9.0f);

	// A bad position or time step is ignored, not recorded.
	TrailSystem::Update(trail, glm::vec3(std::numeric_limits<float>::quiet_NaN()), 0.01f);
	TrailSystem::Update(trail, glm::vec3(20.0f, 0.0f, 0.0f), std::numeric_limits<float>::infinity());
	for (const TrailPoint& point : trail.RuntimePoints)
	{
		CHECK(std::isfinite(point.Position.x));
		CHECK(std::isfinite(point.Age));
	}
	TrailSystem::Clear(trail);
	CHECK(trail.RuntimePoints.empty());
}

TEST_CASE("A trail's strip runs from the entity to the tail, sized, colored and textured along it")
{
	TrailComponent trail;
	trail.Lifetime = 1.0f;
	trail.StartWidth = 2.0f;
	trail.EndWidth = 0.0f;
	trail.StartColor = glm::vec4(1.0f, 0.5f, 0.0f, 1.0f);
	trail.EndColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	trail.Intensity = 2.0f;
	trail.RuntimePoints = { { glm::vec3(0.0f, 0.0f, 0.0f), 1.0f }, { glm::vec3(1.0f, 0.0f, 0.0f), 0.5f }, { glm::vec3(2.0f, 0.0f, 0.0f), 0.0f } };

	std::vector<StripPoint> strip;
	TrailSystem::BuildStrip(trail, glm::vec3(4.0f, 0.0f, 0.0f), strip);
	REQUIRE(strip.size() == 4);
	CHECK(strip[0].Position == glm::vec3(4.0f, 0.0f, 0.0f));		// The entity itself leads
	CHECK(strip[3].Position == glm::vec3(0.0f));					// The oldest at the tail
	CHECK(strip[0].Width == doctest::Approx(2.0f));
	CHECK(strip[2].Width == doctest::Approx(1.0f));					// Halfway through its life
	CHECK(strip[3].Width == doctest::Approx(0.0f));
	CHECK(strip[0].Color == glm::vec4(2.0f, 1.0f, 0.0f, 1.0f));		// Intensity on RGB only
	CHECK(strip[2].Color.a == doctest::Approx(0.5f));
	// Stretch: U from 0 at the head to 1 at the tail, by distance (4 m in all).
	CHECK(strip[0].U == 0.0f);
	CHECK(strip[1].U == doctest::Approx(0.5f));
	CHECK(strip[3].U == doctest::Approx(1.0f));

	trail.TextureMode = TrailTextureMode::Tile;
	trail.TileLength = 0.5f;
	TrailSystem::BuildStrip(trail, glm::vec3(4.0f, 0.0f, 0.0f), strip);
	CHECK(strip[3].U == doctest::Approx(8.0f));						// 4 m / 0.5 m

	trail.WidthCurve = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
	trail.ColorGradient = { { 0.0f, glm::vec4(1.0f) }, { 1.0f, glm::vec4(1.0f) } };
	TrailSystem::BuildStrip(trail, glm::vec3(4.0f, 0.0f, 0.0f), strip);
	CHECK(strip[0].Width == doctest::Approx(0.0f));
	CHECK(strip[2].Width == doctest::Approx(2.0f));
	CHECK(strip[3].Color == glm::vec4(2.0f, 2.0f, 2.0f, 1.0f));

	// Not emitting, it lets go of the entity; with fewer than two points there's nothing to draw.
	trail.Emitting = false;
	TrailSystem::BuildStrip(trail, glm::vec3(4.0f, 0.0f, 0.0f), strip);
	CHECK(strip.size() == 3);
	CHECK(strip[0].Position == glm::vec3(2.0f, 0.0f, 0.0f));
	trail.RuntimePoints.resize(1);
	TrailSystem::BuildStrip(trail, glm::vec3(4.0f, 0.0f, 0.0f), strip);
	CHECK(strip.empty());
}

TEST_CASE("Trails are clamped, saved and loaded, and run in the editor and in play")
{
	TrailComponent bad;
	bad.Lifetime = -1.0f;
	bad.MinDistance = std::numeric_limits<float>::quiet_NaN();
	bad.MaxPoints = 0;
	bad.StartWidth = 1e9f;
	bad.Blend = static_cast<ParticleBlend>(9);
	bad.TextureMode = static_cast<TrailTextureMode>(9);
	bad.TileLength = 0.0f;
	for (int i = 0; i < 10; i++)
		bad.WidthCurve.push_back({ 1.0f, 2.0f });
	bad.Clamp();
	CHECK(bad.Lifetime == TrailComponent::MinLifetime);
	CHECK(bad.MinDistance == TrailComponent().MinDistance);
	CHECK(bad.MaxPoints == 2);
	CHECK(bad.StartWidth == TrailComponent::MaxWidth);
	CHECK(bad.Blend == ParticleBlend::Additive);
	CHECK(bad.TextureMode == TrailTextureMode::Stretch);
	CHECK(bad.TileLength == TrailComponent::MinTileLength);
	CHECK(bad.WidthCurve.size() == ParticleEmitterComponent::MaxCurveKeys);

	Ref<Scene> edited = CreateRef<Scene>("Trails");
	Entity sword = edited->CreateEntity("Sword");
	TrailComponent& trail = sword.AddComponent<TrailComponent>();
	trail.Lifetime = 0.75f;
	trail.MinDistance = 0.02f;
	trail.MaxPoints = 64;
	trail.StartWidth = 0.4f;
	trail.EndWidth = 0.1f;
	trail.WidthCurve = { { 0.0f, 0.5f }, { 1.0f, 0.0f } };
	trail.StartColor = glm::vec4(0.25f, 0.5f, 1.0f, 1.0f);
	trail.ColorGradient = { { 0.0f, glm::vec4(1.0f) }, { 1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f) } };
	trail.Intensity = 3.0f;
	trail.Blend = ParticleBlend::Alpha;
	trail.TexturePath = "Textures/Slash.png";
	trail.TextureMode = TrailTextureMode::Tile;
	trail.TileLength = 0.25f;
	trail.Emitting = false;

	// The editor shows it when the entity moves.
	trail.Emitting = true;
	edited->OnUpdateEditor(Timestep(0.016f));
	sword.GetTransform().Translation = glm::vec3(1.0f, 0.0f, 0.0f);
	edited->OnUpdateEditor(Timestep(0.016f));
	CHECK(sword.GetComponent<TrailComponent>().RuntimePoints.size() == 2);

	// Play starts with none, records while the game runs and holds still while it's paused.
	Ref<Scene> game = Scene::Copy(edited);
	game->OnRuntimeStart();
	Entity gameSword = game->TryGetEntityByUUID(sword.GetUUID());
	REQUIRE(gameSword);
	CHECK(gameSword.GetComponent<TrailComponent>().RuntimePoints.empty());
	game->OnUpdateRuntime(Timestep(0.016f));
	gameSword.GetTransform().Translation = glm::vec3(3.0f, 0.0f, 0.0f);
	game->OnUpdateRuntime(Timestep(0.016f));
	CHECK(gameSword.GetComponent<TrailComponent>().RuntimePoints.size() == 2);
	game->SetGamePaused(true);
	gameSword.GetTransform().Translation = glm::vec3(5.0f, 0.0f, 0.0f);
	game->OnUpdateRuntime(Timestep(0.016f));
	CHECK(gameSword.GetComponent<TrailComponent>().RuntimePoints.size() == 2);
	game->OnRuntimeStop();

	trail.Emitting = false;
	const Json json = SceneSerializer::SerializeToJson(*edited);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity loadedSword = loaded.TryGetEntityByUUID(sword.GetUUID());
	REQUIRE(loadedSword);
	const TrailComponent& read = loadedSword.GetComponent<TrailComponent>();
	CHECK_FALSE(read.Emitting);
	CHECK(read.Lifetime == 0.75f);
	CHECK(read.MinDistance == 0.02f);
	CHECK(read.MaxPoints == 64);
	CHECK(read.StartWidth == 0.4f);
	CHECK(read.EndWidth == 0.1f);
	CHECK(read.WidthCurve == trail.WidthCurve);
	CHECK(read.StartColor == trail.StartColor);
	CHECK(read.ColorGradient == trail.ColorGradient);
	CHECK(read.Intensity == 3.0f);
	CHECK(read.Blend == ParticleBlend::Alpha);
	CHECK(read.TexturePath == "Textures/Slash.png");
	CHECK(read.TextureMode == TrailTextureMode::Tile);
	CHECK(read.TileLength == 0.25f);
	CHECK(read.RuntimePoints.empty());
	CHECK(SceneSerializer::SerializeToJson(loaded)["Entities"].dump() == json["Entities"].dump());

	// Unknown names and broken curve keys load as defaults.
	Json broken = json;
	for (Json& entity : broken["Entities"])
	{
		if (entity.contains("Trail"))
		{
			entity["Trail"]["TextureMode"] = "wrap";
			entity["Trail"]["Blend"] = "glow";
			entity["Trail"]["WidthCurve"] = Json::parse(R"([[0, 1], "x", [1]])");
		}
	}
	Scene fixed("Fixed");
	REQUIRE(SceneSerializer::DeserializeFromJson(fixed, broken));
	Entity fixedSword = fixed.TryGetEntityByUUID(sword.GetUUID());
	REQUIRE(fixedSword);
	CHECK(fixedSword.GetComponent<TrailComponent>().TextureMode == TrailTextureMode::Stretch);
	CHECK(fixedSword.GetComponent<TrailComponent>().Blend == ParticleBlend::Additive);
	CHECK(fixedSword.GetComponent<TrailComponent>().WidthCurve.size() == 1);
}

namespace {

	constexpr uint32_t c_Size = 96;

	RenderCamera FrontCamera()
	{
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 0.0f, 5.0f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
		return camera;
	}

	void PlainSettings(Scene& scene)
	{
		SceneRendererSettings& settings = scene.GetSettings().Rendering;
		settings.ToneMapper = Tonemapper::None;
		settings.BloomEnabled = false;
		settings.SSAOEnabled = false;
		settings.FXAAEnabled = false;
		settings.ShadowsEnabled = false;
		settings.Vignette = 0.0f;
	}

	Image Draw(SceneRenderer& renderer, Scene& scene)
	{
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera());
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	}

	bool IsRed(const glm::u8vec4& pixel)
	{
		return pixel.r > 150 && pixel.g < 60 && pixel.b < 60;
	}

}

TEST_CASE("[gpu] A trail draws as a strip along where its entity has been, soft at its edges")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	Scene scene("Trail");
	PlainSettings(scene);
	Entity comet = scene.CreateEntity("Comet");
	TrailComponent& trail = comet.AddComponent<TrailComponent>();
	trail.Lifetime = 100.0f;
	trail.MinDistance = 0.1f;
	trail.StartWidth = trail.EndWidth = 0.6f;
	trail.StartColor = trail.EndColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	trail.Blend = ParticleBlend::Alpha;
	// Across the view from left to right, a little at a time.
	for (int step = 0; step <= 20; step++)
	{
		comet.GetTransform().Translation = glm::vec3(-1.5f + 0.15f * static_cast<float>(step), 0.0f, 0.0f);
		scene.OnUpdateEditor(Timestep(0.016f));
	}
	const Image image = Draw(renderer, scene);
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "Trail_Strip.png");
	CHECK(renderer.GetStatistics().Particles > 10);

	// Red along a horizontal band through the middle, soft toward its top and bottom edges, and
	// nothing above or below it.
	uint32_t wide = 0;
	for (uint32_t x = 0; x < c_Size; x++)
		wide += IsRed(image.GetPixel(x, c_Size / 2)) ? 1 : 0;
	CHECK(wide > c_Size / 2);
	CHECK_FALSE(IsRed(image.GetPixel(c_Size / 2, 4)));
	CHECK_FALSE(IsRed(image.GetPixel(c_Size / 2, c_Size - 5)));
	const glm::u8vec4 middle = image.GetPixel(c_Size / 2, c_Size / 2);
	uint32_t edgeRow = c_Size / 2;
	while (edgeRow > 0 && IsRed(image.GetPixel(c_Size / 2, edgeRow)))
		edgeRow--;
	CHECK(edgeRow < c_Size / 2 - 2);		// Several rows tall
	CHECK(middle.r > image.GetPixel(c_Size / 2, edgeRow + 1).r - 1);

	// Cleared, it draws nothing.
	TrailSystem::Clear(comet.GetComponent<TrailComponent>());
	const Image cleared = Draw(renderer, scene);
	uint32_t red = 0;
	for (uint32_t x = 0; x < c_Size; x++)
		red += IsRed(cleared.GetPixel(x, c_Size / 2)) ? 1 : 0;
	CHECK(red == 0);

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
