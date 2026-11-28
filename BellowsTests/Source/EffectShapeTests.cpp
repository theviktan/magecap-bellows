#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/EffectShapeSystem.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace Bellows;

namespace {

	bool Near(float a, float b, float epsilon = 1e-4f)
	{
		return std::abs(a - b) <= epsilon;
	}

	// The smallest and largest distance from the Y axis, and the lowest and highest Y, over a mesh.
	struct Extent
	{
		float MinRadius = std::numeric_limits<float>::max(), MaxRadius = 0.0f;
		float MinY = std::numeric_limits<float>::max(), MaxY = -std::numeric_limits<float>::max();
	};

	Extent Measure(const std::vector<EffectVertex>& vertices)
	{
		Extent extent;
		for (const EffectVertex& vertex : vertices)
		{
			const float radius = glm::length(glm::vec2(vertex.Position.x, vertex.Position.z));
			extent.MinRadius = std::min(extent.MinRadius, radius);
			extent.MaxRadius = std::max(extent.MaxRadius, radius);
			extent.MinY = std::min(extent.MinY, vertex.Position.y);
			extent.MaxY = std::max(extent.MaxY, vertex.Position.y);
		}
		return extent;
	}

}

TEST_CASE("Effect shapes build their surfaces: rings, cylinders, cones, spheres and domes")
{
	std::vector<EffectVertex> mesh;
	EffectShapeComponent ring;
	ring.Shape = EffectShapeType::Ring;
	ring.Radius = 2.0f;
	ring.InnerRadius = 1.0f;
	ring.Segments = 16;
	EffectShapeSystem::BuildMesh(ring, glm::mat4(1.0f), mesh);
	REQUIRE(mesh.size() == 16 * 6);
	Extent extent = Measure(mesh);
	CHECK(Near(extent.MinRadius, 1.0f));
	CHECK(Near(extent.MaxRadius, 2.0f));
	CHECK(Near(extent.MinY, 0.0f));
	CHECK(Near(extent.MaxY, 0.0f));
	for (const EffectVertex& vertex : mesh)
	{
		CHECK(Near(vertex.Normal.y, 1.0f));
		// V runs from the inside (0) to the outside (1); U around, 0 to 1.
		CHECK(Near(glm::length(glm::vec2(vertex.Position.x, vertex.Position.z)), 1.0f + vertex.UV.y));
		CHECK(vertex.UV.x >= 0.0f);
		CHECK(vertex.UV.x <= 1.0f);
	}

	// Moved, scaled and turned with the entity.
	const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
	EffectShapeSystem::BuildMesh(ring, world, mesh);
	extent = Measure(mesh);
	CHECK(Near(extent.MaxRadius, 4.0f));
	CHECK(Near(extent.MinY, 3.0f));

	EffectShapeComponent tube;
	tube.Shape = EffectShapeType::Cylinder;
	tube.Radius = 1.0f;
	tube.TopRadius = 0.5f;
	tube.Height = 3.0f;
	tube.Segments = 12;
	EffectShapeSystem::BuildMesh(tube, glm::mat4(1.0f), mesh);
	CHECK(mesh.size() == 12 * 6);
	extent = Measure(mesh);
	CHECK(Near(extent.MinRadius, 0.5f));
	CHECK(Near(extent.MaxRadius, 1.0f));
	CHECK(Near(extent.MinY, 0.0f));
	CHECK(Near(extent.MaxY, 3.0f));
	for (const EffectVertex& vertex : mesh)
	{
		// Outward, tipped up a little where it narrows toward its top.
		CHECK(glm::dot(vertex.Normal, glm::normalize(glm::vec3(vertex.Position.x, 0.0f, vertex.Position.z))) > 0.9f);
		CHECK(vertex.Normal.y > 0.0f);
	}

	EffectShapeComponent cone = tube;
	cone.Shape = EffectShapeType::Cone;
	EffectShapeSystem::BuildMesh(cone, glm::mat4(1.0f), mesh);
	extent = Measure(mesh);
	CHECK(Near(extent.MinRadius, 0.0f));
	CHECK(Near(extent.MaxY, 3.0f));

	EffectShapeComponent ball;
	ball.Shape = EffectShapeType::Sphere;
	ball.Radius = 1.5f;
	ball.Segments = 16;
	EffectShapeSystem::BuildMesh(ball, glm::mat4(1.0f), mesh);
	CHECK(mesh.size() == 16 * 8 * 6);
	extent = Measure(mesh);
	CHECK(Near(extent.MinY, -1.5f));
	CHECK(Near(extent.MaxY, 1.5f));
	for (const EffectVertex& vertex : mesh)
	{
		CHECK(Near(glm::length(vertex.Position), 1.5f));
		CHECK(glm::dot(vertex.Normal, glm::normalize(vertex.Position)) > 0.999f);
	}

	EffectShapeComponent dome = ball;
	dome.Shape = EffectShapeType::Dome;
	EffectShapeSystem::BuildMesh(dome, glm::mat4(1.0f), mesh);
	extent = Measure(mesh);
	CHECK(Near(extent.MinY, 0.0f));
	CHECK(Near(extent.MaxY, 1.5f));

	// A slice is centered on the entity's forward (-Z) and covers only its arc.
	EffectShapeComponent slice = ring;
	slice.Arc = 90.0f;
	EffectShapeSystem::BuildMesh(slice, glm::mat4(1.0f), mesh);
	CHECK(mesh.size() == 4 * 6);
	for (const EffectVertex& vertex : mesh)
	{
		const float angle = glm::degrees(std::atan2(vertex.Position.x, -vertex.Position.z));
		CHECK(angle >= -45.0f - 1e-3f);
		CHECK(angle <= 45.0f + 1e-3f);
	}

	// Nothing once finished, or at size 0.
	EffectShapeComponent finished = ring;
	finished.RuntimeFinished = true;
	EffectShapeSystem::BuildMesh(finished, glm::mat4(1.0f), mesh);
	CHECK(mesh.empty());
	EffectShapeComponent nothing = ring;
	nothing.SizeCurve = { { 0.0f, 0.0f }, { 1.0f, 0.0f } };
	EffectShapeSystem::BuildMesh(nothing, glm::mat4(1.0f), mesh);
	CHECK(mesh.empty());
}

