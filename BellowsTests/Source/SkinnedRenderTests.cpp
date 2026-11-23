#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Animation/AnimationClip.h"
#include "Bellows/Animation/Skeleton.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

using namespace Bellows;

namespace {

	constexpr uint32_t c_Size = 256;
	constexpr uint32_t c_ColumnID = 5;
	constexpr uint32_t c_GroundID = 6;

	// The 2 m column of Tools/GenerateTestModels.py: "Bend" turns its top half 90 degrees about Z (towards
	// -X) at 1 s.
	Ref<Mesh> LoadColumn()
	{
		Ref<Mesh> mesh = MeshImporter::ImportGLTF(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "SkinnedColumn.glb");
		REQUIRE(mesh);
		REQUIRE(mesh->GetSkin());
		REQUIRE(mesh->GetSkinVertexBuffer());
		return mesh;
	}

	std::vector<glm::mat4> BentPose(const MeshSkin& skin)
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		skin.FindClip("Bend")->Sample(1.0f, pose);
		std::vector<glm::mat4> model;
		std::vector<glm::mat4> matrices;
		skin.Rig->ComputeModelMatrices(pose, model);
		skin.Rig->ComputeSkinMatrices(model, matrices);
		return matrices;
	}

	RenderCamera MakeCamera(const glm::vec3& position, const glm::vec3& target, float fieldOfView = 50.0f)
	{
		RenderCamera camera;
		camera.Position = position;
		camera.View = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
		camera.NearClip = 0.1f;
		camera.FarClip = 100.0f;
		camera.Projection = Math::PerspectiveProjection(glm::radians(fieldOfView), 1.0f, camera.NearClip, camera.FarClip);
		return camera;
	}

	// The column on a ground plane, lit from straight above.
	void Render(SceneRenderer& renderer, const RenderCamera& camera, const Ref<Mesh>& column, const std::vector<glm::mat4>* pose)
	{
		for (int frame = 0; frame < 2; frame++)
		{
			renderer.BeginScene(camera);
			renderer.SetEnvironment(EnvironmentSettings {});
			DirectionalLightData sun;
			sun.Direction = glm::vec3(0.0f, -1.0f, 0.0f);
			sun.Intensity = 3.0f;
			renderer.SubmitDirectionalLight(sun);
			renderer.SubmitMesh(column, glm::mat4(1.0f), c_ColumnID, nullptr, true, pose);
			renderer.SubmitMesh(Renderer::GetPrimitiveMesh(PrimitiveMesh::Plane), glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)), c_GroundID);
			renderer.EndScene();
		}
	}

	uint32_t IDAt(const SceneRenderer& renderer, const RenderCamera& camera, const glm::vec3& world)
	{
		glm::vec2 screen(0.0f);
		REQUIRE(Math::WorldToScreenPoint(world, camera.Projection * camera.View, glm::vec2(static_cast<float>(c_Size)), screen));
		return renderer.ReadEntityID(static_cast<uint32_t>(screen.x), static_cast<uint32_t>(screen.y));
	}

}

