#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstring>
#include <vector>

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

	// A binary glTF (.glb): the JSON chunk, then the BIN chunk, each padded to 4 bytes.
	std::vector<uint8_t> MakeGLB(const Json& document, std::vector<uint8_t> bin)
	{
		std::string json = document.dump();
		while (json.size() % 4 != 0)
			json.push_back(' ');
		while (bin.size() % 4 != 0)
			bin.push_back(0);
		std::vector<uint8_t> glb;
		auto put32 = [&](uint32_t value) { for (int i = 0; i < 4; i++) glb.push_back(static_cast<uint8_t>(value >> (8 * i))); };
		put32(0x46546C67);	// "glTF"
		put32(2);
		put32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
		put32(static_cast<uint32_t>(json.size()));
		put32(0x4E4F534A);	// "JSON"
		glb.insert(glb.end(), json.begin(), json.end());
		put32(static_cast<uint32_t>(bin.size()));
		put32(0x004E4942);	// "BIN"
		glb.insert(glb.end(), bin.begin(), bin.end());
		return glb;
	}

	template<typename T>
	void Append(std::vector<uint8_t>& bytes, const std::vector<T>& values)
	{
		const size_t start = bytes.size();
		bytes.resize(start + values.size() * sizeof(T));
		std::memcpy(bytes.data() + start, values.data(), values.size() * sizeof(T));
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

TEST_CASE("A MageCap Foundry model imports: shared accessors, both index sizes, linear colors, blending, node transforms and extras")
{
	// What Foundry writes (MFC, Nov 23): float positions and normals; uint16 or uint32 indices; one
	// accessor set shared by several meshes; linear baseColorFactor with metallic 0 and roughness 1,
	// BLEND when see-through; node translation, rotation and scale; extras.magecapFoundry; no extensions.
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "FoundryImport";
	REQUIRE(FileSystem::CreateDirectories(folder));

	std::vector<uint8_t> bin;
	Append(bin, std::vector<float> { -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f });	// 0: positions
	Append(bin, std::vector<float> { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 });										// 48: normals
	Append(bin, std::vector<uint16_t> { 0, 3, 2, 0, 2, 1 });														// 96: uint16 indices
	Append(bin, std::vector<uint32_t> { 0, 3, 2, 0, 2, 1 });														// 108: uint32 indices
	REQUIRE(bin.size() == 132);

	const glm::quat quarterTurn = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
	const Json extras = { { "magecapFoundry", { { "shape", "slab" }, { "size", { 1.0, 0.25, 1.0 } }, { "color", "#cc2222" } } } };
	const Json document = {
		{ "asset", { { "version", "2.0" }, { "generator", "MageCap Foundry" } } },
		{ "scene", 0 },
		{ "scenes", { { { "nodes", { 0, 1, 2 } } } } },
		{ "nodes", {
			{ { "name", "slab" }, { "mesh", 0 }, { "translation", { 1.0, 2.0, 3.0 } },
				{ "rotation", { quarterTurn.x, quarterTurn.y, quarterTurn.z, quarterTurn.w } }, { "scale", { 2.0, 1.0, 1.0 } }, { "extras", extras } },
			{ { "name", "glass" }, { "mesh", 1 }, { "translation", { 0.0, 1.0, 0.0 } }, { "extras", extras } },
			{ { "name", "slab mirrored" }, { "mesh", 2 }, { "scale", { -1.0, 1.0, 1.0 } }, { "extras", extras } } } },
		{ "meshes", {
			{ { "primitives", { { { "attributes", { { "POSITION", 0 }, { "NORMAL", 1 } } }, { "indices", 2 }, { "material", 0 } } } } },
			{ { "primitives", { { { "attributes", { { "POSITION", 0 }, { "NORMAL", 1 } } }, { "indices", 3 }, { "material", 1 } } } } },
			{ { "primitives", { { { "attributes", { { "POSITION", 0 }, { "NORMAL", 1 } } }, { "indices", 2 }, { "material", 0 } } } } } } },
		{ "materials", {
			{ { "name", "red" }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.6, 0.016, 0.016, 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } },
			{ { "name", "glass" }, { "alphaMode", "BLEND" },
				{ "pbrMetallicRoughness", { { "baseColorFactor", { 0.2, 0.4, 0.8, 0.35 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } } } },
		{ "buffers", { { { "byteLength", 132 } } } },
		{ "bufferViews", {
			{ { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 48 }, { "target", 34962 } },
			{ { "buffer", 0 }, { "byteOffset", 48 }, { "byteLength", 48 }, { "target", 34962 } },
			{ { "buffer", 0 }, { "byteOffset", 96 }, { "byteLength", 12 }, { "target", 34963 } },
			{ { "buffer", 0 }, { "byteOffset", 108 }, { "byteLength", 24 }, { "target", 34963 } } } },
		{ "accessors", {
			{ { "bufferView", 0 }, { "componentType", 5126 }, { "count", 4 }, { "type", "VEC3" }, { "min", { -0.5, 0.0, -0.5 } }, { "max", { 0.5, 0.0, 0.5 } } },
			{ { "bufferView", 1 }, { "componentType", 5126 }, { "count", 4 }, { "type", "VEC3" } },
			{ { "bufferView", 2 }, { "componentType", 5123 }, { "count", 6 }, { "type", "SCALAR" } },
			{ { "bufferView", 3 }, { "componentType", 5125 }, { "count", 6 }, { "type", "SCALAR" } } } } };
	const std::vector<uint8_t> glb = MakeGLB(document, bin);
	REQUIRE(FileSystem::WriteBinaryFile(folder / "Foundry.glb", glb.data(), glb.size()));

	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(folder / "Foundry.glb");
	REQUIRE(mesh);
	const std::vector<Submesh>& submeshes = mesh->GetSubmeshes();
	REQUIRE(submeshes.size() == 3);

	// Each node gets the shared vertices and its own triangles, wound as written.
	for (const Submesh& submesh : submeshes)
	{
		CHECK(submesh.VertexCount == 4);
		REQUIRE(submesh.IndexCount == 6);
		const std::vector<uint32_t> expected = { 0, 3, 2, 0, 2, 1 };
		for (uint32_t i = 0; i < 6; i++)
			CHECK(mesh->GetIndices()[submesh.BaseIndex + i] == expected[i]);
		const Vertex& corner = mesh->GetVertices()[submesh.BaseVertex + 2];
		CHECK(corner.Position == glm::vec3(0.5f, 0.0f, 0.5f));
		CHECK(corner.Normal == glm::vec3(0.0f, 1.0f, 0.0f));
	}
	CHECK(submeshes[0].Name == "slab");
	CHECK(submeshes[1].Name == "glass");

	// Node transforms: translation, then rotation, then scale, as glTF composes them.
	const glm::mat4 expected = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)) * glm::mat4_cast(quarterTurn)
		* glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f));
	for (int column = 0; column < 4; column++)
		for (int row = 0; row < 4; row++)
			CHECK(submeshes[0].Transform[column][row] == doctest::Approx(expected[column][row]).epsilon(1e-5));
	CHECK(glm::vec3(submeshes[1].Transform[3]) == glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(glm::determinant(glm::mat3(submeshes[2].Transform)) == doctest::Approx(-1.0f));	// The renderer draws it mirrored

	// Colors stay linear, as glTF writes them; see-through blends.
	const MaterialProperties& red = mesh->GetMaterials()[submeshes[0].MaterialIndex]->GetProperties();
	CHECK(red.AlbedoColor == glm::vec4(0.6f, 0.016f, 0.016f, 1.0f));
	CHECK(red.Metallic == 0.0f);
	CHECK(red.Roughness == 1.0f);
	CHECK(red.Mode == AlphaMode::Opaque);
	const MaterialProperties& glass = mesh->GetMaterials()[submeshes[1].MaterialIndex]->GetProperties();
	CHECK(glass.AlbedoColor == glm::vec4(0.2f, 0.4f, 0.8f, 0.35f));
	CHECK(glass.Mode == AlphaMode::Blend);
}

