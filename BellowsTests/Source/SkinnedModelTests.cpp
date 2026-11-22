#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Animation/AnimationClip.h"
#include "Bellows/Animation/Skeleton.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/Buffer.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	// Tools/GenerateTestModels.py writes this: a 2 m column, joint "Base" at its foot and "Top" 1 m up,
	// with the clips "Bend" (Top turns 90 degrees about Z and back over 2 s) and "Wave" (1 s: Base turns
	// about Y, steps up 0.5 m at 0.5 s, and Top's scale eases from 1 to 2 and back).
	std::filesystem::path GetSkinnedColumnPath()
	{
		return BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "SkinnedColumn.glb";
	}

	struct GLTFParts
	{
		Json Document;
		std::vector<uint8_t> Binary;
	};

	// The column as JSON and its buffer, to write changed copies as .gltf files.
	GLTFParts ReadSkinnedColumn()
	{
		const Buffer file = FileSystem::ReadBinaryFile(GetSkinnedColumnPath());
		const uint8_t* bytes = file.Data();
		const size_t size = static_cast<size_t>(file.Size());
		REQUIRE(size > 28);
		const auto readU32 = [&](size_t offset)
		{
			REQUIRE(offset + 4 <= size);
			uint32_t value = 0;
			std::memcpy(&value, bytes + offset, 4);
			return value;
		};
		const uint32_t jsonSize = readU32(12);
		const uint32_t binarySize = readU32(20 + jsonSize);
		REQUIRE(28 + jsonSize + binarySize <= size);

		GLTFParts parts;
		parts.Document = Json::parse(bytes + 20, bytes + 20 + jsonSize);
		parts.Binary.assign(bytes + 28 + jsonSize, bytes + 28 + jsonSize + binarySize);
		return parts;
	}

	Ref<Mesh> ImportChanged(const char* name, GLTFParts parts)
	{
		const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "SkinnedModels" / name;
		REQUIRE(FileSystem::CreateDirectories(folder));
		parts.Document["buffers"][0]["uri"] = "Column.bin";
		REQUIRE(FileSystem::WriteBinaryFile(folder / "Column.bin", parts.Binary.data(), parts.Binary.size()));
		REQUIRE(FileSystem::WriteTextFile(folder / "Column.gltf", parts.Document.dump()));
		return MeshImporter::ImportGLTF(folder / "Column.gltf");
	}

	// Where the skin moves a model-space point that follows one joint completely.
	glm::vec3 Skin(const MeshSkin& skin, const std::vector<JointPose>& pose, size_t joint, const glm::vec3& point)
	{
		std::vector<glm::mat4> model;
		std::vector<glm::mat4> matrices;
		skin.Rig->ComputeModelMatrices(pose, model);
		skin.Rig->ComputeSkinMatrices(model, matrices);
		return glm::vec3(matrices[joint] * glm::vec4(point, 1.0f));
	}

	void CheckNear(const glm::vec3& actual, const glm::vec3& expected)
	{
		INFO("actual (", actual.x, ", ", actual.y, ", ", actual.z, "), expected (", expected.x, ", ", expected.y, ", ", expected.z, ")");
		CHECK(glm::length(actual - expected) < 1e-4f);
	}

	const AnimationTrack* FindTrack(const AnimationClip& clip, uint32_t joint, AnimationPath path)
	{
		for (const AnimationTrack& track : clip.GetTracks())
		{
			if (track.Joint == joint && track.Path == path)
				return &track;
		}
		return nullptr;
	}

}

TEST_CASE("A skinned glTF model brings its skeleton, weights and clips")
{
	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(GetSkinnedColumnPath());
	REQUIRE(mesh);
	const MeshSkin* skin = mesh->GetSkin();
	REQUIRE(skin);

	const Skeleton& skeleton = *skin->Rig;
	REQUIRE(skeleton.GetJointCount() == 2);
	CHECK(skeleton.FindJoint("Base") == 0);
	CHECK(skeleton.FindJoint("Top") == 1);
	CHECK(skeleton.FindJoint("Knee") == -1);
	CHECK(skeleton.GetJoints()[0].Parent == -1);
	CHECK(skeleton.GetJoints()[1].Parent == 0);
	CheckNear(skeleton.GetJoints()[1].Rest.Translation, glm::vec3(0.0f, 1.0f, 0.0f));

	for (const Submesh& submesh : mesh->GetSubmeshes())
		CHECK(submesh.Skinned);

	// Every vertex has weights adding up to 1: the foot follows Base, the top follows Top.
	REQUIRE(skin->Vertices.size() == mesh->GetVertices().size());
	for (size_t v = 0; v < skin->Vertices.size(); v++)
	{
		const SkinVertex& vertex = skin->Vertices[v];
		const float y = mesh->GetVertices()[v].Position.y;
		CHECK(vertex.Weights.x + vertex.Weights.y + vertex.Weights.z + vertex.Weights.w == doctest::Approx(1.0f));
		const float onTop = vertex.Joints[0] == 1 ? vertex.Weights[0] : vertex.Joints[1] == 1 ? vertex.Weights[1] : 0.0f;
		if (y < 0.5f)
			CHECK(onTop == doctest::Approx(0.0f));
		if (y > 1.5f)
			CHECK(onTop == doctest::Approx(1.0f));
	}

	REQUIRE(skin->Clips.size() == 2);
	CHECK(skin->Clips[0]->GetName() == "Bend");
	CHECK(skin->Clips[0]->GetDuration() == doctest::Approx(2.0f));
	CHECK(skin->Clips[1]->GetName() == "Wave");
	CHECK(skin->Clips[1]->GetDuration() == doctest::Approx(1.0f));
	CHECK(skin->FindClip("Wave") == skin->Clips[1]);
	CHECK_FALSE(skin->FindClip("Run"));

	const AnimationClip& wave = *skin->Clips[1];
	REQUIRE(FindTrack(wave, 0, AnimationPath::Translation));
	CHECK(FindTrack(wave, 0, AnimationPath::Translation)->Interpolation == AnimationInterpolation::Step);
	REQUIRE(FindTrack(wave, 1, AnimationPath::Scale));
	CHECK(FindTrack(wave, 1, AnimationPath::Scale)->Interpolation == AnimationInterpolation::CubicSpline);
	CHECK(FindTrack(wave, 1, AnimationPath::Scale)->Values.size() == 9);
}

