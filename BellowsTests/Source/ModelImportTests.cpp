#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <array>

using namespace Bellows;

namespace {

	// A one-triangle glTF model in front of its origin along +Z (how Blender exports a model's front),
	// on a node moved 3 m further along +Z.
	void WriteFrontTriangle(const std::filesystem::path& folder)
	{
		const std::array<float, 9> positions = { -0.5f, 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 2.0f };
		REQUIRE(FileSystem::WriteBinaryFile(folder / "Front.bin", positions.data(), sizeof(positions)));
		REQUIRE(FileSystem::WriteTextFile(folder / "Front.gltf", R"({
			"asset": { "version": "2.0" },
			"scene": 0,
			"scenes": [ { "nodes": [ 0 ] } ],
			"nodes": [ { "mesh": 0, "translation": [ 0, 0, 3 ] } ],
			"meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
			"buffers": [ { "uri": "Front.bin", "byteLength": 36 } ],
			"bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
			"accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
				"min": [ -0.5, 0, 1 ], "max": [ 0.5, 1, 2 ] } ]
		})"));
	}

	std::filesystem::path MakeModelFolder(const char* name)
	{
		const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / name / "Models";
		REQUIRE(FileSystem::CreateDirectories(folder));
		WriteFrontTriangle(folder);
		return folder;
	}

}

TEST_CASE("A model whose front faces +Z is turned to face Bellows' forward, around its own origin")
{
	const std::filesystem::path model = MakeModelFolder("ModelFront") / "Front.gltf";

	Ref<Mesh> asMade = MeshImporter::ImportGLTF(model);
	REQUIRE(asMade);
	CHECK(asMade->GetBoundingBox().Min.z == doctest::Approx(4.0f));
	CHECK(asMade->GetBoundingBox().Max.z == doctest::Approx(5.0f));

	MeshImportSettings settings;
	settings.Front = ModelFront::PositiveZ;
	Ref<Mesh> turned = MeshImporter::ImportGLTF(model, settings);
	REQUIRE(turned);
	const AABB& box = turned->GetBoundingBox();
	CHECK(box.Min.z == doctest::Approx(-5.0f));
	CHECK(box.Max.z == doctest::Approx(-4.0f));
	CHECK(box.Min.x == doctest::Approx(-0.5f));
	CHECK(box.Max.x == doctest::Approx(0.5f));
	CHECK(box.Max.y == doctest::Approx(1.0f));

	// Half a turn keeps the handedness, so faces keep their winding and stay lit from the right side.
	const glm::mat4& transform = turned->GetSubmeshes()[0].Transform;
	CHECK(glm::determinant(glm::mat3(transform)) == doctest::Approx(1.0f));
	CHECK(turned->GetVertices().size() == asMade->GetVertices().size());
}

