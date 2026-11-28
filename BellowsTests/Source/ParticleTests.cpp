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
#include "Bellows/Scene/ParticleSystem.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Bellows;

namespace {

	const glm::vec3 c_NoGravity(0.0f);

	bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
	}

	// An emitter that sends nothing on its own, so a test decides what is born.
	ParticleEmitterComponent QuietEmitter()
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = 0.0f;
		emitter.Burst = 0;
		emitter.LifetimeVariation = 0.0f;
		emitter.SpeedVariation = 0.0f;
		ParticleSystem::Reset(emitter, 1);
		return emitter;
	}

	void Run(ParticleEmitterComponent& emitter, float seconds, int steps, const glm::mat4& transform = glm::mat4(1.0f), const glm::vec3& gravity = c_NoGravity)
	{
		for (int i = 0; i < steps; i++)
			ParticleSystem::Update(emitter, transform, gravity, seconds);
	}

}

TEST_CASE("A particle emitter sends Rate particles per second while playing")
{
	ParticleEmitterComponent emitter = QuietEmitter();
	emitter.Rate = 10.0f;
	emitter.Lifetime = 100.0f;
	Run(emitter, 0.1f, 10);
	CHECK(emitter.RuntimePlaying);
	CHECK(emitter.RuntimeParticles.size() == 10);

	// A fraction of a particle carries over to the next update rather than being lost.
	ParticleEmitterComponent slow = QuietEmitter();
	slow.Rate = 3.0f;
	slow.Lifetime = 100.0f;
	Run(slow, 0.1f, 20);
	CHECK(slow.RuntimeParticles.size() == 6);
}

TEST_CASE("A particle emitter's bursts, cycles, cap and lifetimes")
{
	SUBCASE("each cycle of a looping emitter starts with its burst")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 5;
		emitter.Duration = 1.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.1f, 1);
		CHECK(emitter.RuntimeParticles.size() == 5);
		Run(emitter, 0.1f, 10);
		CHECK(emitter.RuntimeParticles.size() == 10);
	}

	SUBCASE("a one-shot emitter stops after its duration")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Looping = false;
		emitter.Duration = 0.5f;
		emitter.Rate = 10.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.1f, 20);
		CHECK_FALSE(emitter.RuntimePlaying);
		CHECK(emitter.RuntimeParticles.size() == 5);
	}

	SUBCASE("no more than MaxParticles live at once")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 500;
		emitter.MaxParticles = 40;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.1f, 3);
		CHECK(emitter.RuntimeParticles.size() == 40);
	}

	SUBCASE("particles die at the end of their lifetime")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 8;
		emitter.Looping = false;
		emitter.Lifetime = 0.5f;
		Run(emitter, 0.1f, 1);
		REQUIRE(emitter.RuntimeParticles.size() == 8);
		Run(emitter, 0.1f, 4);
		CHECK(emitter.RuntimeParticles.size() == 8);
		Run(emitter, 0.1f, 1);
		CHECK(emitter.RuntimeParticles.empty());
	}

	SUBCASE("one that doesn't play on start sends nothing until played or asked")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.PlayOnStart = false;
		emitter.Rate = 50.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.1f, 5);
		CHECK_FALSE(emitter.RuntimePlaying);
		CHECK(emitter.RuntimeParticles.empty());
		ParticleSystem::Emit(emitter, 3);
		Run(emitter, 0.1f, 1);
		CHECK(emitter.RuntimeParticles.size() == 3);
		ParticleSystem::Play(emitter);
		Run(emitter, 0.1f, 1);
		CHECK(emitter.RuntimeParticles.size() == 8);
	}

	SUBCASE("stop keeps the live particles unless asked to clear them")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Rate = 100.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.1f, 1);
		const size_t alive = emitter.RuntimeParticles.size();
		REQUIRE(alive == 10);
		ParticleSystem::Stop(emitter, false);
		Run(emitter, 0.1f, 3);
		CHECK(emitter.RuntimeParticles.size() == alive);
		ParticleSystem::Stop(emitter, true);
		CHECK(emitter.RuntimeParticles.empty());
	}
}