TEST_CASE("An effect shape's cycle loops or plays once, and drives its size, color, dissolve, spin and scroll")
{
	EffectShapeComponent shape;
	shape.Duration = 2.0f;
	shape.SizeCurve = { { 0.0f, 0.0f }, { 1.0f, 2.0f } };
	shape.ColorGradient = { { 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f } }, { 1.0f, { 1.0f, 1.0f, 1.0f, 0.0f } } };
	shape.DissolveCurve = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
	shape.Color = glm::vec4(1.0f, 0.5f, 0.25f, 1.0f);
	shape.Intensity = 2.0f;
	shape.SpinSpeed = 90.0f;
	shape.Scroll = glm::vec2(0.5f, -0.25f);
	shape.Radius = 1.0f;
	shape.InnerRadius = 0.0f;
	shape.Segments = 8;

	EffectShapeSystem::Update(shape, 0.5f);
	CHECK(Near(EffectShapeSystem::CycleProgress(shape), 0.25f));
	CHECK(Near(shape.RuntimeSpin, 45.0f));
	CHECK(Near(shape.RuntimeOffset.x, 0.25f));
	CHECK(Near(shape.RuntimeOffset.y, 0.875f));
	EffectMaterial material = EffectShapeSystem::BuildMaterial(shape, glm::mat4(1.0f));
	CHECK(Near(material.Color.r, 2.0f));
	CHECK(Near(material.Color.g, 1.0f));
	CHECK(Near(material.Color.a, 0.75f));
	CHECK(Near(material.Dissolve, 0.25f));
	CHECK(material.Offset == shape.RuntimeOffset);
	std::vector<EffectVertex> mesh;
	EffectShapeSystem::BuildMesh(shape, glm::mat4(1.0f), mesh);
	CHECK(Near(Measure(mesh).MaxRadius, 0.5f));			// Size 0.5 a quarter of the way through

	// Past its end it loops back...
	EffectShapeSystem::Update(shape, 2.0f);
	CHECK(Near(shape.RuntimeTime, 0.5f));
	CHECK_FALSE(shape.RuntimeFinished);

	// ...or, played once, finishes and hides.
	shape.Loop = false;
	EffectShapeSystem::Update(shape, 3.0f);
	CHECK(shape.RuntimeFinished);
	CHECK(Near(EffectShapeSystem::CycleProgress(shape), 1.0f));
	EffectShapeSystem::BuildMesh(shape, glm::mat4(1.0f), mesh);
	CHECK(mesh.empty());
	EffectShapeSystem::Play(shape);
	CHECK_FALSE(shape.RuntimeFinished);
	CHECK(shape.RuntimeTime == 0.0f);

	// Paused, it holds; stopped, it hides.
	shape.Playing = false;
	EffectShapeSystem::Update(shape, 1.0f);
	CHECK(shape.RuntimeTime == 0.0f);
	EffectShapeSystem::Stop(shape);
	CHECK(shape.RuntimeFinished);
	EffectShapeSystem::Update(shape, std::numeric_limits<float>::quiet_NaN());
	CHECK(shape.RuntimeTime == 0.0f);

	// The scene runs cycles in the editor, and play starts every shape at the beginning.
	Scene scene("Cycles");
	Entity entity = scene.CreateEntity("Ring");
	EffectShapeComponent& live = entity.AddComponent<EffectShapeComponent>();
	live.Duration = 4.0f;
	scene.OnUpdateEditor(Timestep(1.0f));
	CHECK(Near(entity.GetComponent<EffectShapeComponent>().RuntimeTime, 1.0f));
	EffectShapeSystem::Stop(entity.GetComponent<EffectShapeComponent>());
	scene.OnRuntimeStart();
	CHECK_FALSE(entity.GetComponent<EffectShapeComponent>().RuntimeFinished);
	CHECK(entity.GetComponent<EffectShapeComponent>().RuntimeTime == 0.0f);
	scene.OnUpdateRuntime(Timestep(0.5f));
	CHECK(entity.GetComponent<EffectShapeComponent>().RuntimeTime > 0.0f);
	scene.OnRuntimeStop();
}