TEST_CASE("Import settings live beside the model, and the project's default covers the rest")
{
	const std::filesystem::path folder = MakeModelFolder("ModelImportSettings");
	const std::filesystem::path model = folder / "Front.gltf";
	AssetManager::SetAssetDirectory(folder.parent_path());
	AssetManager::SetDefaultModelFront(ModelFront::NegativeZ);

	SUBCASE("No file: the project's default")
	{
		CHECK_FALSE(MeshImportSettings::Load(model));
		CHECK(AssetManager::GetMeshImportSettings("Models/Front.gltf").Front == ModelFront::NegativeZ);
		AssetManager::SetDefaultModelFront(ModelFront::PositiveZ);
		CHECK(AssetManager::GetMeshImportSettings("Models/Front.gltf").Front == ModelFront::PositiveZ);
		// Meshes loaded with the old default are dropped, so they load turned now.
		Ref<Mesh> mesh = AssetManager::GetMesh("Models/Front.gltf");
		REQUIRE(mesh);
		CHECK(mesh->GetBoundingBox().Max.z == doctest::Approx(-4.0f));
	}

	SUBCASE("The model's own file wins over the default, and round-trips")
	{
		MeshImportSettings settings;
		settings.Front = ModelFront::PositiveZ;
		REQUIRE(settings.Save(model));
		CHECK(MeshImportSettings::GetFilePath(model).filename() == "Front.gltf.bimport");
		CHECK(FileSystem::ReadTextFile(MeshImportSettings::GetFilePath(model))->find("\"FrontFaces\": \"+Z\"") != std::string::npos);
		REQUIRE(MeshImportSettings::Load(model));
		CHECK(MeshImportSettings::Load(model)->Front == ModelFront::PositiveZ);
		CHECK(AssetManager::GetMeshImportSettings("Models/Front.gltf").Front == ModelFront::PositiveZ);
	}

	SUBCASE("A damaged file or an unknown value is ignored")
	{
		REQUIRE(FileSystem::WriteTextFile(MeshImportSettings::GetFilePath(model), "not json"));
		CHECK_FALSE(MeshImportSettings::Load(model));
		REQUIRE(FileSystem::WriteTextFile(MeshImportSettings::GetFilePath(model), R"({ "FrontFaces": "+Y" })"));
		REQUIRE(MeshImportSettings::Load(model));
		CHECK(MeshImportSettings::Load(model)->Front == ModelFront::NegativeZ);
	}

	std::filesystem::remove(MeshImportSettings::GetFilePath(model));
	AssetManager::SetDefaultModelFront(ModelFront::NegativeZ);
	AssetManager::ClearCache();
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Model front names, and the project's ModelFrontFaces is saved and read")
{
	CHECK(std::string(ModelFrontToString(ModelFront::NegativeZ)) == "-Z");
	CHECK(std::string(ModelFrontToString(ModelFront::PositiveZ)) == "+Z");
	CHECK(ModelFrontFromString("+Z") == ModelFront::PositiveZ);
	CHECK_FALSE(ModelFrontFromString("z"));

	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "ModelFrontProject";
	Ref<Project> project = Project::Create(folder, "Fronts");
	REQUIRE(project);
	// New projects start at +Z, and the setting is saved and read back.
	CHECK(project->GetConfig().ModelFrontFaces == ModelFront::PositiveZ);
	project->GetConfig().ModelFrontFaces = ModelFront::NegativeZ;
	REQUIRE(project->Save());
	Ref<Project> loaded = Project::Load(project->GetProjectFilePath());
	REQUIRE(loaded);
	CHECK(loaded->GetConfig().ModelFrontFaces == ModelFront::NegativeZ);
	loaded->GetConfig().ModelFrontFaces = ModelFront::PositiveZ;
	loaded->ApplyImportSettings();
	CHECK(AssetManager::GetDefaultModelFront() == ModelFront::PositiveZ);
	AssetManager::SetDefaultModelFront(ModelFront::NegativeZ);

	// A project file from before 0.5.0 has no setting: -Z, so its placed models don't turn around.
	Json old = Json::parse(*FileSystem::ReadTextFile(project->GetProjectFilePath()));
	old.erase("Import");
	REQUIRE(FileSystem::WriteTextFile(project->GetProjectFilePath(), old.dump(1, '\t')));
	Ref<Project> older = Project::Load(project->GetProjectFilePath());
	REQUIRE(older);
	CHECK(older->GetConfig().ModelFrontFaces == ModelFront::NegativeZ);
}

TEST_CASE("Damaged glTF sizes and offsets are refused before anything is read or allocated")
{
	const std::filesystem::path folder = MakeModelFolder("DamagedModels");
	const auto import = [&](const char* name, const std::string& buffers, const std::string& accessors)
	{
		const std::filesystem::path model = folder / name;
		REQUIRE(FileSystem::WriteTextFile(model, R"({
			"asset": { "version": "2.0" },
			"nodes": [ { "mesh": 0 } ],
			"meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
			"buffers": )" + buffers + R"(,
			"bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
			"accessors": )" + accessors + "}"));
		return MeshImporter::ImportGLTF(model);
	};
	const std::string triangle = R"([ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ -0.5, 0, 1 ], "max": [ 0.5, 1, 2 ] } ])";
	const std::string bin = R"([ { "uri": "Front.bin", "byteLength": 36 } ])";
	REQUIRE(import("Valid.gltf", bin, triangle));

	// A buffer bigger than its file (here 2^62 bytes) would be allocated in full before the read failed.
	CHECK_FALSE(import("HugeBuffer.gltf", R"([ { "uri": "Front.bin", "byteLength": 4611686018427387904 } ])", triangle));
	// The same for base64 data: 3 bytes per 4 characters, at most.
	CHECK_FALSE(import("HugeDataBuffer.gltf", R"([ { "uri": "data:application/octet-stream;base64,AAAA", "byteLength": 36 } ])", triangle));
	// An accessor without a buffer view is all zeros and may claim any count.
	CHECK_FALSE(import("HugeCount.gltf", bin, R"([ { "componentType": 5126, "count": 4000000000, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 0, 0, 0 ] } ])"));
	// Floats must start on a multiple of 4 bytes.
	CHECK_FALSE(import("Misaligned.gltf", R"([ { "uri": "Front.bin", "byteLength": 36 } ])",
		R"([ { "bufferView": 0, "byteOffset": 2, "componentType": 5126, "count": 2, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 1 ] } ])"));
}

TEST_CASE("A glTF file that requires an extension Bellows can't read is refused")
{
	const std::filesystem::path folder = MakeModelFolder("ModelExtensions");
	const std::optional<std::string> text = FileSystem::ReadTextFile(folder / "Front.gltf");
	REQUIRE(text);
	const Json front = Json::parse(*text);
	const auto withRequired = [&](const char* name, const std::vector<std::string>& required)
	{
		Json document = front;
		document["extensionsUsed"] = required;
		document["extensionsRequired"] = required;
		const std::filesystem::path path = folder / name;
		REQUIRE(FileSystem::WriteTextFile(path, document.dump()));
		return MeshImporter::ImportGLTF(path);
	};
	CHECK_FALSE(withRequired("Draco.gltf", { "KHR_draco_mesh_compression" }));
	CHECK_FALSE(withRequired("Mixed.gltf", { "KHR_materials_emissive_strength", "EXT_meshopt_compression" }));
	CHECK(withRequired("Supported.gltf", { "KHR_materials_emissive_strength", "KHR_mesh_quantization" }));

	// Extensions a file only uses (not requires) don't stop it.
	Json document = front;
	document["extensionsUsed"] = { "KHR_materials_variants" };
	REQUIRE(FileSystem::WriteTextFile(folder / "Used.gltf", document.dump()));
	CHECK(MeshImporter::ImportGLTF(folder / "Used.gltf"));
}