TEST_CASE("Particles leave in the entity's cone, from its shape, and feel gravity and drag")
{
	SUBCASE("a spread of 0 sends them straight along the entity's up")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 20;
		emitter.SpreadAngle = 0.0f;
		emitter.Speed = 3.0f;
		emitter.Lifetime = 100.0f;
		// Turned so its up is the world's -X, and moved to (5, 0, 0).
		const glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f))
			* glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		Run(emitter, 0.0f, 1, transform);
		REQUIRE(emitter.RuntimeParticles.size() == 20);
		for (const Particle& particle : emitter.RuntimeParticles)
		{
			CHECK(Near(particle.Position, { 5.0f, 0.0f, 0.0f }, 1e-4f));
			CHECK(Near(particle.Velocity, { -3.0f, 0.0f, 0.0f }, 1e-4f));
		}
	}

	SUBCASE("a cone's directions stay inside its angle")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 200;
		emitter.SpreadAngle = 30.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.0f, 1);
		const float cosLimit = std::cos(glm::radians(30.0f)) - 1e-4f;
		bool allInside = true, anyOffAxis = false;
		for (const Particle& particle : emitter.RuntimeParticles)
		{
			const glm::vec3 direction = glm::normalize(particle.Velocity);
			allInside = allInside && direction.y >= cosLimit;
			anyOffAxis = anyOffAxis || direction.y < 0.99f;
		}
		CHECK(allInside);
		CHECK(anyOffAxis);
	}

	SUBCASE("sphere and box shapes")
	{
		ParticleEmitterComponent sphere = QuietEmitter();
		sphere.Burst = 200;
		sphere.Speed = 0.0f;
		sphere.Lifetime = 100.0f;
		sphere.Shape = ParticleShape::Sphere;
		sphere.ShapeSize = glm::vec3(2.0f);
		Run(sphere, 0.0f, 1);
		float farthest = 0.0f;
		for (const Particle& particle : sphere.RuntimeParticles)
			farthest = std::max(farthest, glm::length(particle.Position));
		CHECK(farthest <= 2.0f + 1e-4f);
		CHECK(farthest > 1.5f);

		ParticleEmitterComponent box = sphere;
		ParticleSystem::Reset(box, 2);
		box.Shape = ParticleShape::Box;
		box.ShapeSize = glm::vec3(1.0f, 0.0f, 3.0f);
		Run(box, 0.0f, 1);
		glm::vec3 extent(0.0f);
		for (const Particle& particle : box.RuntimeParticles)
			extent = glm::max(extent, glm::abs(particle.Position));
		CHECK(extent.x <= 1.0f);
		CHECK(extent.y == 0.0f);
		CHECK(extent.z <= 3.0f);
		CHECK(extent.z > 2.0f);
	}

	SUBCASE("gravity pulls in proportion to its scale, and drag slows")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 1;
		emitter.Speed = 0.0f;
		emitter.Lifetime = 100.0f;
		emitter.GravityScale = 0.5f;
		Run(emitter, 0.0f, 1);
		Run(emitter, 0.1f, 10, glm::mat4(1.0f), glm::vec3(0.0f, -10.0f, 0.0f));
		REQUIRE(emitter.RuntimeParticles.size() == 1);
		CHECK(emitter.RuntimeParticles[0].Velocity.y == doctest::Approx(-5.0f));

		ParticleEmitterComponent dragged = QuietEmitter();
		dragged.Burst = 1;
		dragged.Speed = 4.0f;
		dragged.SpreadAngle = 0.0f;
		dragged.Lifetime = 100.0f;
		dragged.Drag = 1.0f;
		Run(dragged, 0.0f, 1);
		Run(dragged, 0.1f, 10);
		CHECK(dragged.RuntimeParticles[0].Velocity.y == doctest::Approx(4.0f * std::exp(-1.0f)).epsilon(0.001));
	}

	SUBCASE("local-space particles stay in the emitter's space")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.WorldSpace = false;
		emitter.Burst = 1;
		emitter.SpreadAngle = 0.0f;
		emitter.Speed = 1.0f;
		emitter.Lifetime = 100.0f;
		const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
		Run(emitter, 0.0f, 1, moved);
		CHECK(Near(emitter.RuntimeParticles[0].Position, glm::vec3(0.0f), 1e-5f));
		CHECK(Near(emitter.RuntimeParticles[0].Velocity, { 0.0f, 1.0f, 0.0f }, 1e-5f));
	}
}

TEST_CASE("Particle emitters are repeatable, sized and colored over their lives, and clamped")
{
	SUBCASE("the same seed gives the same particles; another seed others")
	{
		auto positions = [](uint64_t seed)
		{
			ParticleEmitterComponent emitter = QuietEmitter();
			ParticleSystem::Reset(emitter, seed);
			emitter.Burst = 10;
			emitter.Shape = ParticleShape::Sphere;
			emitter.Lifetime = 100.0f;
			Run(emitter, 0.0f, 1);
			std::vector<glm::vec3> result;
			for (const Particle& particle : emitter.RuntimeParticles)
				result.push_back(particle.Position);
			return result;
		};
		CHECK(positions(42) == positions(42));
		CHECK(positions(42) != positions(43));
	}

	SUBCASE("size and color go from start to end")
	{
		ParticleEmitterComponent emitter;
		emitter.StartSize = 1.0f;
		emitter.EndSize = 0.0f;
		emitter.StartColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		emitter.EndColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		emitter.Intensity = 2.0f;
		Particle particle;
		particle.Lifetime = 2.0f;
		particle.Age = 0.5f;
		CHECK(ParticleSystem::GetSize(emitter, particle) == doctest::Approx(0.75f));
		const glm::vec4 color = ParticleSystem::GetColor(emitter, particle);
		CHECK(Near(glm::vec3(color), { 1.5f, 0.0f, 0.5f }, 1e-5f));
		CHECK(color.a == doctest::Approx(0.75f));
	}

	SUBCASE("Clamp holds values to their ranges and NaN to defaults")
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = std::nanf("");
		emitter.MaxParticles = 1000000;
		emitter.SpreadAngle = 400.0f;
		emitter.StartColor = glm::vec4(2.0f, -1.0f, 0.5f, 1.0f);
		emitter.Shape = static_cast<ParticleShape>(9);
		emitter.Clamp();
		CHECK(emitter.Rate == ParticleEmitterComponent().Rate);
		CHECK(emitter.MaxParticles == ParticleEmitterComponent::MaxMaxParticles);
		CHECK(emitter.SpreadAngle == 180.0f);
		CHECK(emitter.StartColor == glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
		CHECK(emitter.Shape == ParticleShape::Point);
	}

	SUBCASE("shape and blend names")
	{
		for (ParticleShape shape : { ParticleShape::Point, ParticleShape::Sphere, ParticleShape::Box })
			CHECK(ParticleShapeFromString(ParticleShapeToString(shape)) == shape);
		for (ParticleBlend blend : { ParticleBlend::Additive, ParticleBlend::Alpha })
			CHECK(ParticleBlendFromString(ParticleBlendToString(blend)) == blend);
		CHECK_FALSE(ParticleShapeFromString("Sphere").has_value());
		CHECK_FALSE(ParticleBlendFromString("glow").has_value());
	}

	SUBCASE("a long hitch moves them a quarter second at most")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.Burst = 1;
		emitter.SpreadAngle = 0.0f;
		emitter.Speed = 1.0f;
		emitter.Lifetime = 100.0f;
		Run(emitter, 0.0f, 1);
		Run(emitter, 30.0f, 1);
		CHECK(emitter.RuntimeParticles[0].Position.y == doctest::Approx(0.25f));
	}
}