TEST_CASE("[gpu] A skinned mesh is drawn in the pose it's given")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	const Ref<Mesh> column = LoadColumn();
	const std::vector<glm::mat4> bent = BentPose(*column->GetSkin());
	const RenderCamera camera = MakeCamera(glm::vec3(0.0f, 1.2f, 5.0f), glm::vec3(-0.3f, 1.0f, 0.0f));

	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);

	SUBCASE("Without a pose, it stands in its rest pose")
	{
		Render(renderer, camera, column, nullptr);
		CHECK(renderer.GetStatistics().SkinnedMeshes == 1);
		CHECK(IDAt(renderer, camera, glm::vec3(0.0f, 1.8f, 0.0f)) == c_ColumnID);
		CHECK(IDAt(renderer, camera, glm::vec3(0.0f, 0.3f, 0.0f)) == c_ColumnID);
		CHECK(IDAt(renderer, camera, glm::vec3(-0.8f, 1.0f, 0.0f)) == 0u);
	}

	SUBCASE("Bent, its top half lies over to the left and its foot stays")
	{
		Render(renderer, camera, column, &bent);
		Renderer::ReadbackTexture(renderer.GetFinalImage()).SavePNG(BellowsTests::GetTestOutputDirectory() / "SkinnedColumn_Bent.png");
		CHECK(renderer.GetStatistics().SkinnedMeshes == 1);
		CHECK(IDAt(renderer, camera, glm::vec3(0.0f, 1.8f, 0.0f)) == 0u);
		CHECK(IDAt(renderer, camera, glm::vec3(-0.8f, 1.0f, 0.0f)) == c_ColumnID);
		CHECK(IDAt(renderer, camera, glm::vec3(0.0f, 0.3f, 0.0f)) == c_ColumnID);
	}

	SUBCASE("A pose with the wrong number of matrices is ignored")
	{
		const std::vector<glm::mat4> wrong(1, glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 0.0f, 0.0f)));
		Render(renderer, camera, column, &wrong);
		CHECK(IDAt(renderer, camera, glm::vec3(0.0f, 1.8f, 0.0f)) == c_ColumnID);
	}
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A posed mesh is culled by where its pose puts it")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	const Ref<Mesh> column = LoadColumn();
	const std::vector<glm::mat4> bent = BentPose(*column->GetSkin());

	// A narrow view of where the bent top goes, beside the column: the mesh as made (0.2 m wide) is
	// outside it, so only bounds that follow the pose keep it from being culled.
	const RenderCamera camera = MakeCamera(glm::vec3(-0.9f, 1.0f, 3.0f), glm::vec3(-0.9f, 1.0f, 0.0f), 8.0f);
	SceneRenderer renderer;
	renderer.SetViewportSize(c_Size, c_Size);
	Render(renderer, camera, column, &bent);
	CHECK(IDAt(renderer, camera, glm::vec3(-0.9f, 1.0f, 0.0f)) == c_ColumnID);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] A posed mesh casts its posed shadow")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	const Ref<Mesh> column = LoadColumn();
	const std::vector<glm::mat4> bent = BentPose(*column->GetSkin());

	// The sun is straight overhead, so the bent top's shadow falls on the ground under it (x from -1 to
	// 0). The camera sees that ground past the column: its ray to (-0.6, 0, 0) is 1.3 m from the top.
	const RenderCamera camera = MakeCamera(glm::vec3(-0.6f, 3.0f, 4.0f), glm::vec3(-0.6f, 0.0f, 0.0f), 40.0f);
	const glm::vec3 ground(-0.6f, 0.0f, 0.0f);
	auto groundBrightness = [&](const std::vector<glm::mat4>* pose, const char* imageName)
	{
		SceneRenderer renderer;
		renderer.SetViewportSize(c_Size, c_Size);
		renderer.GetSettings().SSAOEnabled = false;
		renderer.GetSettings().Vignette = 0.0f;
		Render(renderer, camera, column, pose);
		REQUIRE(IDAt(renderer, camera, ground) == c_GroundID);
		const Image image = Renderer::ReadbackTexture(renderer.GetFinalImage());
		REQUIRE(image.IsValid());
		image.SavePNG(BellowsTests::GetTestOutputDirectory() / imageName);
		glm::vec2 screen(0.0f);
		REQUIRE(Math::WorldToScreenPoint(ground, camera.Projection * camera.View, glm::vec2(static_cast<float>(c_Size)), screen));
		return image.GetAverageColor(static_cast<uint32_t>(screen.x) - 2, static_cast<uint32_t>(screen.y) - 2, 4, 4).g;
	};
	const float standing = groundBrightness(nullptr, "SkinnedColumn_RestShadow.png");
	const float bentShadow = groundBrightness(&bent, "SkinnedColumn_BentShadow.png");
	INFO("standing " << standing << ", bent " << bentShadow);
	CHECK(bentShadow < standing * 0.7f);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}

TEST_CASE("[gpu] Past the frame's joint limit, skinned meshes are drawn as made")
{
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	RenderDevice::ResetValidationErrorCount();
	const Ref<Mesh> column = LoadColumn();

	// Every column is behind the camera, so the frame stays quick to draw.
	SceneRenderer renderer;
	renderer.SetViewportSize(64, 64);
	renderer.BeginScene(MakeCamera(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(0.0f, 1.0f, 10.0f)));
	const size_t posed = SceneRenderer::MaxFrameJoints / column->GetSkin()->Rig->GetJointCount();
	for (size_t i = 0; i < posed + 3; i++)
		renderer.SubmitMesh(column, glm::mat4(1.0f), c_ColumnID);
	CHECK(renderer.GetStatistics().Meshes == posed + 3);
	CHECK(renderer.GetStatistics().SkinnedMeshes == posed);
	renderer.EndScene();
	CHECK(renderer.GetStatistics().DrawCalls == 0);
	CHECK(RenderDevice::GetValidationErrorCount() == 0);
}
