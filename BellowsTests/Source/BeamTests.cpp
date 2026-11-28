#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/BeamSystem.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace Bellows;

namespace {

	bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
	}

}

TEST_CASE("A beam's strip runs from start to end, colored and scrolling, wobbling only between its ends")
{
	BeamComponent beam;
	beam.RuntimeValid = true;
	beam.RuntimeStart = glm::vec3(0.0f);
	beam.RuntimeEnd = glm::vec3(4.0f, 0.0f, 0.0f);
	beam.Segments = 4;
	beam.StartWidth = 0.2f;
	beam.EndWidth = 1.0f;
	beam.StartColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	beam.EndColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.5f);
	beam.Intensity = 2.0f;
	beam.TileLength = 2.0f;

	std::vector<StripPoint> strip;
	BeamSystem::BuildStrip(beam, strip);
	REQUIRE(strip.size() == 5);
	CHECK(strip[0].Position == glm::vec3(0.0f));
	CHECK(Near(strip[2].Position, glm::vec3(2.0f, 0.0f, 0.0f)));
	CHECK(strip[4].Position == glm::vec3(4.0f, 0.0f, 0.0f));
	CHECK(strip[0].Width == doctest::Approx(0.2f));
	CHECK(strip[4].Width == doctest::Approx(1.0f));
	CHECK(strip[0].Color == glm::vec4(2.0f, 0.0f, 0.0f, 1.0f));		// Intensity on RGB only
	CHECK(strip[4].Color == glm::vec4(0.0f, 0.0f, 2.0f, 0.5f));
	CHECK(strip[0].U == doctest::Approx(0.0f));
	CHECK(strip[4].U == doctest::Approx(2.0f));						// 4 m / 2 m per copy

	// Scrolling moves U by speed × time, kept to a fraction so it stays precise.
	beam.ScrollSpeed = 1.5f;
	beam.RuntimeTime = 0.5f;
	BeamSystem::BuildStrip(beam, strip);
	CHECK(strip[0].U == doctest::Approx(-0.75f));
	beam.RuntimeTime = 1000.5f;
	BeamSystem::BuildStrip(beam, strip);
	CHECK(strip[0].U == doctest::Approx(-0.75f).epsilon(1e-3));

	// A wobble moves the middle sideways and leaves both ends where they are.
	beam.Segments = 16;
	beam.NoiseAmplitude = 0.5f;
	beam.NoiseFrequency = 2.0f;
	bool moved = false;
	for (float time : { 0.1f, 0.7f, 1.3f })
	{
		beam.RuntimeTime = time;
		BeamSystem::BuildStrip(beam, strip);
		REQUIRE(strip.size() == 17);
		CHECK(Near(strip.front().Position, glm::vec3(0.0f)));
		CHECK(Near(strip.back().Position, glm::vec3(4.0f, 0.0f, 0.0f)));
		for (const StripPoint& point : strip)
		{
			CHECK(glm::length(glm::vec2(point.Position.y, point.Position.z)) <= 0.5f + 1e-4f);
			moved = moved || glm::length(glm::vec2(point.Position.y, point.Position.z)) > 0.05f;
		}
	}
	CHECK(moved);

	// Nothing to draw when it's off, not worked out, or has no length.
	beam.Active = false;
	BeamSystem::BuildStrip(beam, strip);
	CHECK(strip.empty());
	beam.Active = true;
	beam.RuntimeValid = false;
	BeamSystem::BuildStrip(beam, strip);
	CHECK(strip.empty());
	beam.RuntimeValid = true;
	beam.RuntimeEnd = beam.RuntimeStart;
	BeamSystem::BuildStrip(beam, strip);
	CHECK(strip.empty());
}