TEST_CASE("A scene runs its emitters: fresh at play, held while the game is paused, and saved")
{
	Ref<Scene> edited = CreateRef<Scene>("Particles");
	Scene& scene = *edited;
	Entity fire = scene.CreateEntity("Fire");
	ParticleEmitterComponent& emitter = fire.AddComponent<ParticleEmitterComponent>();
	emitter.Rate = 20.0f;
	emitter.Lifetime = 100.0f;
	emitter.Shape = ParticleShape::Box;
	emitter.Blend = ParticleBlend::Alpha;
	emitter.TexturePath = "Textures/Smoke.png";
	emitter.StartColor = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);

	// The editor previews it...
	scene.OnUpdateEditor(Timestep(0.1f));
	scene.OnUpdateEditor(Timestep(0.1f));
	CHECK(fire.GetComponent<ParticleEmitterComponent>().RuntimeParticles.size() == 4);

	// ...but play starts it afresh.
	Ref<Scene> game = Scene::Copy(edited);
	game->OnRuntimeStart();
	Entity gameFire = game->TryGetEntityByUUID(fire.GetUUID());
	REQUIRE(gameFire);
	CHECK(gameFire.GetComponent<ParticleEmitterComponent>().RuntimeParticles.empty());
	game->OnUpdateRuntime(Timestep(0.1f));
	CHECK(gameFire.GetComponent<ParticleEmitterComponent>().RuntimeParticles.size() == 2);
	game->SetGamePaused(true);
	game->OnUpdateRuntime(Timestep(0.1f));
	CHECK(gameFire.GetComponent<ParticleEmitterComponent>().RuntimeParticles.size() == 2);
	game->OnRuntimeStop();

	// One a script adds and plays in the same frame keeps playing, and a duplicate gets its own randomness.
	Ref<Scene> second = Scene::Copy(edited);
	second->OnRuntimeStart();
	Entity spark = second->CreateEntity("Spark");
	ParticleEmitterComponent& added = spark.AddComponent<ParticleEmitterComponent>();
	added.PlayOnStart = false;
	added.Rate = 0.0f;
	added.Burst = 3;
	added.Lifetime = 100.0f;
	ParticleSystem::Play(added);
	second->OnUpdateRuntime(Timestep(0.0f));
	CHECK(spark.GetComponent<ParticleEmitterComponent>().RuntimeParticles.size() == 3);
	CHECK(spark.GetComponent<ParticleEmitterComponent>().RuntimePlaying);
	second->OnRuntimeStop();

	Entity twin = scene.DuplicateEntity(fire);
	scene.OnUpdateEditor(Timestep(0.1f));
	CHECK(twin.GetComponent<ParticleEmitterComponent>().RuntimeSeed == static_cast<uint64_t>(twin.GetUUID()));
	CHECK(twin.GetComponent<ParticleEmitterComponent>().RuntimeRandom != fire.GetComponent<ParticleEmitterComponent>().RuntimeRandom);
	scene.DestroyEntity(twin);

	// Saved and loaded, every setting comes back and nothing of the run does.
	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity loadedFire = loaded.TryGetEntityByUUID(fire.GetUUID());
	REQUIRE(loadedFire);
	const ParticleEmitterComponent& read = loadedFire.GetComponent<ParticleEmitterComponent>();
	CHECK(read.Rate == 20.0f);
	CHECK(read.Shape == ParticleShape::Box);
	CHECK(read.Blend == ParticleBlend::Alpha);
	CHECK(read.TexturePath == "Textures/Smoke.png");
	CHECK(read.StartColor == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
	CHECK(read.RuntimeParticles.empty());
	CHECK(SceneSerializer::SerializeToJson(loaded)["Entities"].dump() == json["Entities"].dump());

	// A file with a bad shape or out-of-range numbers loads with defaults and limits.
	Json broken = json;
	for (Json& entity : broken["Entities"])
	{
		if (entity.contains("ParticleEmitter"))
		{
			entity["ParticleEmitter"]["Shape"] = "donut";
			entity["ParticleEmitter"]["MaxParticles"] = 99999999;
		}
	}
	Scene fixed("Fixed");
	REQUIRE(SceneSerializer::DeserializeFromJson(fixed, broken));
	Entity fixedFire = fixed.TryGetEntityByUUID(fire.GetUUID());
	REQUIRE(fixedFire);
	const ParticleEmitterComponent& clamped = fixedFire.GetComponent<ParticleEmitterComponent>();
	CHECK(clamped.Shape == ParticleShape::Point);
	CHECK(clamped.MaxParticles == ParticleEmitterComponent::MaxMaxParticles);
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

	// A scene drawn plainly: no tonemapping curve, bloom or post effects, so a pixel shows its color.
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

	ParticleEmitterComponent& AddStillEmitter(Scene& scene, const char* name, uint32_t count, const glm::vec4& color, ParticleBlend blend)
	{
		ParticleEmitterComponent& emitter = scene.CreateEntity(name).AddComponent<ParticleEmitterComponent>();
		emitter.Rate = 0.0f;
		emitter.Burst = count;
		emitter.Looping = false;
		emitter.Speed = 0.0f;
		emitter.Lifetime = 100.0f;
		emitter.LifetimeVariation = 0.0f;
		emitter.StartSize = emitter.EndSize = 1.5f;
		emitter.StartColor = emitter.EndColor = color;
		emitter.Blend = blend;
		return emitter;
	}

	Image Draw(SceneRenderer& renderer, Scene& scene)
	{
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera());
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	}

	uint32_t CountRedPixels(const Image& image)
	{
		uint32_t red = 0;
		for (uint32_t y = 0; y < c_Size; y++)
		{
			for (uint32_t x = 0; x < c_Size; x++)
			{
				const glm::u8vec4 pixel = image.GetPixel(x, y);
				red += pixel.r > 150 && pixel.g < 60 && pixel.b < 60 ? 1 : 0;
			}
		}
		return red;
	}

}