TEST_CASE("A glTF material with KHR_materials_unlit imports Unlit, even when the file requires it")
{
	// Foundry's Unlit look (MFC, Nov 23) writes KHR_materials_unlit as used, not required; other
	// programs may require it, which Bellows now reads.
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "UnlitImport";
	REQUIRE(FileSystem::CreateDirectories(folder));
	std::vector<uint8_t> bin;
	Append(bin, std::vector<float> { -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f, 0.5f });
	Append(bin, std::vector<uint16_t> { 0, 2, 1, 0 });		// The last index pads to 4 bytes
	REQUIRE(bin.size() == 44);

	for (const bool required : { false, true })
	{
		Json document = {
			{ "asset", { { "version", "2.0" }, { "generator", "MageCap Foundry" } } },
			{ "extensionsUsed", { "KHR_materials_unlit" } },
			{ "scene", 0 },
			{ "scenes", { { { "nodes", { 0, 1 } } } } },
			{ "nodes", { { { "name", "flat" }, { "mesh", 0 } }, { { "name", "lit" }, { "mesh", 1 } } } },
			{ "meshes", {
				{ { "primitives", { { { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "material", 0 } } } } },
				{ { "primitives", { { { "attributes", { { "POSITION", 0 } } }, { "indices", 1 }, { "material", 1 } } } } } } },
			{ "materials", {
				{ { "name", "flat" }, { "extensions", { { "KHR_materials_unlit", Json::object() } } },
					{ "pbrMetallicRoughness", { { "baseColorFactor", { 0.2, 0.6, 0.1, 1.0 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } },
				{ { "name", "lit" }, { "pbrMetallicRoughness", { { "baseColorFactor", { 0.2, 0.6, 0.1, 1.0 } } } } } } },
			{ "buffers", { { { "byteLength", 44 } } } },
			{ "bufferViews", {
				{ { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 36 }, { "target", 34962 } },
				{ { "buffer", 0 }, { "byteOffset", 36 }, { "byteLength", 6 }, { "target", 34963 } } } },
			{ "accessors", {
				{ { "bufferView", 0 }, { "componentType", 5126 }, { "count", 3 }, { "type", "VEC3" }, { "min", { -0.5, 0.0, -0.5 } }, { "max", { 0.5, 0.0, 0.5 } } },
				{ { "bufferView", 1 }, { "componentType", 5123 }, { "count", 3 }, { "type", "SCALAR" } } } } };
		if (required)
			document["extensionsRequired"] = { "KHR_materials_unlit" };
		const std::vector<uint8_t> glb = MakeGLB(document, bin);
		const std::filesystem::path path = folder / (required ? "Required.glb" : "Used.glb");
		REQUIRE(FileSystem::WriteBinaryFile(path, glb.data(), glb.size()));

		INFO("required: ", required);
		const Ref<Mesh> mesh = MeshImporter::ImportGLTF(path);
		REQUIRE(mesh);
		const std::vector<Submesh>& submeshes = mesh->GetSubmeshes();
		REQUIRE(submeshes.size() == 2);
		const MaterialProperties& flat = mesh->GetMaterials()[submeshes[0].MaterialIndex]->GetProperties();
		CHECK(flat.Unlit);
		CHECK(flat.AlbedoColor == glm::vec4(0.2f, 0.6f, 0.1f, 1.0f));
		CHECK_FALSE(mesh->GetMaterials()[submeshes[1].MaterialIndex]->GetProperties().Unlit);
	}
}

TEST_CASE("[gpu] A textured MageCap Foundry model imports: palette sampled pixelated, glass, glow and both sides")
{
	// What Foundry 2.60.0 writes besides seq 1694's list (MFC, board seq 1743): TEXCOORD_0, an embedded PNG
	// with a NEAREST sampler, emissiveFactor with an emissiveTexture, KHR_materials_transmission (used, not
	// required) on a BLEND material with alpha 0.35, and doubleSided.
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "FoundryTextured";
	REQUIRE(FileSystem::CreateDirectories(folder));

	Image palette;
	palette.Width = palette.Height = 2;
	palette.Pixels = { 200, 40, 40, 255, 40, 200, 40, 255, 40, 40, 200, 255, 230, 230, 230, 255 };
	REQUIRE(palette.SavePNG(folder / "Palette.png"));
	const Buffer png = FileSystem::ReadBinaryFile(folder / "Palette.png");
	REQUIRE(png.Size() > 0);

	std::vector<uint8_t> bin;
	Append(bin, std::vector<float> { -0.5f, 0.0f, -0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f });	// 0: positions
	Append(bin, std::vector<float> { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 });										// 48: normals
	Append(bin, std::vector<float> { 0, 0, 1, 0, 1, 1, 0, 1 });													// 96: TEXCOORD_0
	Append(bin, std::vector<uint16_t> { 0, 3, 2, 0, 2, 1 });														// 128: indices
	const size_t imageOffset = 140;
	bin.resize(imageOffset);
	bin.insert(bin.end(), png.Data(), png.Data() + png.Size());

	const Json attributes = { { "POSITION", 0 }, { "NORMAL", 1 }, { "TEXCOORD_0", 2 } };
	const Json document = {
		{ "asset", { { "version", "2.0" }, { "generator", "MageCap Foundry" } } },
		{ "extensionsUsed", { "KHR_materials_transmission" } },
		{ "scene", 0 },
		{ "scenes", { { { "nodes", { 0, 1, 2 } } } } },
		{ "nodes", { { { "name", "wall" }, { "mesh", 0 } }, { { "name", "glass" }, { "mesh", 1 } }, { { "name", "lantern" }, { "mesh", 2 } } } },
		{ "meshes", {
			{ { "primitives", { { { "attributes", attributes }, { "indices", 3 }, { "material", 0 } } } } },
			{ { "primitives", { { { "attributes", attributes }, { "indices", 3 }, { "material", 1 } } } } },
			{ { "primitives", { { { "attributes", attributes }, { "indices", 3 }, { "material", 2 } } } } } } },
		{ "materials", {
			{ { "name", "palette" }, { "doubleSided", true },
				{ "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 0 } } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } },
			{ { "name", "glass" }, { "alphaMode", "BLEND" }, { "doubleSided", true },
				{ "pbrMetallicRoughness", { { "baseColorFactor", { 0.8, 0.9, 1.0, 0.35 } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 0.1 } } },
				{ "extensions", { { "KHR_materials_transmission", { { "transmissionFactor", 0.9 } } } } } },
			{ { "name", "glow" }, { "emissiveFactor", { 1.0, 0.8, 0.4 } }, { "emissiveTexture", { { "index", 0 } } },
				{ "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 0 } } }, { "metallicFactor", 0.0 }, { "roughnessFactor", 1.0 } } } } } },
		{ "samplers", { { { "magFilter", 9728 }, { "minFilter", 9984 }, { "wrapS", 33071 }, { "wrapT", 33648 } } } },
		{ "images", { { { "bufferView", 4 }, { "mimeType", "image/png" } } } },
		{ "textures", { { { "source", 0 }, { "sampler", 0 } } } },
		{ "buffers", { { { "byteLength", bin.size() } } } },
		{ "bufferViews", {
			{ { "buffer", 0 }, { "byteOffset", 0 }, { "byteLength", 48 } },
			{ { "buffer", 0 }, { "byteOffset", 48 }, { "byteLength", 48 } },
			{ { "buffer", 0 }, { "byteOffset", 96 }, { "byteLength", 32 } },
			{ { "buffer", 0 }, { "byteOffset", 128 }, { "byteLength", 12 } },
			{ { "buffer", 0 }, { "byteOffset", imageOffset }, { "byteLength", png.Size() } } } },
		{ "accessors", {
			{ { "bufferView", 0 }, { "componentType", 5126 }, { "count", 4 }, { "type", "VEC3" }, { "min", { -0.5, 0.0, -0.5 } }, { "max", { 0.5, 0.0, 0.5 } } },
			{ { "bufferView", 1 }, { "componentType", 5126 }, { "count", 4 }, { "type", "VEC3" } },
			{ { "bufferView", 2 }, { "componentType", 5126 }, { "count", 4 }, { "type", "VEC2" } },
			{ { "bufferView", 3 }, { "componentType", 5123 }, { "count", 6 }, { "type", "SCALAR" } } } } };
	const std::vector<uint8_t> glb = MakeGLB(document, bin);
	REQUIRE(FileSystem::WriteBinaryFile(folder / "Textured.glb", glb.data(), glb.size()));

	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(folder / "Textured.glb");
	REQUIRE(mesh);
	REQUIRE(mesh->GetSubmeshes().size() == 3);
	auto material = [&](size_t submesh) { return mesh->GetMaterials()[mesh->GetSubmeshes()[submesh].MaterialIndex]; };

	// 1: the palette texture, sampled pixelated, with its wraps; UVs as written.
	const Ref<Material> wall = material(0);
	CHECK(wall->GetAlbedoMap());
	CHECK(wall->GetProperties().Filter == TextureFilter::Nearest);
	CHECK(wall->GetProperties().WrapU == TextureWrap::Clamp);
	CHECK(wall->GetProperties().WrapV == TextureWrap::Mirror);
	CHECK(mesh->GetVertices()[mesh->GetSubmeshes()[0].BaseVertex + 2].TexCoord == glm::vec2(1.0f, 1.0f));
	// 2: glass blends with its alpha (Bellows draws transmission as that, and the extension doesn't stop the import).
	const MaterialProperties& glass = material(1)->GetProperties();
	CHECK(glass.Mode == AlphaMode::Blend);
	CHECK(glass.AlbedoColor.a == doctest::Approx(0.35f));
	CHECK(glass.Filter == TextureFilter::Linear);		// No texture, so nothing to sample
	// 3: glow, its color and its map.
	const Ref<Material> lantern = material(2);
	CHECK(lantern->GetEmissiveMap());
	CHECK(lantern->GetProperties().EmissiveColor == glm::vec3(1.0f, 0.8f, 0.4f));
	// 4: both sides.
	CHECK(wall->GetProperties().DoubleSided);
	CHECK(glass.DoubleSided);
	CHECK_FALSE(lantern->GetProperties().DoubleSided);
}

TEST_CASE("[gpu] MageCap Foundry's starter house, as Forge exports it, imports and renders")
{
	// BellowsTests/Data/FoundryStarterHouse.glb is a copy of magecap-forge's
	// tests/fixtures/foundry/foundry-starter-house.glb (Forge 2.65.0, commit a60d176): a floor, a
	// wall, a mirrored wall with a window, a lantern post, and a cube, cylinder and wedge, all with the
	// Starter pack's palette texture.
	Application& application = BellowsTests::GetTestApplication();
	REQUIRE(application.IsValid());
	const Ref<Mesh> mesh = MeshImporter::ImportGLTF(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "FoundryStarterHouse.glb");
	REQUIRE(mesh);

	// Each block's parts, one per material, named after the block.
	const std::vector<Submesh>& submeshes = mesh->GetSubmeshes();
	REQUIRE(submeshes.size() == 11);
	auto part = [&](const std::string& block, const std::string& material) -> const Submesh*
	{
		for (const Submesh& submesh : submeshes)
			if (submesh.Name == block && mesh->GetMaterials()[submesh.MaterialIndex]->GetName() == material)
				return &submesh;
		return nullptr;
	};
	auto properties = [&](const Submesh* submesh) -> const MaterialProperties& { return mesh->GetMaterials()[submesh->MaterialIndex]->GetProperties(); };
	const Submesh* floor = part("Floor wood 2 m", "palette");
	const Submesh* window = part("Wall timber 2 m window", "glass");
	const Submesh* metal = part("Lantern post large", "palette_metal");
	const Submesh* glow = part("Lantern post large", "palette_glow");
	const Submesh* wedge = part("Wedge", "Material6");
	REQUIRE(floor);
	REQUIRE(part("Wall timber 2 m", "palette"));
	REQUIRE(part("Wall timber 2 m window", "palette"));
	REQUIRE(window);
	REQUIRE(metal);
	REQUIRE(glow);
	REQUIRE(part("Cube", "Material4"));
	REQUIRE(part("Cylinder", "Material5"));
	REQUIRE(wedge);

	// The Starter pack's palette: one texture, sampled pixelated, repeating (glTF's default wrap).
	const Ref<Material> palette = mesh->GetMaterials()[floor->MaterialIndex];
	REQUIRE(palette->GetAlbedoMap());
	CHECK(palette->GetProperties().Filter == TextureFilter::Nearest);
	CHECK(palette->GetProperties().WrapU == TextureWrap::Repeat);
	CHECK(palette->GetProperties().DoubleSided);
	CHECK(palette->GetProperties().Roughness == doctest::Approx(0.8f));
	CHECK_FALSE(palette->GetProperties().Unlit);
	// Glass blends at 0.35 (transmission is used, not required); metal and glow keep their values.
	CHECK(properties(window).Mode == AlphaMode::Blend);
	CHECK(properties(window).AlbedoColor.a == doctest::Approx(0.35f));
	CHECK(properties(window).Filter == TextureFilter::Nearest);
	CHECK(properties(metal).Metallic == doctest::Approx(0.6f));
	CHECK(properties(metal).Roughness == doctest::Approx(0.4f));
	CHECK(mesh->GetMaterials()[glow->MaterialIndex]->GetEmissiveMap());
	CHECK(properties(glow).EmissiveColor == glm::vec3(1.0f));
	// Plain shapes: their color, linear as glTF writes it, and no texture.
	const MaterialProperties& cube = properties(part("Cube", "Material4"));
	CHECK(cube.AlbedoColor.r == doctest::Approx(0.57758f).epsilon(1e-4));
	CHECK(cube.AlbedoColor.g == doctest::Approx(0.03071f).epsilon(1e-3));
	CHECK_FALSE(mesh->GetMaterials()[part("Cube", "Material4")->MaterialIndex]->GetAlbedoMap());
	// Node transforms: the floor sits at (1, 0, -1); the wedge is sized 1 x 1.5 x 2.
	CHECK(glm::vec3(floor->Transform[3]) == glm::vec3(1.0f, 0.0f, -1.0f));
	CHECK(glm::length(glm::vec3(wedge->Transform[1])) == doctest::Approx(1.5f));
	CHECK(glm::length(glm::vec3(wedge->Transform[2])) == doctest::Approx(2.0f));
	// The mirrored wall is mirrored in its vertices (Forge 2.60.0's fix), so nothing is drawn mirrored.
	for (const Submesh& submesh : submeshes)
		CHECK(glm::determinant(glm::mat3(submesh.Transform)) > 0.0f);
}