TEST_CASE("A beam ends at its point, its target or what it hits")
{
	Ref<Scene> edited = CreateRef<Scene>("Beams");
	Entity gun = edited->CreateEntity("Gun");
	gun.GetTransform().Translation = glm::vec3(1.0f, 2.0f, 0.0f);
	// Turned a quarter turn about Y: its local -Z (forward) now points along world -X.
	gun.GetTransform().SetRotationEuler(glm::vec3(0.0f, glm::half_pi<float>(), 0.0f));
	BeamComponent& beam = gun.AddComponent<BeamComponent>();
	beam.EndPoint = glm::vec3(0.0f, 0.0f, -3.0f);

	SUBCASE("a point in the entity's own space turns and moves with it")
	{
		edited->OnUpdateEditor(Timestep(0.016f));
		const BeamComponent& read = gun.GetComponent<BeamComponent>();
		REQUIRE(read.RuntimeValid);
		CHECK(Near(read.RuntimeStart, glm::vec3(1.0f, 2.0f, 0.0f)));
		CHECK(Near(read.RuntimeEnd, glm::vec3(-2.0f, 2.0f, 0.0f)));
		CHECK(read.RuntimeTime == doctest::Approx(0.016f));
	}

	SUBCASE("another entity, and nothing when it's gone or it's the beam's own")
	{
		Entity crystal = edited->CreateEntity("Crystal");
		crystal.GetTransform().Translation = glm::vec3(0.0f, 5.0f, 5.0f);
		gun.GetComponent<BeamComponent>().EndMode = BeamEndMode::Entity;
		gun.GetComponent<BeamComponent>().Target = crystal.GetUUID();
		edited->OnUpdateEditor(Timestep(0.016f));
		CHECK(gun.GetComponent<BeamComponent>().RuntimeValid);
		CHECK(Near(gun.GetComponent<BeamComponent>().RuntimeEnd, glm::vec3(0.0f, 5.0f, 5.0f)));

		gun.GetComponent<BeamComponent>().Target = gun.GetUUID();
		edited->OnUpdateEditor(Timestep(0.016f));
		CHECK_FALSE(gun.GetComponent<BeamComponent>().RuntimeValid);
		gun.GetComponent<BeamComponent>().Target = crystal.GetUUID();
		edited->DestroyEntity(crystal);
		edited->OnUpdateEditor(Timestep(0.016f));
		CHECK_FALSE(gun.GetComponent<BeamComponent>().RuntimeValid);
	}

	SUBCASE("a raycast runs its full length in the editor and stops on a collider in play, not its own")
	{
		BeamComponent& laser = gun.GetComponent<BeamComponent>();
		laser.EndMode = BeamEndMode::Raycast;
		laser.Length = 10.0f;
		// The gun's own collider is in the way at its start; a wall stands 4 m along its forward.
		gun.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
		gun.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);
		Entity wall = edited->CreateEntity("Wall");
		wall.GetTransform().Translation = glm::vec3(-3.5f, 2.0f, 0.0f);
		wall.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Static;
		wall.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f, 2.0f, 2.0f);

		edited->OnUpdateEditor(Timestep(0.016f));
		CHECK(Near(laser.RuntimeEnd, glm::vec3(-9.0f, 2.0f, 0.0f)));
		CHECK_FALSE(laser.RuntimeHit);

		Ref<Scene> game = Scene::Copy(edited);
		game->OnRuntimeStart();
		game->OnUpdateRuntime(Timestep(0.016f));
		Entity gameGun = game->TryGetEntityByUUID(gun.GetUUID());
		REQUIRE(gameGun);
		const BeamComponent& played = gameGun.GetComponent<BeamComponent>();
		REQUIRE(played.RuntimeValid);
		CHECK(played.RuntimeHit);
		CHECK(played.RuntimeHitEntity == wall.GetUUID());
		CHECK(Near(played.RuntimeEnd, glm::vec3(-3.0f, 2.0f, 0.0f), 1e-3f));
		CHECK(Near(played.RuntimeHitNormal, glm::vec3(1.0f, 0.0f, 0.0f), 1e-3f));

		// Off, it doesn't cast at all.
		gameGun.GetComponent<BeamComponent>().Active = false;
		game->OnUpdateRuntime(Timestep(0.016f));
		CHECK_FALSE(played.RuntimeValid);
		CHECK_FALSE(played.RuntimeHit);
		game->OnRuntimeStop();
	}
}