TEST_CASE("[gpu] Particles draw as camera-facing dots, add up when additive, and hide behind geometry")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	SUBCASE("an alpha emitter's dot, soft at its edge")
	{
		Scene empty("Empty");
		PlainSettings(empty);
		const glm::u8vec4 background = Draw(renderer, empty).GetPixel(2, 2);

		Scene scene("Alpha");
		PlainSettings(scene);
		AddStillEmitter(scene, "Dot", 1, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), ParticleBlend::Alpha);
		scene.OnUpdateEditor(Timestep(0.0f));
		const Image image = Draw(renderer, scene);
		CHECK(renderer.GetStatistics().Particles == 1);
		const glm::u8vec4 center = image.GetPixel(c_Size / 2, c_Size / 2);
		CHECK(center.r > 240);
		CHECK(center.g < 10);
		// Fully red at the center, fading toward the dot's edge, and the corner untouched.
		const uint32_t red = CountRedPixels(image);
		CHECK(red > 50);
		CHECK(red < c_Size * c_Size / 2);
		CHECK(image.GetPixel(2, 2) == background);
		image.SavePNG(BellowsTests::GetTestOutputDirectory() / "Particles_AlphaDot.png");
	}

	SUBCASE("additive particles in the same spot add their light")
	{
		Scene one("One");
		PlainSettings(one);
		AddStillEmitter(one, "Dim", 1, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), ParticleBlend::Additive);
		one.OnUpdateEditor(Timestep(0.0f));
		const uint8_t single = Draw(renderer, one).GetPixel(c_Size / 2, c_Size / 2).r;

		Scene two("Two");
		PlainSettings(two);
		AddStillEmitter(two, "Dim", 2, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), ParticleBlend::Additive);
		two.OnUpdateEditor(Timestep(0.0f));
		const uint8_t doubled = Draw(renderer, two).GetPixel(c_Size / 2, c_Size / 2).r;
		CHECK(single > 20);
		CHECK(doubled > single + 20);
	}

	SUBCASE("a wall in front hides them, and a hidden group isn't drawn in the editor's view")
	{
		Scene scene("Wall");
		PlainSettings(scene);
		AddStillEmitter(scene, "Dot", 1, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), ParticleBlend::Alpha);
		scene.OnUpdateEditor(Timestep(0.0f));
		REQUIRE(CountRedPixels(Draw(renderer, scene)) > 50);

		SceneGroup& group = scene.CreateGroup("Effects");
		const UUID groupID = group.ID;
		Entity dot = scene.FindEntityByName("Dot");
		REQUIRE(dot);
		scene.SetEntityGroup(dot, groupID);
		scene.FindGroup(groupID)->HiddenInEditor = true;
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera(), {}, true);
		CHECK(CountRedPixels(Renderer::ReadbackTexture(renderer.GetFinalImage())) == 0);
		scene.FindGroup(groupID)->HiddenInEditor = false;

		Entity wall = scene.CreateEntity("Wall");
		wall.AddComponent<MeshRendererComponent>().MeshPath = "Builtin/Cube";
		wall.GetTransform().Translation = glm::vec3(0.0f, 0.0f, 2.0f);
		wall.GetTransform().Scale = glm::vec3(4.0f, 4.0f, 0.2f);
		CHECK(CountRedPixels(Draw(renderer, scene)) == 0);
	}

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("Particle size curves and color gradients shape a particle over its life")
{
	using ParticleSystem::EvaluateCurve;
	using ParticleSystem::EvaluateGradient;
	CHECK(EvaluateCurve({}, 0.5f, 7.0f) == 7.0f);
	const std::vector<ParticleCurveKey> bump { { 0.2f, 0.0f }, { 0.5f, 2.0f }, { 1.0f, 1.0f } };
	CHECK(EvaluateCurve(bump, 0.0f, 9.0f) == doctest::Approx(0.0f));		// Level before the first key
	CHECK(EvaluateCurve(bump, 0.35f, 9.0f) == doctest::Approx(1.0f));
	CHECK(EvaluateCurve(bump, 0.75f, 9.0f) == doctest::Approx(1.5f));
	CHECK(EvaluateCurve(bump, 1.0f, 9.0f) == doctest::Approx(1.0f));
	// Two keys at the same time: a step, not a division by zero.
	const std::vector<ParticleCurveKey> step { { 0.5f, 0.0f }, { 0.5f, 4.0f } };
	CHECK(EvaluateCurve(step, 0.5f, 9.0f) == doctest::Approx(0.0f));
	CHECK(EvaluateCurve(step, 0.6f, 9.0f) == doctest::Approx(4.0f));

	const std::vector<ParticleColorKey> flash { { 0.0f, glm::vec4(1.0f) }, { 0.5f, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) }, { 1.0f, glm::vec4(0.0f, 0.0f, 1.0f, 0.0f) } };
	CHECK(EvaluateGradient({}, 0.5f, glm::vec4(0.25f)) == glm::vec4(0.25f));
	CHECK(Near(glm::vec3(EvaluateGradient(flash, 0.25f, glm::vec4(0.0f))), glm::vec3(0.5f, 0.5f, 1.0f), 1e-5f));
	CHECK(EvaluateGradient(flash, 0.75f, glm::vec4(0.0f)).a == doctest::Approx(0.5f));

	ParticleEmitterComponent emitter = QuietEmitter();
	emitter.StartSize = 2.0f;
	emitter.EndSize = 100.0f;		// Not used once there's a curve
	emitter.SizeCurve = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
	emitter.ColorGradient = flash;
	emitter.Intensity = 2.0f;
	Particle particle;
	particle.Lifetime = 2.0f;
	particle.Age = 0.5f;
	CHECK(ParticleSystem::GetSize(emitter, particle) == doctest::Approx(1.0f));
	particle.Age = 1.0f;
	CHECK(ParticleSystem::GetSize(emitter, particle) == doctest::Approx(2.0f));
	CHECK(ParticleSystem::GetColor(emitter, particle) == glm::vec4(0.0f, 0.0f, 2.0f, 1.0f));	// Intensity on RGB, not alpha
	emitter.SizeCurve.clear();
	emitter.ColorGradient.clear();
	CHECK(ParticleSystem::GetSize(emitter, particle) == doctest::Approx(51.0f));				// Back to start to end
}