TEST_CASE("In its rest pose a skinned model stays where it was made")
{
	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(GetSkinnedColumnPath());
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const Skeleton& skeleton = *mesh->GetSkin()->Rig;

	std::vector<glm::mat4> model;
	std::vector<glm::mat4> matrices;
	skeleton.ComputeModelMatrices(skeleton.GetRestPose(), model);
	skeleton.ComputeSkinMatrices(model, matrices);
	REQUIRE(matrices.size() == 2);
	for (const glm::mat4& matrix : matrices)
	{
		for (int column = 0; column < 4; column++)
		{
			for (int row = 0; row < 4; row++)
				CHECK(matrix[column][row] == doctest::Approx(column == row ? 1.0f : 0.0f));
		}
	}
}

TEST_CASE("A clip poses the skeleton between and at its keys")
{
	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(GetSkinnedColumnPath());
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const MeshSkin& skin = *mesh->GetSkin();
	const AnimationClip& bend = *skin.FindClip("Bend");
	const AnimationClip& wave = *skin.FindClip("Wave");

	SUBCASE("Linear rotation: the top of the column bends over the knee")
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		bend.Sample(1.0f, pose);
		CheckNear(Skin(skin, pose, 1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-1.0f, 1.0f, 0.0f));

		bend.Sample(0.5f, pose);
		const float half = std::sqrt(0.5f);
		CheckNear(Skin(skin, pose, 1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(-half, 1.0f + half, 0.0f));

		bend.Sample(2.0f, pose);
		CheckNear(Skin(skin, pose, 1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(0.0f, 2.0f, 0.0f));
		// The foot doesn't move.
		CheckNear(Skin(skin, pose, 0, glm::vec3(0.1f, 0.0f, 0.1f)), glm::vec3(0.1f, 0.0f, 0.1f));
	}

	SUBCASE("Step translation holds each key until the next")
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		wave.Sample(0.25f, pose);
		CheckNear(pose[0].Translation, glm::vec3(0.0f));
		wave.Sample(0.5f, pose);
		CheckNear(pose[0].Translation, glm::vec3(0.0f, 0.5f, 0.0f));
		wave.Sample(0.75f, pose);
		CheckNear(pose[0].Translation, glm::vec3(0.0f, 0.5f, 0.0f));
		wave.Sample(1.0f, pose);
		CheckNear(pose[0].Translation, glm::vec3(0.0f));
	}

	SUBCASE("Cubic scale eases between keys with flat tangents")
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		wave.Sample(0.25f, pose);
		CheckNear(pose[1].Scale, glm::vec3(1.5f));
		wave.Sample(0.125f, pose);
		// Hermite at a quarter of the span: 1 + (3 * 0.25^2 - 2 * 0.25^3) = 1.15625.
		CheckNear(pose[1].Scale, glm::vec3(1.15625f));
		wave.Sample(0.5f, pose);
		CheckNear(pose[1].Scale, glm::vec3(2.0f));
	}

	SUBCASE("Times outside the clip are held to its start and end")
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		bend.Sample(-3.0f, pose);
		CHECK(std::abs(pose[1].Rotation.w) == doctest::Approx(1.0f));
		bend.Sample(50.0f, pose);
		CHECK(std::abs(pose[1].Rotation.w) == doctest::Approx(1.0f));
		bend.Sample(std::nanf(""), pose);
		CHECK(std::abs(pose[1].Rotation.w) == doctest::Approx(1.0f));
		CHECK(std::isfinite(pose[1].Rotation.x));
	}

	SUBCASE("Parts a clip doesn't animate keep their values")
	{
		std::vector<JointPose> pose = skin.Rig->GetRestPose();
		pose[0].Scale = glm::vec3(3.0f);
		bend.Sample(1.0f, pose);
		CheckNear(pose[0].Scale, glm::vec3(3.0f));
		CheckNear(pose[1].Translation, glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

TEST_CASE("Skin joints listed children first are sorted parents first, with their weights")
{
	GLTFParts parts = ReadSkinnedColumn();
	// Top before Base, without inverse bind matrices (they'd have to be swapped too).
	parts.Document["skins"][0]["joints"] = Json::array({ 2, 1 });
	parts.Document["skins"][0].erase("inverseBindMatrices");
	const Ref<Mesh> mesh = ImportChanged("ChildFirst", std::move(parts));
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const MeshSkin& skin = *mesh->GetSkin();
	CHECK(skin.Rig->FindJoint("Base") == 0);
	CHECK(skin.Rig->FindJoint("Top") == 1);
	CHECK(skin.Rig->GetJoints()[1].Parent == 0);

	// The file's joint 0 is now Top, so the foot's weights (on the file's joint 0) go to our joint 1.
	for (size_t v = 0; v < skin.Vertices.size(); v++)
	{
		if (mesh->GetVertices()[v].Position.y < 0.5f)
			CHECK(skin.Vertices[v].Joints[0] == 1);
	}
}

TEST_CASE("Nodes above a skin's root move its joints, and a skinned node's own transform is ignored")
{
	GLTFParts parts = ReadSkinnedColumn();
	Json& nodes = parts.Document["nodes"];
	nodes[0]["translation"] = Json::array({ 0.0, 0.0, 9.0 });	// glTF says skinned meshes ignore this
	nodes.push_back(Json { { "name", "Armature" }, { "translation", Json::array({ 5.0, 0.0, 0.0 }) }, { "children", Json::array({ 1 }) } });
	parts.Document["scenes"][0]["nodes"] = Json::array({ 0, 3 });
	const Ref<Mesh> mesh = ImportChanged("Armature", std::move(parts));
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const MeshSkin& skin = *mesh->GetSkin();
	CHECK(mesh->GetBoundingBox().Max.z < 1.0f);

	const std::vector<JointPose> rest = skin.Rig->GetRestPose();
	CheckNear(Skin(skin, rest, 0, glm::vec3(0.0f)), glm::vec3(5.0f, 0.0f, 0.0f));
	CheckNear(Skin(skin, rest, 1, glm::vec3(0.0f, 2.0f, 0.0f)), glm::vec3(5.0f, 2.0f, 0.0f));
}

TEST_CASE("A model facing +Z is turned with its skeleton")
{
	MeshImportSettings settings;
	settings.Front = ModelFront::PositiveZ;
	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(GetSkinnedColumnPath(), settings);
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const MeshSkin& skin = *mesh->GetSkin();

	// The skeleton stays in the file's space and the submesh transform turns the skinned result, so the
	// column ends up turned and in its rest pose.
	std::vector<JointPose> pose = skin.Rig->GetRestPose();
	const glm::mat4 turn = mesh->GetSubmeshes()[0].Transform;
	CheckNear(glm::vec3(turn * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)), glm::vec3(-1.0f, 0.0f, 0.0f));
	CheckNear(glm::vec3(turn * glm::vec4(Skin(skin, pose, 1, glm::vec3(0.1f, 2.0f, 0.1f)), 1.0f)),
		glm::vec3(turn * glm::vec4(0.1f, 2.0f, 0.1f, 1.0f)));
}

TEST_CASE("Animations of nodes outside the skin, and broken tracks, are left out")
{
	GLTFParts parts = ReadSkinnedColumn();
	Json& animations = parts.Document["animations"];
	// Bend now moves the mesh node, which isn't a joint, so the clip has no tracks and is dropped.
	animations[0]["channels"][0]["target"]["node"] = 0;
	// Wave's rotation gets the step track's three-number values, which can't be a rotation.
	animations[1]["samplers"][0]["output"] = animations[1]["samplers"][1]["output"];
	animations.push_back(animations[1]);	// A second "Wave" gets a unique name
	animations.push_back(animations[1]);
	animations[3].erase("name");
	const Ref<Mesh> mesh = ImportChanged("BrokenTracks", std::move(parts));
	REQUIRE(mesh);
	REQUIRE(mesh->GetSkin());
	const MeshSkin& skin = *mesh->GetSkin();
	REQUIRE(skin.Clips.size() == 3);
	CHECK(skin.Clips[0]->GetName() == "Wave");
	CHECK(skin.Clips[1]->GetName() == "Wave 2");
	CHECK(skin.Clips[2]->GetName() == "Animation 4");
	CHECK(skin.Clips[0]->GetTracks().size() == 2);
	CHECK_FALSE(FindTrack(*skin.Clips[0], 0, AnimationPath::Rotation));
	CHECK(FindTrack(*skin.Clips[0], 0, AnimationPath::Translation));
}

TEST_CASE("A static model has no skin")
{
	GLTFParts parts = ReadSkinnedColumn();
	parts.Document["nodes"][0].erase("skin");
	const Ref<Mesh> mesh = ImportChanged("NoSkin", std::move(parts));
	REQUIRE(mesh);
	CHECK_FALSE(mesh->GetSkin());
	for (const Submesh& submesh : mesh->GetSubmeshes())
		CHECK_FALSE(submesh.Skinned);
}
