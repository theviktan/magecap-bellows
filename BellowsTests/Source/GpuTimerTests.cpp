#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Profiler.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/GpuTimer.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <set>
#include <string>
#include <utility>

using namespace Bellows;

namespace {

	// Renders one small scene (a lit cube on a plane) inside a profiled frame, and waits for the GPU,
	// so each recording is done before the next frame.
	void RenderFrame(SceneRenderer& renderer, uint64_t frameIndex, bool profiled = true)
	{
		if (profiled)
			Profiler::BeginFrame(frameIndex);
		RenderCamera camera;
		camera.Position = glm::vec3(2.5f, 2.0f, 3.5f);
		camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f, 0.3f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		camera.NearClip = 0.1f;
		camera.FarClip = 100.0f;
		camera.Projection = Math::PerspectiveProjection(glm::radians(50.0f), 160.0f / 90.0f, camera.NearClip, camera.FarClip);
		renderer.BeginScene(camera);
		DirectionalLightData sun;
		sun.Direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
		sun.Intensity = 3.0f;
		renderer.SubmitDirectionalLight(sun);
		renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Cube), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)), 1);
		renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), 2);
		renderer.EndScene();
		Renderer::GetDevice()->waitForIdle();
		if (profiled)
			Profiler::EndFrame();
	}

	struct ProfilerReset
	{
		ProfilerReset()
		{
			Profiler::SetPaused(false);
			Profiler::Clear();
		}
		~ProfilerReset()
		{
			Profiler::Clear();
		}
	};

}

TEST_CASE("[gpu] A timed renderer reports each pass's GPU time to the frame that drew it")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	REQUIRE(GpuTimer::IsSupported()); // lavapipe and every desktop GPU write timestamps
	RenderDevice::ResetValidationErrorCount();
	ProfilerReset reset;

	SceneRenderer renderer;
	renderer.SetViewportSize(160, 90);
	renderer.SetGpuTimingName("Test Scene");
	const uint64_t frameCount = GpuTimer::c_Latency + 3;
	for (uint64_t i = 0; i < frameCount; i++)
		RenderFrame(renderer, 100 + i);

	// A recording's times are read when its slot comes round again, so the last c_Latency frames are
	// still waiting.
	const auto& history = Profiler::GetHistory();
	REQUIRE(history.size() == frameCount);
	const std::set<std::string> passes = { "Uploads", "Shadows", "Depth Prepass", "Ambient Occlusion", "Opaque", "Sky",
		"Transparent", "Bloom", "Tone Mapping", "Text", "Overlays", "Game UI", "Selection Outline" };
	for (size_t i = 0; i < history.size(); i++)
	{
		const ProfileFrame& frame = history[i];
		CAPTURE(frame.Index);
		if (i + GpuTimer::c_Latency >= frameCount)
		{
			CHECK_FALSE(frame.GpuTimed);
			continue;
		}
		REQUIRE(frame.GpuTimed);
		REQUIRE(frame.GpuSamples.size() == passes.size() + 1);
		const ProfileSample& total = frame.GpuSamples[0];
		CHECK(std::string(total.Name) == "Test Scene");
		CHECK(total.Depth == 0);
		CHECK(total.DurationMs > 0.0);
		CHECK(frame.GpuMs == doctest::Approx(total.DurationMs));

		double passTotal = 0.0;
		std::set<std::string> seen;
		for (size_t j = 1; j < frame.GpuSamples.size(); j++)
		{
			const ProfileSample& pass = frame.GpuSamples[j];
			CHECK(pass.Depth == 1);
			CHECK(pass.DurationMs >= 0.0);
			seen.insert(pass.Name);
			passTotal += pass.DurationMs;
		}
		CHECK(seen == passes);
		// The passes run one after another inside the renderer's scope.
		CHECK(passTotal <= total.DurationMs * 1.01 + 0.01);
	}
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] GPU timing doesn't change the image, and records nothing when off or outside a frame")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	ProfilerReset reset;

	// Timing ends the render pass before each pass; the image must be the same without it.
	SceneRenderer timed;
	timed.SetViewportSize(160, 90);
	timed.SetGpuTimingName("Timed");
	SceneRenderer untimed;
	untimed.SetViewportSize(160, 90);
	for (uint64_t i = 0; i < 2; i++)
	{
		RenderFrame(timed, i);
		RenderFrame(untimed, i, false);
	}
	const Image timedImage = Renderer::ReadbackTexture(timed.GetFinalImage());
	const Image untimedImage = Renderer::ReadbackTexture(untimed.GetFinalImage());
	REQUIRE(timedImage.IsValid());
	REQUIRE(untimedImage.IsValid());
	for (const auto& [x, y] : { std::pair(80u, 40u), std::pair(10u, 5u), std::pair(20u, 80u), std::pair(140u, 70u) })
	{
		const glm::vec4 a = timedImage.GetAverageColor(x - 4, y - 4, 8, 8);
		const glm::vec4 b = untimedImage.GetAverageColor(x - 4, y - 4, 8, 8);
		CAPTURE(x);
		CAPTURE(y);
		CHECK(glm::all(glm::lessThan(glm::abs(a - b), glm::vec4(0.01f))));
	}

	// Off, and outside a profiled frame, nothing reaches the profiler.
	Profiler::Clear();
	timed.SetGpuTimingName(nullptr);
	for (uint64_t i = 0; i < GpuTimer::c_Latency * 2; i++)
		RenderFrame(timed, 200 + i);
	SceneRenderer outside;
	outside.SetViewportSize(160, 90);
	outside.SetGpuTimingName("Outside");
	for (uint64_t i = 0; i < GpuTimer::c_Latency * 2; i++)
		RenderFrame(outside, 0, false);
	for (const ProfileFrame& frame : Profiler::GetHistory())
		CHECK_FALSE(frame.GpuTimed);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