TEST_CASE("New particle settings are held to their ranges")
{
	ParticleEmitterComponent emitter;
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (int i = 0; i < 12; i++)
		emitter.SizeCurve.push_back({ 1.0f - static_cast<float>(i) * 0.05f, 5000.0f });
	emitter.SizeCurve[0].Time = nan;
	emitter.ColorGradient = { { 0.9f, glm::vec4(2.0f, -1.0f, nan, 0.5f) }, { 0.1f, glm::vec4(1.0f) } };
	emitter.Facing = static_cast<ParticleFacing>(77);
	emitter.Shape = static_cast<ParticleShape>(42);
	emitter.FlipbookColumns = 0;
	emitter.FlipbookRows = 500;
	emitter.FlipbookFps = -3.0f;
	emitter.SpinSpeed = 1e9f;
	emitter.RotationVariation = nan;
	emitter.NoiseStrength = -1.0f;
	emitter.Clamp();

	CHECK(emitter.SizeCurve.size() == ParticleEmitterComponent::MaxCurveKeys);
	CHECK(std::is_sorted(emitter.SizeCurve.begin(), emitter.SizeCurve.end(), [](const ParticleCurveKey& a, const ParticleCurveKey& b) { return a.Time < b.Time; }));
	for (const ParticleCurveKey& key : emitter.SizeCurve)
	{
		CHECK(key.Time >= 0.0f);
		CHECK(key.Time <= 1.0f);
		CHECK(key.Value == ParticleEmitterComponent::MaxSize);
	}
	REQUIRE(emitter.ColorGradient.size() == 2);
	CHECK(emitter.ColorGradient[0].Time == 0.1f);
	CHECK(emitter.ColorGradient[1].Color == glm::vec4(1.0f, 0.0f, 1.0f, 0.5f));
	CHECK(emitter.Facing == ParticleFacing::Camera);
	CHECK(emitter.Shape == ParticleShape::Point);
	CHECK(emitter.FlipbookColumns == 1);
	CHECK(emitter.FlipbookRows == ParticleEmitterComponent::MaxFlipbookFrames);
	CHECK(emitter.FlipbookFps == 0.0f);
	CHECK(emitter.SpinSpeed == ParticleEmitterComponent::MaxSpin);
	CHECK(emitter.RotationVariation == 0.0f);
	CHECK(emitter.NoiseStrength == 0.0f);
}