TEST_CASE("Effect shapes are clamped, saved and loaded")
{
	EffectShapeComponent bad;
	bad.Shape = static_cast<EffectShapeType>(9);
	bad.Radius = -1.0f;
	bad.Arc = 720.0f;
	bad.Segments = 1;
	bad.Duration = 0.0f;
	bad.Dissolve = std::numeric_limits<float>::quiet_NaN();
	bad.DissolveCurve = { { 0.5f, 7.0f } };
	bad.EdgeFade = 2.0f;
	bad.Rim = -1.0f;
	bad.Tiling = glm::vec2(-1.0f, 1e9f);
	bad.Blend = static_cast<ParticleBlend>(4);
	bad.Clamp();
	CHECK(bad.Shape == EffectShapeType::Ring);
	CHECK(bad.Radius == 0.0f);
	CHECK(bad.Arc == EffectShapeComponent::MaxArc);
	CHECK(bad.Segments == EffectShapeComponent::MinSegments);
	CHECK(bad.Duration == EffectShapeComponent::MinDuration);
	CHECK(bad.Dissolve == EffectShapeComponent().Dissolve);
	CHECK(bad.DissolveCurve.front().Value == 1.0f);
	CHECK(bad.EdgeFade == 0.5f);
	CHECK(bad.Rim == 0.0f);
	CHECK(bad.Tiling == glm::vec2(0.0f, EffectShapeComponent::MaxTiling));
	CHECK(bad.Blend == ParticleBlend::Additive);

	Scene scene("Saved");
	Entity entity = scene.CreateEntity("Pillar");
	EffectShapeComponent& shape = entity.AddComponent<EffectShapeComponent>();
	shape.Shape = EffectShapeType::Cylinder;
	shape.Radius = 0.6f;
	shape.InnerRadius = 0.1f;
	shape.TopRadius = 0.9f;
	shape.Height = 4.0f;
	shape.Arc = 180.0f;
	shape.Segments = 64;
	shape.Playing = false;
	shape.Loop = false;
	shape.Duration = 1.5f;
	shape.SizeCurve = { { 0.0f, 0.5f }, { 1.0f, 1.5f } };
	shape.ColorGradient = { { 0.0f, { 1.0f, 0.0f, 0.0f, 1.0f } }, { 1.0f, { 0.0f, 0.0f, 1.0f, 0.0f } } };
	shape.DissolveCurve = { { 0.2f, 0.0f }, { 1.0f, 1.0f } };
	shape.SpinSpeed = -30.0f;
	shape.Color = glm::vec4(1.0f, 0.9f, 0.5f, 0.8f);
	shape.Intensity = 4.0f;
	shape.Blend = ParticleBlend::Alpha;
	shape.TexturePath = "Textures/Glow.png";
	shape.Tiling = glm::vec2(4.0f, 2.0f);
	shape.Scroll = glm::vec2(0.0f, 0.5f);
	shape.EdgeFade = 0.4f;
	shape.Rim = 2.5f;
	shape.Dissolve = 0.3f;
	shape.DissolveEdgeWidth = 0.1f;
	shape.DissolveEdgeColor = glm::vec4(0.2f, 1.0f, 0.4f, 1.0f);
	shape.DissolveScale = 6.0f;

	const auto json = SceneSerializer::SerializeToJson(scene);
	Scene loaded("Loaded");
	REQUIRE(SceneSerializer::DeserializeFromJson(loaded, json));
	Entity copy = loaded.TryGetEntityByUUID(entity.GetUUID());
	REQUIRE(copy);
	REQUIRE(copy.HasComponent<EffectShapeComponent>());
	const EffectShapeComponent& read = copy.GetComponent<EffectShapeComponent>();
	CHECK(read.Shape == EffectShapeType::Cylinder);
	CHECK(read.Radius == doctest::Approx(0.6f));
	CHECK(read.InnerRadius == doctest::Approx(0.1f));
	CHECK(read.TopRadius == doctest::Approx(0.9f));
	CHECK(read.Height == doctest::Approx(4.0f));
	CHECK(read.Arc == doctest::Approx(180.0f));
	CHECK(read.Segments == 64);
	CHECK_FALSE(read.Playing);
	CHECK_FALSE(read.Loop);
	CHECK(read.Duration == doctest::Approx(1.5f));
	CHECK(read.SizeCurve == shape.SizeCurve);
	CHECK(read.ColorGradient == shape.ColorGradient);
	CHECK(read.DissolveCurve == shape.DissolveCurve);
	CHECK(read.SpinSpeed == doctest::Approx(-30.0f));
	CHECK(read.Color == shape.Color);
	CHECK(read.Intensity == doctest::Approx(4.0f));
	CHECK(read.Blend == ParticleBlend::Alpha);
	CHECK(read.TexturePath == "Textures/Glow.png");
	CHECK(read.Tiling == shape.Tiling);
	CHECK(read.Scroll == shape.Scroll);
	CHECK(read.EdgeFade == doctest::Approx(0.4f));
	CHECK(read.Rim == doctest::Approx(2.5f));
	CHECK(read.Dissolve == doctest::Approx(0.3f));
	CHECK(read.DissolveEdgeWidth == doctest::Approx(0.1f));
	CHECK(read.DissolveEdgeColor == shape.DissolveEdgeColor);
	CHECK(read.DissolveScale == doctest::Approx(6.0f));

	// An unknown shape name falls back to a ring rather than failing the load.
	auto broken = json;
	for (auto& item : broken["Entities"])
	{
		if (item.contains("EffectShape"))
			item["EffectShape"]["Shape"] = "pyramid";
	}
	Scene fixed("Fixed");
	REQUIRE(SceneSerializer::DeserializeFromJson(fixed, broken));
	CHECK(fixed.TryGetEntityByUUID(entity.GetUUID()).GetComponent<EffectShapeComponent>().Shape == EffectShapeType::Ring);
}