TEST_CASE("Beams are clamped, saved and loaded, and keep their targets when copied")
{
	BeamComponent bad;
	bad.EndMode = static_cast<BeamEndMode>(7);
	bad.EndPoint = glm::vec3(std::numeric_limits<float>::quiet_NaN(), 1e9f, 1.0f);
	bad.Length = -5.0f;
	bad.Segments = 0;
	bad.TileLength = 0.0f;
	bad.ScrollSpeed = -1e9f;
	bad.NoiseAmplitude = std::numeric_limits<float>::infinity();
	bad.Blend = static_cast<ParticleBlend>(5);
	bad.Clamp();
	CHECK(bad.EndMode == BeamEndMode::Point);
	CHECK(bad.EndPoint == glm::vec3(BeamComponent().EndPoint.x, BeamComponent::MaxLength, 1.0f));
	CHECK(bad.Length == 0.0f);
	CHECK(bad.Segments == BeamComponent::MinSegments);
	CHECK(bad.TileLength == BeamComponent::MinTileLength);
	CHECK(bad.ScrollSpeed == -BeamComponent::MaxScrollSpeed);
	CHECK(bad.NoiseAmplitude == BeamComponent().NoiseAmplitude);
	CHECK(bad.Blend == ParticleBlend::Additive);

	Scene scene("Saved");
	Entity rig = scene.CreateEntity("Rig");
	Entity emitter = scene.CreateChildEntity(rig, "Emitter");
	Entity receiver = scene.CreateChildEntity(rig, "Receiver");
	Entity outside = scene.CreateEntity("Outside");
	BeamComponent& tether = emitter.AddComponent<BeamComponent>();
	tether.Active = false;
	tether.EndMode = BeamEndMode::Entity;
	tether.Target = receiver.GetUUID();
	tether.EndPoint = glm::vec3(1.0f, 2.0f, 3.0f);
	tether.Length = 7.5f;
	tether.StartWidth = 0.3f;
	tether.EndWidth = 0.05f;
	tether.StartColor = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
	tether.EndColor = glm::vec4(0.25f, 0.5f, 1.0f, 0.5f);
	tether.Intensity = 6.0f;
	tether.Blend = ParticleBlend::Alpha;
	tether.TexturePath = "Textures/Lightning.png";
	tether.TileLength = 0.5f;
	tether.ScrollSpeed = -2.0f;
	tether.Segments = 40;
	tether.NoiseAmplitude = 0.3f;
	tether.NoiseFrequency = 3.0f;
	tether.NoiseSpeed = 12.0f;

	const Json json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity loadedEmitter = loaded.TryGetEntityByUUID(emitter.GetUUID());
	REQUIRE(loadedEmitter);
	const BeamComponent& read = loadedEmitter.GetComponent<BeamComponent>();
	CHECK_FALSE(read.Active);
	CHECK(read.EndMode == BeamEndMode::Entity);
	CHECK(read.Target == receiver.GetUUID());
	CHECK(read.EndPoint == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(read.Length == 7.5f);
	CHECK(read.StartWidth == 0.3f);
	CHECK(read.EndWidth == 0.05f);
	CHECK(read.StartColor == tether.StartColor);
	CHECK(read.EndColor == tether.EndColor);
	CHECK(read.Intensity == 6.0f);
	CHECK(read.Blend == ParticleBlend::Alpha);
	CHECK(read.TexturePath == "Textures/Lightning.png");
	CHECK(read.TileLength == 0.5f);
	CHECK(read.ScrollSpeed == -2.0f);
	CHECK(read.Segments == 40);
	CHECK(read.NoiseAmplitude == 0.3f);
	CHECK(read.NoiseFrequency == 3.0f);
	CHECK(read.NoiseSpeed == 12.0f);
	CHECK(SceneSerializer::SerializeToJson(loaded)["Entities"].dump() == json["Entities"].dump());

	// A copy of the whole rig (duplicate, paste, a prefab placed) aims at the copy's receiver; a
	// beam aimed outside the copied tree keeps its target.
	Entity rigCopy = scene.DuplicateEntity(rig);
	Entity emitterCopy, receiverCopy;
	for (Entity child : rigCopy.GetChildren())
		(child.GetName() == "Emitter" ? emitterCopy : receiverCopy) = child;
	REQUIRE(emitterCopy);
	REQUIRE(receiverCopy);
	CHECK(emitterCopy.GetComponent<BeamComponent>().Target == receiverCopy.GetUUID());

	Entity pasted = SceneSerializer::DeserializeEntityTree(scene, SceneSerializer::SerializeEntityTree(rig), true);
	REQUIRE(pasted);
	Entity pastedEmitter, pastedReceiver;
	for (Entity child : pasted.GetChildren())
		(child.GetName() == "Emitter" ? pastedEmitter : pastedReceiver) = child;
	REQUIRE(pastedEmitter);
	CHECK(pastedEmitter.GetComponent<BeamComponent>().Target == pastedReceiver.GetUUID());

	emitter.GetComponent<BeamComponent>().Target = outside.GetUUID();
	Entity emitterAlone = scene.DuplicateEntity(emitter);
	CHECK(emitterAlone.GetComponent<BeamComponent>().Target == outside.GetUUID());

	// An unknown end mode loads as Point.
	Json broken = json;
	for (Json& entity : broken["Entities"])
	{
		if (entity.contains("Beam"))
			entity["Beam"]["EndMode"] = "laser";
	}
	Scene fixed("Fixed");
	REQUIRE(SceneSerializer::DeserializeFromJson(fixed, broken));
	Entity fixedEmitter = fixed.TryGetEntityByUUID(emitter.GetUUID());
	REQUIRE(fixedEmitter);
	CHECK(fixedEmitter.GetComponent<BeamComponent>().EndMode == BeamEndMode::Point);
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

	bool IsRed(const glm::u8vec4& pixel)
	{
		return pixel.r > 150 && pixel.g < 60 && pixel.b < 60;
	}

}

TEST_CASE("[gpu] A beam draws as a strip between its ends")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	Scene scene("Beam");
	SceneRendererSettings& settings = scene.GetSettings().Rendering;
	settings.ToneMapper = Tonemapper::None;
	settings.BloomEnabled = false;
	settings.SSAOEnabled = false;
	settings.FXAAEnabled = false;
	settings.ShadowsEnabled = false;
	settings.Vignette = 0.0f;
	Entity emitter = scene.CreateEntity("Laser");
	emitter.GetTransform().Translation = glm::vec3(-1.0f, 0.0f, 0.0f);
	BeamComponent& beam = emitter.AddComponent<BeamComponent>();
	beam.EndPoint = glm::vec3(2.0f, 0.0f, 0.0f);			// To x = 1, across the middle of the view
	beam.StartWidth = beam.EndWidth = 0.5f;
	beam.StartColor = beam.EndColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
	beam.Intensity = 1.0f;
	beam.Blend = ParticleBlend::Alpha;
	scene.OnUpdateEditor(Timestep(0.016f));

	const auto draw = [&]()
	{
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, FrontCamera());
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};
	const Image image = draw();
	image.SavePNG(BellowsTests::GetTestOutputDirectory() / "Beam_Strip.png");
	uint32_t across = 0, beyond = 0;
	for (uint32_t x = 0; x < c_Size; x++)
		across += IsRed(image.GetPixel(x, c_Size / 2)) ? 1 : 0;
	beyond += IsRed(image.GetPixel(4, c_Size / 2)) ? 1 : 0;
	beyond += IsRed(image.GetPixel(c_Size - 5, c_Size / 2)) ? 1 : 0;
	CHECK(across > 25);			// About 2 m of the ~4.7 m in view
	CHECK(across < 60);
	CHECK(beyond == 0);			// It stops at its ends
	CHECK_FALSE(IsRed(image.GetPixel(c_Size / 2, 10)));

	emitter.GetComponent<BeamComponent>().Active = false;
	scene.OnUpdateEditor(Timestep(0.016f));
	const Image off = draw();
	uint32_t red = 0;
	for (uint32_t x = 0; x < c_Size; x++)
		red += IsRed(off.GetPixel(x, c_Size / 2)) ? 1 : 0;
	CHECK(red == 0);

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