TEST_CASE("Rings, lines and outward particles; spin, swirl and flipbook frames")
{
	SUBCASE("a ring's particles start on its circle around +Y, and a line's along X")
	{
		ParticleEmitterComponent ring = QuietEmitter();
		ring.Shape = ParticleShape::Ring;
		ring.ShapeSize = glm::vec3(2.0f);
		ring.Speed = 0.0f;
		ring.Lifetime = 100.0f;
		ParticleSystem::Emit(ring, 200);
		Run(ring, 0.0f, 1);
		REQUIRE(ring.RuntimeParticles.size() == 200);
		float minimumX = 10.0f, maximumX = -10.0f;
		for (const Particle& particle : ring.RuntimeParticles)
		{
			CHECK(glm::length(particle.Position) == doctest::Approx(2.0f).epsilon(1e-4));
			CHECK(particle.Position.y == doctest::Approx(0.0f));
			minimumX = std::min(minimumX, particle.Position.x);
			maximumX = std::max(maximumX, particle.Position.x);
		}
		CHECK(minimumX < -1.8f);		// All the way round
		CHECK(maximumX > 1.8f);

		ParticleEmitterComponent line = ring;
		line.RuntimeParticles.clear();
		line.Shape = ParticleShape::Line;
		line.ShapeSize.x = 3.0f;
		ParticleSystem::Emit(line, 200);
		Run(line, 0.0f, 1);
		for (const Particle& particle : line.RuntimeParticles)
		{
			CHECK(std::abs(particle.Position.x) <= 1.5f);
			CHECK(particle.Position.y == 0.0f);
			CHECK(particle.Position.z == 0.0f);
		}
	}

	SUBCASE("outward particles leave away from the center")
	{
		ParticleEmitterComponent burst = QuietEmitter();
		burst.Outward = true;
		burst.SpreadAngle = 0.0f;
		burst.Shape = ParticleShape::Sphere;
		burst.ShapeSize = glm::vec3(1.0f);
		burst.Speed = 3.0f;
		burst.Lifetime = 100.0f;
		ParticleSystem::Emit(burst, 100);
		Run(burst, 0.0f, 1);
		int down = 0;
		for (const Particle& particle : burst.RuntimeParticles)
		{
			CHECK(glm::length(particle.Velocity) == doctest::Approx(3.0f).epsilon(1e-4));
			if (glm::length(particle.Position) > 1e-3f)
				CHECK(glm::dot(glm::normalize(particle.Velocity), glm::normalize(particle.Position)) > 0.999f);
			down += particle.Velocity.y < 0.0f ? 1 : 0;
		}
		CHECK(down > 20);		// Every way, not only up

		// A ring's go out flat, and a point's go every way.
		ParticleEmitterComponent shockwave = burst;
		shockwave.RuntimeParticles.clear();
		shockwave.Shape = ParticleShape::Ring;
		ParticleSystem::Emit(shockwave, 50);
		Run(shockwave, 0.0f, 1);
		for (const Particle& particle : shockwave.RuntimeParticles)
			CHECK(std::abs(particle.Velocity.y) < 1e-4f);
		ParticleEmitterComponent point = burst;
		point.RuntimeParticles.clear();
		point.Shape = ParticleShape::Point;
		ParticleSystem::Emit(point, 100);
		Run(point, 0.0f, 1);
		glm::vec3 average(0.0f);
		for (const Particle& particle : point.RuntimeParticles)
			average += particle.Velocity / 100.0f;
		CHECK(glm::length(average) < 1.0f);
	}

	SUBCASE("rotation starts where it's set and spins")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		emitter.StartRotation = 90.0f;
		emitter.SpinSpeed = 180.0f;
		emitter.Lifetime = 100.0f;
		ParticleSystem::Emit(emitter, 1);
		Run(emitter, 0.0f, 1);
		REQUIRE(emitter.RuntimeParticles.size() == 1);
		CHECK(emitter.RuntimeParticles[0].Rotation == doctest::Approx(glm::half_pi<float>()));
		Run(emitter, 0.25f, 2);
		CHECK(emitter.RuntimeParticles[0].Rotation == doctest::Approx(glm::pi<float>()).epsilon(1e-4));

		ParticleEmitterComponent varied = QuietEmitter();
		varied.RotationVariation = 180.0f;
		varied.Lifetime = 100.0f;
		ParticleSystem::Emit(varied, 50);
		Run(varied, 0.0f, 1);
		float smallest = 10.0f, largest = -10.0f;
		for (const Particle& particle : varied.RuntimeParticles)
		{
			smallest = std::min(smallest, particle.Rotation);
			largest = std::max(largest, particle.Rotation);
		}
		CHECK(largest - smallest > 3.0f);
	}

	SUBCASE("swirl noise bends paths, the same way every run")
	{
		auto run = [](float strength)
		{
			ParticleEmitterComponent emitter = QuietEmitter();
			emitter.SpreadAngle = 0.0f;
			emitter.Speed = 1.0f;
			emitter.Lifetime = 100.0f;
			emitter.NoiseStrength = strength;
			ParticleSystem::Emit(emitter, 1);
			Run(emitter, 0.05f, 40);
			return emitter.RuntimeParticles.at(0).Position;
		};
		const glm::vec3 straight = run(0.0f);
		CHECK(Near(straight, glm::vec3(0.0f, 1.95f, 0.0f), 1e-3f));
		const glm::vec3 swirled = run(4.0f);
		CHECK(glm::length(glm::vec2(swirled.x, swirled.z)) > 0.05f);
		CHECK(Near(swirled, run(4.0f), 1e-6f));
	}

	SUBCASE("flipbook frames go across then down, over the life or at a frame rate")
	{
		ParticleEmitterComponent emitter = QuietEmitter();
		Particle particle;
		particle.Lifetime = 1.0f;
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
		emitter.FlipbookColumns = 2;
		emitter.FlipbookRows = 2;
		particle.Age = 0.0f;
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.0f, 0.0f, 0.5f, 0.5f));
		particle.Age = 0.3f;
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.5f, 0.0f, 1.0f, 0.5f));
		particle.Age = 0.6f;
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.0f, 0.5f, 0.5f, 1.0f));
		particle.Age = 1.0f;
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));	// The last frame, not past it
		emitter.FlipbookFps = 10.0f;
		particle.Age = 0.55f;		// Frame 5 of a looping 4: frame 1
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.5f, 0.0f, 1.0f, 0.5f));
		emitter.FlipbookRandomStart = true;
		particle.Age = 0.0f;
		particle.Random = 0.8f;		// Starts on frame 3
		CHECK(ParticleSystem::GetFrameRect(emitter, particle) == glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
	}
}