namespace {

	constexpr uint32_t c_Size = 96;

	RenderCamera TopCamera()
	{
		RenderCamera camera;
		camera.Position = glm::vec3(0.0f, 6.0f, 0.001f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 1.0f, 0.1f, 100.0f);
		return camera;
	}

	bool IsGreen(const glm::u8vec4& pixel)
	{
		return pixel.g > 120 && pixel.r < 60 && pixel.b < 60;
	}

	uint32_t CountGreen(const Image& image)
	{
		uint32_t count = 0;
		for (uint32_t y = 0; y < c_Size; y++)
			for (uint32_t x = 0; x < c_Size; x++)
				count += IsGreen(image.GetPixel(x, y)) ? 1 : 0;
		return count;
	}

}

TEST_CASE("[gpu] An effect shape draws its surface, fades its edges and dissolves")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	Scene scene("EffectShape");
	SceneRendererSettings& settings = scene.GetSettings().Rendering;
	settings.ToneMapper = Tonemapper::None;
	settings.BloomEnabled = false;
	settings.SSAOEnabled = false;
	settings.FXAAEnabled = false;
	settings.ShadowsEnabled = false;
	settings.Vignette = 0.0f;
	Entity entity = scene.CreateEntity("Ring");
	EffectShapeComponent& ring = entity.AddComponent<EffectShapeComponent>();
	ring.Shape = EffectShapeType::Ring;
	ring.Radius = 2.0f;
	ring.InnerRadius = 1.0f;
	ring.EdgeFade = 0.0f;
	ring.Color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
	ring.Intensity = 1.0f;
	ring.Blend = ParticleBlend::Alpha;
	ring.Playing = false;

	const auto draw = [&]()
	{
		for (int frame = 0; frame < 2; frame++)
			scene.OnRender(renderer, TopCamera());
		return Renderer::ReadbackTexture(renderer.GetFinalImage());
	};
	const Image whole = draw();
	whole.SavePNG(BellowsTests::GetTestOutputDirectory() / "EffectShape_Ring.png");
	// The band is green; its hole and the space past it are not.
	const uint32_t center = c_Size / 2;
	const float pixelsPerMeter = static_cast<float>(c_Size) / (2.0f * 6.0f * std::tan(glm::radians(25.0f)));
	CHECK(IsGreen(whole.GetPixel(center + static_cast<uint32_t>(1.5f * pixelsPerMeter), center)));
	CHECK_FALSE(IsGreen(whole.GetPixel(center, center)));
	CHECK_FALSE(IsGreen(whole.GetPixel(2, 2)));
	const uint32_t full = CountGreen(whole);
	CHECK(full > 200);

	// Edge fade softens the band's inside and outside, so less of it is fully green.
	entity.GetComponent<EffectShapeComponent>().EdgeFade = 0.5f;
	const uint32_t faded = CountGreen(draw());
	CHECK(faded < full);
	CHECK(faded > 0);

	// Half dissolved eats holes in it; fully dissolved, nothing is left.
	entity.GetComponent<EffectShapeComponent>().EdgeFade = 0.0f;
	entity.GetComponent<EffectShapeComponent>().Dissolve = 0.5f;
	entity.GetComponent<EffectShapeComponent>().DissolveEdgeWidth = 0.0f;
	const Image half = draw();
	half.SavePNG(BellowsTests::GetTestOutputDirectory() / "EffectShape_Dissolve.png");
	const uint32_t remaining = CountGreen(half);
	CHECK(remaining < full * 9 / 10);
	CHECK(remaining > full / 10);
	entity.GetComponent<EffectShapeComponent>().Dissolve = 1.0f;
	CHECK(CountGreen(draw()) == 0);

	// Stopped, it isn't drawn.
	entity.GetComponent<EffectShapeComponent>().Dissolve = 0.0f;
	EffectShapeSystem::Stop(entity.GetComponent<EffectShapeComponent>());
	CHECK(CountGreen(draw()) == 0);

	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