TEST_CASE("The new particle settings are saved and loaded, and broken curves are skipped")
{
	Scene scene("Effects");
	Entity portal = scene.CreateEntity("Portal");
	ParticleEmitterComponent& emitter = portal.AddComponent<ParticleEmitterComponent>();
	emitter.Shape = ParticleShape::Ring;
	emitter.Outward = true;
	emitter.Facing = ParticleFacing::Stretched;
	emitter.StretchAmount = 0.2f;
	emitter.StartRotation = 45.0f;
	emitter.RotationVariation = 10.0f;
	emitter.SpinSpeed = -90.0f;
	emitter.SpinVariation = 30.0f;
	emitter.NoiseStrength = 2.0f;
	emitter.NoiseFrequency = 0.5f;
	emitter.NoiseSpeed = 1.5f;
	emitter.TexturePath = "Textures/Sparkle.png";
	emitter.FlipbookColumns = 4;
	emitter.FlipbookRows = 2;
	emitter.FlipbookFps = 24.0f;
	emitter.FlipbookRandomStart = true;
	emitter.SizeCurve = { { 0.0f, 0.0f }, { 0.25f, 1.5f }, { 1.0f, 0.0f } };
	emitter.ColorGradient = { { 0.0f, glm::vec4(1.0f) }, { 1.0f, glm::vec4(0.25f, 0.5f, 1.0f, 0.0f) } };

	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity loadedPortal = loaded.TryGetEntityByUUID(portal.GetUUID());
	REQUIRE(loadedPortal);
	const ParticleEmitterComponent& read = loadedPortal.GetComponent<ParticleEmitterComponent>();
	CHECK(read.Shape == ParticleShape::Ring);
	CHECK(read.Outward);
	CHECK(read.Facing == ParticleFacing::Stretched);
	CHECK(read.StretchAmount == 0.2f);
	CHECK(read.StartRotation == 45.0f);
	CHECK(read.RotationVariation == 10.0f);
	CHECK(read.SpinSpeed == -90.0f);
	CHECK(read.SpinVariation == 30.0f);
	CHECK(read.NoiseStrength == 2.0f);
	CHECK(read.NoiseFrequency == 0.5f);
	CHECK(read.NoiseSpeed == 1.5f);
	CHECK(read.FlipbookColumns == 4);
	CHECK(read.FlipbookRows == 2);
	CHECK(read.FlipbookFps == 24.0f);
	CHECK(read.FlipbookRandomStart);
	CHECK(read.SizeCurve == emitter.SizeCurve);
	CHECK(read.ColorGradient == emitter.ColorGradient);
	CHECK(SceneSerializer::SerializeToJson(loaded)["Entities"].dump() == json["Entities"].dump());

	// Keys that aren't [time, value] or [time, r, g, b, a] are skipped; the rest load.
	Json broken = json;
	for (Json& entity : broken["Entities"])
	{
		if (entity.contains("ParticleEmitter"))
		{
			entity["ParticleEmitter"]["SizeCurve"] = Json::parse(R"([[0, 1], "big", [0.5], [0.5, 2, 3], [1, 0.5], {"t": 1}])");
			entity["ParticleEmitter"]["ColorGradient"] = Json::parse(R"([[0, 1, 1, 1, 1], [0.5, 1, 1, 1], 7, [1, 0, 0, 0, "x"]])");
			entity["ParticleEmitter"]["Facing"] = "sideways";
		}
	}
	Scene fixed("Fixed");
	REQUIRE(SceneSerializer::DeserializeFromJson(fixed, broken));
	Entity fixedPortal = fixed.TryGetEntityByUUID(portal.GetUUID());
	REQUIRE(fixedPortal);
	const ParticleEmitterComponent& kept = fixedPortal.GetComponent<ParticleEmitterComponent>();
	CHECK(kept.SizeCurve == std::vector<ParticleCurveKey> { { 0.0f, 1.0f }, { 1.0f, 0.5f } });
	CHECK(kept.ColorGradient.size() == 1);
	CHECK(kept.Facing == ParticleFacing::Camera);

	// An old scene without them keeps the defaults.
	Json old = json;
	for (Json& entity : old["Entities"])
	{
		if (entity.contains("ParticleEmitter"))
		{
			for (const char* key : { "Outward", "Facing", "SizeCurve", "ColorGradient", "FlipbookColumns", "NoiseStrength", "SpinSpeed" })
				entity["ParticleEmitter"].erase(key);
		}
	}
	Scene older("Older");
	REQUIRE(SceneSerializer::DeserializeFromJson(older, old));
	Entity olderPortal = older.TryGetEntityByUUID(portal.GetUUID());
	REQUIRE(olderPortal);
	const ParticleEmitterComponent& defaults = olderPortal.GetComponent<ParticleEmitterComponent>();
	CHECK_FALSE(defaults.Outward);
	CHECK(defaults.Facing == ParticleFacing::Camera);
	CHECK(defaults.SizeCurve.empty());
	CHECK(defaults.ColorGradient.empty());
	CHECK(defaults.FlipbookColumns == 1);
	CHECK(defaults.NoiseStrength == 0.0f);
	CHECK(defaults.SpinSpeed == 0.0f);
}

TEST_CASE("[gpu] Stretched particles streak along their motion, and flat ones lie across the emitter's up")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	// The widest and tallest runs of red pixels in the image.
	auto redExtent = [](const Image& image)
	{
		uint32_t minX = c_Size, maxX = 0, minY = c_Size, maxY = 0;
		for (uint32_t y = 0; y < c_Size; y++)
		{
			for (uint32_t x = 0; x < c_Size; x++)
			{
				const glm::u8vec4 pixel = image.GetPixel(x, y);
				if (pixel.r > 150 && pixel.g < 60 && pixel.b < 60)
				{
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
				}
			}
		}
		return maxX >= minX ? glm::ivec2(static_cast<int>(maxX - minX), static_cast<int>(maxY - minY)) : glm::ivec2(-1);
	};

	SUBCASE("a stretched particle moving sideways is wider than it is tall")
	{
		Scene scene("Streak");
		PlainSettings(scene);
		ParticleEmitterComponent& emitter = AddStillEmitter(scene, "Streak", 1, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), ParticleBlend::Alpha);
		emitter.StartSize = emitter.EndSize = 0.4f;
		emitter.Speed = 10.0f;
		emitter.SpreadAngle = 0.0f;
		emitter.Facing = ParticleFacing::Stretched;
		emitter.StretchAmount = 0.2f;			// 2 m long at 10 m/s
		emitter.Drag = 0.0f;
		Entity entity = scene.FindEntityByName("Streak");
		// Up (+Y, where it's sent) turned to +X: it moves sideways across the view.
		entity.GetTransform().SetRotationEuler(glm::vec3(0.0f, 0.0f, -glm::half_pi<float>()));
		scene.OnUpdateEditor(Timestep(0.0f));
		const Image image = Draw(renderer, scene);
		image.SavePNG(BellowsTests::GetTestOutputDirectory() / "Particles_Stretched.png");
		const glm::ivec2 extent = redExtent(image);
		CHECK(extent.x > extent.y * 3);

		emitter.Facing = ParticleFacing::Camera;
		const glm::ivec2 square = redExtent(Draw(renderer, scene));
		CHECK(std::abs(square.x - square.y) <= 2);
	}

	SUBCASE("a flat particle is edge-on from the side, and a full disc from above its plane")
	{
		Scene scene("Flat");
		PlainSettings(scene);
		ParticleEmitterComponent& emitter = AddStillEmitter(scene, "Ring", 1, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), ParticleBlend::Alpha);
		emitter.Facing = ParticleFacing::Flat;
		scene.OnUpdateEditor(Timestep(0.0f));
		CHECK(CountRedPixels(Draw(renderer, scene)) < 20);		// Lying flat, seen from level

		// Turned so its up faces the camera (+Z): a disc.
		scene.FindEntityByName("Ring").GetTransform().SetRotationEuler(glm::vec3(glm::half_pi<float>(), 0.0f, 0.0f));
		const Image image = Draw(renderer, scene);
		image.SavePNG(BellowsTests::GetTestOutputDirectory() / "Particles_Flat.png");
		CHECK(CountRedPixels(image) > 50);
	}

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
