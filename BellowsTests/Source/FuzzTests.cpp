// Fuzz tests for every loader that reads a file a user or a download could hand the engine: images,
// glTF models, fonts, audio, scenes, prefabs, materials, UI themes, import settings and project files.
// Each test mutates a valid file many times over (flipped bits, special values, cut and copied
// ranges, and for JSON files, values swapped for huge numbers, long strings, deep nesting or the
// wrong type) and loads every result. A loader may refuse a file; it must never crash, throw, trip
// an engine assertion or return something broken.
//
// The mutations come from a fixed seed, so a run is the same on every machine. Each input is written
// to TestOutput/Fuzz/<target>/ before it is loaded, so a crash leaves the file that caused it there.
// BELLOWS_FUZZ_ITERATIONS sets how many inputs each test tries (the default keeps CI quick) and
// BELLOWS_FUZZ_SEED picks another seed; see AGENTS.md for longer runs and AddressSanitizer.

#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/Buffer.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Log.h"
#include "Bellows/Navigation/NavMesh.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Serialization/JsonUtils.h"
#include "Bellows/Text/Font.h"
#include "Bellows/UI/UITheme.h"

#include <miniaudio.h>
#include <stb_image_write.h>
#include <tinyexr.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace Bellows;

namespace {

	using Bytes = std::vector<uint8_t>;

	uint32_t ReadEnvironmentNumber(const char* name, uint32_t fallback)
	{
		const char* text = std::getenv(name);
		if (!text || !*text)
			return fallback;
		char* end = nullptr;
		const unsigned long value = std::strtoul(text, &end, 10);
		return end && *end == '\0' && value > 0 ? static_cast<uint32_t>(value) : fallback;
	}

	uint32_t GetIterations(uint32_t defaultCount) { return ReadEnvironmentNumber("BELLOWS_FUZZ_ITERATIONS", defaultCount); }

	// Each test has its own default seed, so the tests don't all try the same mutations.
	uint32_t GetSeed(uint32_t testSeed) { return ReadEnvironmentNumber("BELLOWS_FUZZ_SEED", 0) ^ testSeed; }

	// Mutates files. Picks numbers with plain modulo rather than std::uniform_int_distribution, whose
	// results differ between standard libraries, so a seed gives the same inputs on Windows and Linux.
	class Mutator
	{
	public:
		explicit Mutator(uint32_t seed) : m_Random(seed) {}

		size_t Below(size_t count) { return count == 0 ? 0 : static_cast<size_t>(m_Random()) % count; }
		bool OneIn(size_t count) { return Below(count) == 0; }

		// One to eight byte-level changes.
		Bytes MutateBytes(Bytes bytes)
		{
			const size_t changes = 1 + Below(8);
			for (size_t i = 0; i < changes; i++)
				MutateOnce(bytes);
			return bytes;
		}

		// Changes one to four values in a JSON document, then sometimes the text as well, so both the
		// loader's reading of values and the JSON parser itself get bad input.
		std::string MutateJson(const Json& document)
		{
			Json copy = document;
			const size_t changes = 1 + Below(4);
			for (size_t i = 0; i < changes; i++)
			{
				std::vector<Json*> nodes;
				CollectNodes(copy, nodes);
				MutateValue(*nodes[Below(nodes.size())]);
			}
			std::string text = copy.dump(1, '\t');
			if (OneIn(4))
			{
				const Bytes bytes = MutateBytes(Bytes(text.begin(), text.end()));
				text.assign(bytes.begin(), bytes.end());
			}
			return text;
		}

	private:
		void MutateOnce(Bytes& bytes)
		{
			static constexpr std::array<uint8_t, 8> c_Interesting8 = { 0x00, 0x01, 0x10, 0x20, 0x7F, 0x80, 0xFE, 0xFF };
			static constexpr std::array<uint32_t, 8> c_Interesting32 = { 0u, 1u, 0x7FFFu, 0xFFFFu, 0x10000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };

			if (bytes.empty())
			{
				bytes.push_back(static_cast<uint8_t>(m_Random()));
				return;
			}
			const size_t at = Below(bytes.size());
			switch (Below(9))
			{
				case 0:		// Flip a bit
					bytes[at] ^= static_cast<uint8_t>(1u << Below(8));
					break;
				case 1:		// A random byte
					bytes[at] = static_cast<uint8_t>(m_Random());
					break;
				case 2:		// A byte at a boundary
					bytes[at] = c_Interesting8[Below(c_Interesting8.size())];
					break;
				case 3:		// A 32-bit size or count at a boundary, either byte order
				{
					uint32_t value = c_Interesting32[Below(c_Interesting32.size())];
					const bool bigEndian = OneIn(2);
					for (size_t i = 0; i < 4 && at + i < bytes.size(); i++)
						bytes[at + i] = static_cast<uint8_t>(bigEndian ? value >> (24 - 8 * i) : value >> (8 * i));
					break;
				}
				case 4:		// Insert random bytes
				{
					Bytes inserted(1 + Below(16));
					for (uint8_t& byte : inserted)
						byte = static_cast<uint8_t>(m_Random());
					bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), inserted.begin(), inserted.end());
					break;
				}
				case 5:		// Delete a range
				{
					const size_t count = std::min(bytes.size() - at, 1 + Below(64));
					bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + count));
					break;
				}
				case 6:		// Cut the file short
					bytes.resize(at);
					break;
				case 7:		// Repeat a range in place
				{
					const size_t count = std::min(bytes.size() - at, 1 + Below(256));
					const Bytes range(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + count));
					bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), range.begin(), range.end());
					break;
				}
				default:	// Copy a range over another part of the file
				{
					const size_t from = Below(bytes.size());
					const size_t count = std::min({ bytes.size() - from, bytes.size() - at, 1 + Below(64) });
					std::memmove(bytes.data() + at, bytes.data() + from, count);
					break;
				}
			}
		}

		static void CollectNodes(Json& node, std::vector<Json*>& nodes)
		{
			nodes.push_back(&node);
			if (node.is_structured())
			{
				for (Json& child : node)
					CollectNodes(child, nodes);
			}
		}

		void MutateValue(Json& value)
		{
			switch (Below(14))
			{
				case 0:		value = nullptr; break;
				case 1:		value = OneIn(2); break;
				case 2:		value = -1; break;
				case 3:		value = 0; break;
				case 4:		value = std::numeric_limits<int64_t>::max(); break;
				case 5:		value = std::numeric_limits<uint64_t>::max(); break;
				case 6:		value = 1e308; break;
				case 7:		value = -1e308; break;
				case 8:		value = std::string(1 + Below(70000), 'x'); break;
				case 9:		value = Json::array(); break;
				case 10:	value = Json::object(); break;
				case 11:	// Deep nesting, past what any loader should walk by recursion
				{
					Json nested = 1;
					const size_t depth = 64 + Below(2000);
					for (size_t i = 0; i < depth; i++)
						nested = Json::array({ std::move(nested) });
					value = std::move(nested);
					break;
				}
				case 12:	// Repeat an array element or object (duplicate IDs, many copies of an entity)
				{
					if (value.is_array() && !value.empty())
					{
						const Json element = value[Below(value.size())];
						const size_t copies = 1 + Below(OneIn(8) ? 300 : 3);
						for (size_t i = 0; i < copies; i++)
							value.push_back(element);
					}
					else
						value = Json::array({ value, value });
					break;
				}
				default:	// Drop a key from an object
				{
					if (value.is_object() && !value.empty())
					{
						auto it = value.begin();
						std::advance(it, static_cast<std::ptrdiff_t>(Below(value.size())));
						value.erase(it);
					}
					else
						value = "";
					break;
				}
			}
		}

		std::mt19937 m_Random;
	};

	std::filesystem::path GetFuzzDirectory(const char* target)
	{
		const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Fuzz" / target;
		REQUIRE(FileSystem::CreateDirectories(folder));
		return folder;
	}

	Bytes ReadFile(const std::filesystem::path& path)
	{
		const Buffer buffer = FileSystem::ReadBinaryFile(path);
		REQUIRE(buffer.Size() > 0);
		return Bytes(buffer.Data(), buffer.Data() + buffer.Size());
	}

	Json ReadJsonFile(const std::filesystem::path& path)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(path);
		REQUIRE(text);
		Json json = Json::parse(*text, nullptr, false);
		REQUIRE(!json.is_discarded());
		return json;
	}

	void WriteInput(const std::filesystem::path& path, const void* data, size_t size)
	{
		REQUIRE(FileSystem::WriteBinaryFile(path, data, size));
	}

	// Runs 'load' on mutated copies of the seeds. 'load' gets the iteration and the seed it starts from
	// and makes its own input (bytes or JSON), so each target writes files the way its loader reads them.
	template<typename LoadFunction>
	void RunFuzzer(uint32_t testSeed, uint32_t defaultIterations, size_t seedCount, LoadFunction&& load)
	{
		const uint32_t seed = GetSeed(testSeed);
		const uint32_t iterations = GetIterations(defaultIterations);
		Mutator mutator(seed);
		const uint32_t assertionsBefore = Log::GetAssertionFailureCount();
		INFO("fuzz seed ", seed, " (BELLOWS_FUZZ_SEED changes it)");
		for (uint32_t iteration = 0; iteration < iterations; iteration++)
		{
			CAPTURE(iteration);
			REQUIRE_NOTHROW(load(mutator, iteration % seedCount));
			// An assertion is the engine noticing it got into a state it shouldn't be in.
			REQUIRE(Log::GetAssertionFailureCount() == assertionsBefore);
		}
	}

	// Small images in every format Bellows reads, each a different size and with an alpha channel.
	std::vector<Bytes> MakeImageSeeds()
	{
		constexpr int c_Width = 7, c_Height = 5;
		std::vector<uint8_t> rgba(c_Width * c_Height * 4);
		std::vector<float> floats(rgba.size());
		for (size_t i = 0; i < rgba.size(); i++)
		{
			rgba[i] = static_cast<uint8_t>(i * 37);
			floats[i] = static_cast<float>(i % 11) * 0.7f;
		}

		std::vector<Bytes> seeds;
		const auto collect = [](void* context, void* data, int size)
		{
			auto* bytes = static_cast<Bytes*>(context);
			bytes->insert(bytes->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
		};
		const auto add = [&](auto write)
		{
			Bytes bytes;
			REQUIRE(write(&bytes) != 0);
			seeds.push_back(std::move(bytes));
		};
		add([&](Bytes* out) { return stbi_write_png_to_func(collect, out, c_Width, c_Height, 4, rgba.data(), c_Width * 4); });
		add([&](Bytes* out) { return stbi_write_jpg_to_func(collect, out, c_Width, c_Height, 4, rgba.data(), 90); });
		add([&](Bytes* out) { return stbi_write_bmp_to_func(collect, out, c_Width, c_Height, 4, rgba.data()); });
		add([&](Bytes* out) { return stbi_write_tga_to_func(collect, out, c_Width, c_Height, 4, rgba.data()); });
		add([&](Bytes* out) { return stbi_write_hdr_to_func(collect, out, c_Width, c_Height, 4, floats.data()); });

		// OpenEXR, through a file (tinyexr writes RGBA EXR to memory only through its lower-level API).
		const std::filesystem::path exr = GetFuzzDirectory("ImageSeeds") / "Seed.exr";
		const char* error = nullptr;
		const int result = SaveEXR(floats.data(), c_Width, c_Height, 4, 0, exr.string().c_str(), &error);
		if (error)
			FreeEXRErrorMessage(error);
		REQUIRE(result == TINYEXR_SUCCESS);
		seeds.push_back(ReadFile(exr));

		seeds.push_back(ReadFile(BellowsTests::GetSourceDirectory() / "Projects" / "FeatureTest" / "Assets" / "Textures" / "GroundTiles.png"));
		return seeds;
	}

	// A one-triangle glTF model with indices, normals and texture coordinates, so the importer's
	// index, normal and tangent code all run.
	struct GLTFSeed
	{
		Json Document;
		Bytes Buffer;
	};

	GLTFSeed MakeGLTFSeed()
	{
		const std::array<float, 9> positions = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
		const std::array<float, 9> normals = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		const std::array<float, 6> texCoords = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
		const std::array<uint16_t, 4> indices = { 0, 1, 2, 0 };	// The last one pads to 4 bytes

		GLTFSeed seed;
		const auto append = [&](const void* data, size_t size)
		{
			seed.Buffer.insert(seed.Buffer.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
		};
		append(positions.data(), sizeof(positions));
		append(normals.data(), sizeof(normals));
		append(texCoords.data(), sizeof(texCoords));
		append(indices.data(), sizeof(indices));

		seed.Document = Json::parse(R"({
			"asset": { "version": "2.0" },
			"scene": 0,
			"scenes": [ { "nodes": [ 0 ] } ],
			"nodes": [ { "name": "Parent", "children": [ 1 ], "rotation": [ 0, 0.7071068, 0, 0.7071068 ] },
				{ "mesh": 0, "translation": [ 0, 0, 3 ], "scale": [ 2, 2, 2 ] } ],
			"materials": [ { "name": "Red", "pbrMetallicRoughness": { "baseColorFactor": [ 1, 0, 0, 1 ], "roughnessFactor": 0.5 },
				"alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true } ],
			"meshes": [ { "name": "Triangle", "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2 },
				"indices": 3, "material": 0 } ] } ],
			"buffers": [ { "uri": "Model.bin", "byteLength": 104 } ],
			"bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 }, { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
				{ "buffer": 0, "byteOffset": 72, "byteLength": 24 }, { "buffer": 0, "byteOffset": 96, "byteLength": 8 } ],
			"accessors": [
				{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] },
				{ "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
				{ "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" },
				{ "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" } ]
		})");
		return seed;
	}

	// The same model as one binary .glb file: header, JSON chunk and BIN chunk.
	Bytes MakeGLB(const GLTFSeed& seed)
	{
		Json document = seed.Document;
		document["buffers"][0].erase("uri");
		std::string json = document.dump();
		json.resize((json.size() + 3) & ~size_t(3), ' ');
		Bytes buffer = seed.Buffer;
		buffer.resize((buffer.size() + 3) & ~size_t(3), 0);

		Bytes glb;
		const auto appendU32 = [&](uint32_t value)
		{
			for (int i = 0; i < 4; i++)
				glb.push_back(static_cast<uint8_t>(value >> (8 * i)));
		};
		appendU32(0x46546C67);	// "glTF"
		appendU32(2);
		appendU32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + buffer.size()));
		appendU32(static_cast<uint32_t>(json.size()));
		appendU32(0x4E4F534A);	// "JSON"
		glb.insert(glb.end(), json.begin(), json.end());
		appendU32(static_cast<uint32_t>(buffer.size()));
		appendU32(0x004E4942);	// "BIN\0"
		glb.insert(glb.end(), buffer.begin(), buffer.end());
		return glb;
	}

	void CheckMesh(const Ref<Mesh>& mesh)
	{
		if (!mesh)
			return;
		const size_t vertexCount = mesh->GetVertices().size();
		const size_t indexCount = mesh->GetIndices().size();
		REQUIRE_FALSE(mesh->GetSubmeshes().empty());
		for (const Submesh& submesh : mesh->GetSubmeshes())
		{
			REQUIRE(static_cast<size_t>(submesh.BaseIndex) + submesh.IndexCount <= indexCount);
			REQUIRE(static_cast<size_t>(submesh.BaseVertex) + submesh.VertexCount <= vertexCount);
			REQUIRE(submesh.MaterialIndex < mesh->GetMaterials().size());
			for (uint32_t i = 0; i < submesh.IndexCount; i++)
				REQUIRE(mesh->GetIndices()[submesh.BaseIndex + i] < submesh.VertexCount);
		}

		const MeshSkin* skin = mesh->GetSkin();
		if (!skin)
			return;
		const Skeleton& skeleton = *skin->Rig;
		REQUIRE(skeleton.GetJointCount() > 0);
		REQUIRE(skeleton.GetJointCount() <= Skeleton::MaxJoints);
		REQUIRE(skin->Vertices.size() == vertexCount);
		for (const SkinVertex& vertex : skin->Vertices)
		{
			for (int k = 0; k < 4; k++)
			{
				REQUIRE(vertex.Joints[k] < skeleton.GetJointCount());
				REQUIRE(std::isfinite(vertex.Weights[k]));
			}
		}
		REQUIRE(skin->Clips.size() <= AnimationClip::MaxClips);
		// Every clip samples to a pose the skeleton can turn into matrices.
		std::vector<JointPose> pose = skeleton.GetRestPose();
		std::vector<glm::mat4> model;
		for (const Ref<AnimationClip>& clip : skin->Clips)
		{
			REQUIRE(std::isfinite(clip->GetDuration()));
			for (const AnimationTrack& track : clip->GetTracks())
				REQUIRE(track.Joint < skeleton.GetJointCount());
			clip->Sample(clip->GetDuration() * 0.37f, pose);
			skeleton.ComputeModelMatrices(pose, model);
			REQUIRE(model.size() == skeleton.GetJointCount());
		}
	}

	// A .glb split into its JSON and its binary chunk (the model the skinning tests read).
	GLTFSeed SplitGLB(const Bytes& glb)
	{
		const auto readU32 = [&](size_t offset)
		{
			REQUIRE(offset + 4 <= glb.size());
			return static_cast<uint32_t>(glb[offset]) | static_cast<uint32_t>(glb[offset + 1]) << 8
				| static_cast<uint32_t>(glb[offset + 2]) << 16 | static_cast<uint32_t>(glb[offset + 3]) << 24;
		};
		const uint32_t jsonSize = readU32(12);
		const size_t binOffset = 20 + jsonSize;
		const uint32_t binSize = readU32(binOffset);
		REQUIRE(binOffset + 8 + binSize <= glb.size());
		GLTFSeed seed;
		seed.Document = Json::parse(glb.begin() + 20, glb.begin() + 20 + jsonSize);
		seed.Buffer.assign(glb.begin() + static_cast<std::ptrdiff_t>(binOffset + 8), glb.begin() + static_cast<std::ptrdiff_t>(binOffset + 8 + binSize));
		return seed;
	}

	// A 16-bit mono WAV of a short tone.
	Bytes MakeWav()
	{
		constexpr uint32_t c_SampleRate = 8000, c_Frames = 400;
		Bytes wav;
		const auto appendU32 = [&](uint32_t value) { for (int i = 0; i < 4; i++) wav.push_back(static_cast<uint8_t>(value >> (8 * i))); };
		const auto appendU16 = [&](uint16_t value) { wav.push_back(static_cast<uint8_t>(value)); wav.push_back(static_cast<uint8_t>(value >> 8)); };
		const auto appendTag = [&](const char* tag) { wav.insert(wav.end(), tag, tag + 4); };
		appendTag("RIFF");
		appendU32(36 + c_Frames * 2);
		appendTag("WAVE");
		appendTag("fmt ");
		appendU32(16);
		appendU16(1);					// PCM
		appendU16(1);					// Mono
		appendU32(c_SampleRate);
		appendU32(c_SampleRate * 2);	// Bytes per second
		appendU16(2);					// Bytes per frame
		appendU16(16);
		appendTag("data");
		appendU32(c_Frames * 2);
		for (uint32_t i = 0; i < c_Frames; i++)
			appendU16(static_cast<uint16_t>(static_cast<int16_t>(std::sin(static_cast<float>(i) * 0.3f) * 12000.0f)));
		return wav;
	}

}

TEST_CASE("Fuzz: damaged images are decoded or refused, never crash")
{
	const std::vector<Bytes> seeds = MakeImageSeeds();
	const std::filesystem::path input = GetFuzzDirectory("Images") / "Input.bin";
	RunFuzzer(0x1A6E5u, 1400, seeds.size(), [&](Mutator& mutator, size_t seedIndex)
	{
		const Bytes bytes = mutator.MutateBytes(seeds[seedIndex]);
		WriteInput(input, bytes.data(), bytes.size());
		const std::optional<DecodedImage> image = Texture2D::Decode(bytes.data(), bytes.size(), "Fuzz");
		if (!image)
			return;
		REQUIRE(image->Width > 0);
		REQUIRE(image->Height > 0);
		REQUIRE(image->Pixels);
		// Touch the last pixel: the buffer must hold every one the size claims (AddressSanitizer checks).
		const size_t last = static_cast<size_t>(image->Width) * image->Height * 4 - 1;
		volatile float sink = image->HighDynamicRange ? image->GetPixelsFloat()[last] : image->GetPixels8()[last];
		(void)sink;
	});
}

TEST_CASE("Fuzz: damaged glTF and .glb models are imported or refused, never crash")
{
	const GLTFSeed seed = MakeGLTFSeed();
	const Bytes glb = MakeGLB(seed);
	const std::filesystem::path folder = GetFuzzDirectory("Models");
	const std::filesystem::path gltfPath = folder / "Model.gltf";
	const std::filesystem::path glbPath = folder / "Model.glb";
	const std::filesystem::path binPath = folder / "Model.bin";

	// The seeds import as they are.
	WriteInput(gltfPath, seed.Document.dump().data(), seed.Document.dump().size());
	WriteInput(binPath, seed.Buffer.data(), seed.Buffer.size());
	WriteInput(glbPath, glb.data(), glb.size());
	REQUIRE(MeshImporter::ImportGLTF(gltfPath));
	REQUIRE(MeshImporter::ImportGLTF(glbPath));

	// 0: the .gltf JSON, 1: its .bin buffer, 2: the .glb's bytes.
	RunFuzzer(0x6175u, 900, 3, [&](Mutator& mutator, size_t target)
	{
		Ref<Mesh> mesh;
		if (target == 0)
		{
			const std::string text = mutator.MutateJson(seed.Document);
			WriteInput(gltfPath, text.data(), text.size());
			WriteInput(binPath, seed.Buffer.data(), seed.Buffer.size());
			MeshImportSettings settings;
			settings.Front = mutator.OneIn(2) ? ModelFront::PositiveZ : ModelFront::NegativeZ;
			mesh = MeshImporter::ImportGLTF(gltfPath, settings);
		}
		else if (target == 1)
		{
			const std::string text = seed.Document.dump();
			const Bytes buffer = mutator.MutateBytes(seed.Buffer);
			WriteInput(gltfPath, text.data(), text.size());
			WriteInput(binPath, buffer.data(), buffer.size());
			mesh = MeshImporter::ImportGLTF(gltfPath);
		}
		else
		{
			const Bytes bytes = mutator.MutateBytes(glb);
			WriteInput(glbPath, bytes.data(), bytes.size());
			mesh = MeshImporter::ImportGLTF(glbPath);
		}
		CheckMesh(mesh);
	});
}

TEST_CASE("Fuzz: damaged skinned models are imported or refused, never crash")
{
	const Bytes glb = ReadFile(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "SkinnedColumn.glb");
	GLTFSeed seed = SplitGLB(glb);
	seed.Document["buffers"][0]["uri"] = "Skinned.bin";
	const std::string text = seed.Document.dump();
	const std::filesystem::path folder = GetFuzzDirectory("SkinnedModels");
	const std::filesystem::path gltfPath = folder / "Skinned.gltf";
	const std::filesystem::path glbPath = folder / "Skinned.glb";
	const std::filesystem::path binPath = folder / "Skinned.bin";

	WriteInput(gltfPath, text.data(), text.size());
	WriteInput(binPath, seed.Buffer.data(), seed.Buffer.size());
	WriteInput(glbPath, glb.data(), glb.size());
	const Ref<Mesh> asIs = MeshImporter::ImportGLTF(gltfPath);
	REQUIRE(asIs);
	REQUIRE(asIs->GetSkin());
	REQUIRE(MeshImporter::ImportGLTF(glbPath));

	// 0: the JSON (skin, joints, animations), 1: the buffer (weights, matrices, keys), 2: the .glb's bytes.
	RunFuzzer(0x534Bu, 900, 3, [&](Mutator& mutator, size_t target)
	{
		Ref<Mesh> mesh;
		if (target == 0)
		{
			const std::string mutated = mutator.MutateJson(seed.Document);
			WriteInput(gltfPath, mutated.data(), mutated.size());
			WriteInput(binPath, seed.Buffer.data(), seed.Buffer.size());
			mesh = MeshImporter::ImportGLTF(gltfPath);
		}
		else if (target == 1)
		{
			const Bytes buffer = mutator.MutateBytes(seed.Buffer);
			WriteInput(gltfPath, text.data(), text.size());
			WriteInput(binPath, buffer.data(), buffer.size());
			mesh = MeshImporter::ImportGLTF(gltfPath);
		}
		else
		{
			const Bytes bytes = mutator.MutateBytes(glb);
			WriteInput(glbPath, bytes.data(), bytes.size());
			mesh = MeshImporter::ImportGLTF(glbPath);
		}
		CheckMesh(mesh);
	});
}

TEST_CASE("Fuzz: damaged navmesh files are loaded or refused, never crash")
{
	// Two tiles of floor with a wall, so links between tiles, detail meshes and search trees are all there.
	NavMeshGeometry geometry;
	const auto quad = [&geometry](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
	{
		geometry.AddTriangle(a, b, c);
		geometry.AddTriangle(a, c, d);
	};
	quad({ -10.0f, 0.0f, -10.0f }, { -10.0f, 0.0f, 10.0f }, { 40.0f, 0.0f, 10.0f }, { 40.0f, 0.0f, -10.0f });
	quad({ -0.2f, 3.0f, -10.0f }, { -0.2f, 3.0f, 6.0f }, { 0.2f, 3.0f, 6.0f }, { 0.2f, 3.0f, -10.0f });
	quad({ -0.2f, 0.0f, -10.0f }, { -0.2f, 3.0f, -10.0f }, { -0.2f, 3.0f, 6.0f }, { -0.2f, 0.0f, 6.0f });
	quad({ 0.2f, 0.0f, -10.0f }, { 0.2f, 0.0f, 6.0f }, { 0.2f, 3.0f, 6.0f }, { 0.2f, 3.0f, -10.0f });
	std::string error;
	const Scope<NavMesh> baked = NavMesh::Build(geometry, {}, error);
	REQUIRE(baked);
	REQUIRE(baked->GetTileCount() == 2);
	const std::vector<uint8_t> saved = baked->Save();
	const Bytes seed(saved.begin(), saved.end());
	const std::filesystem::path path = GetFuzzDirectory("NavMeshes") / "Level.bnav";
	WriteInput(path, seed.data(), seed.size());
	REQUIRE(NavMesh::Load(saved, error));

	RunFuzzer(0x4E41u, 900, 1, [&](Mutator& mutator, size_t)
	{
		const Bytes bytes = mutator.MutateBytes(seed);
		WriteInput(path, bytes.data(), bytes.size());
		std::string reason;
		const Scope<NavMesh> navMesh = NavMesh::Load(std::vector<uint8_t>(bytes.begin(), bytes.end()), reason);
		if (!navMesh)
		{
			REQUIRE_FALSE(reason.empty());
			return;
		}
		// Whatever loads can be searched and drawn.
		navMesh->FindPath(glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(30.0f, 0.0f, 0.0f));
		navMesh->FindNearestPoint(glm::vec3(0.0f), 2.0f);
		navMesh->IsNavigable(glm::vec3(5.0f, 0.0f, 5.0f));
		std::vector<glm::vec3> triangles;
		navMesh->GetTriangles(triangles);
		REQUIRE(triangles.size() % 3 == 0);
	});
}

TEST_CASE("Fuzz: damaged fonts are loaded or refused, never crash")
{
	// A TrueType font, and a small CFF one (Tools/GenerateTestFonts.py), which most .otf files are.
	const std::vector<Bytes> seeds = {
		ReadFile(BellowsTests::GetSourceDirectory() / "Resources" / "Fonts" / "Inter" / "Inter-Regular.ttf"),
		ReadFile(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "CFFTest.otf"),
	};
	const std::filesystem::path input = GetFuzzDirectory("Fonts") / "Input.bin";
	// A small atlas keeps each load quick; damage to any glyph's outline is still found, since the
	// font's tables (where most damage lands) are read whatever characters are asked for.
	FontAtlasSettings settings;
	settings.GlyphSize = 12.0f;
	settings.DistanceRange = 2.0f;
	settings.Ranges = { { ' ', ' ' }, { 'A', 'E' }, { 'a', 'e' } };
	RunFuzzer(0xF047u, 400, seeds.size(), [&](Mutator& mutator, size_t seedIndex)
	{
		Bytes bytes = mutator.MutateBytes(seeds[seedIndex]);
		WriteInput(input, bytes.data(), bytes.size());
		const Ref<Font> font = Font::LoadFromMemory(std::move(bytes), "Fuzz", settings);
		if (!font)
			return;
		REQUIRE(std::isfinite(font->GetLineHeight()));
		REQUIRE(std::isfinite(font->GetGlyph('A').Advance));
		REQUIRE(std::isfinite(font->GetKerning('A', 'B')));
	});
}

TEST_CASE("Fuzz: damaged audio files are decoded or refused, never crash")
{
	const std::vector<Bytes> seeds = { MakeWav(), ReadFile(BellowsTests::GetSourceDirectory() / "BellowsTests" / "Data" / "Tone.ogg") };
	const std::filesystem::path input = GetFuzzDirectory("Audio") / "Input.bin";
	// Audio Sources load through miniaudio's decoders, which read a file the same way they read memory.
	RunFuzzer(0xA0D1u, 800, seeds.size(), [&](Mutator& mutator, size_t seedIndex)
	{
		const Bytes bytes = mutator.MutateBytes(seeds[seedIndex]);
		WriteInput(input, bytes.data(), bytes.size());
		ma_decoder decoder;
		if (ma_decoder_init_memory(bytes.data(), bytes.size(), nullptr, &decoder) != MA_SUCCESS)
			return;
		// Read it all, as MA_SOUND_FLAG_DECODE does, up to a minute of 48 kHz stereo: damage can make a
		// file claim to be hours long.
		std::vector<float> samples(4096 * 8);
		ma_uint64 total = 0;
		while (total < 48000ull * 60)
		{
			ma_uint64 read = 0;
			if (ma_decoder_read_pcm_frames(&decoder, samples.data(), 4096, &read) != MA_SUCCESS || read == 0)
				break;
			total += read;
		}
		ma_decoder_uninit(&decoder);
	});
}

TEST_CASE("Fuzz: damaged scenes and prefabs are loaded or refused, never crash")
{
	const std::filesystem::path project = BellowsTests::GetSourceDirectory() / "Projects" / "FeatureTest" / "Assets";
	const Json scene = ReadJsonFile(project / "Scenes" / "FeatureTest.bscene");
	const Json prefab = ReadJsonFile(project / "Prefabs" / "Ball.bprefab");
	// An asset folder of its own, so references in a damaged scene find nothing to load.
	const std::filesystem::path assets = GetFuzzDirectory("Scenes");
	AssetManager::SetAssetDirectory(assets);

	RunFuzzer(0x5CE7u, 300, 2, [&](Mutator& mutator, size_t target)
	{
		Ref<Scene> loaded = CreateRef<Scene>("Fuzz");
		if (target == 0)
		{
			const std::string text = mutator.MutateJson(scene);
			WriteInput(assets / "Input.bscene", text.data(), text.size());
			SceneSerializer::Deserialize(*loaded, assets / "Input.bscene");
		}
		else
		{
			const std::string text = mutator.MutateJson(prefab);
			WriteInput(assets / "Input.bprefab", text.data(), text.size());
			SceneSerializer::InstantiatePrefab(*loaded, "Input.bprefab");
		}
		// Whatever loaded can be saved again and updated.
		const Json saved = SceneSerializer::SerializeToJson(*loaded);
		REQUIRE(saved.is_object());
		loaded->OnUpdateEditor(1.0f / 60.0f);
	});
	AssetManager::SetAssetDirectory({});
}

TEST_CASE("Fuzz: damaged materials, themes, import settings and project files are read or refused, never crash")
{
	const std::filesystem::path source = BellowsTests::GetSourceDirectory() / "Projects" / "FeatureTest";
	const Json material = ReadJsonFile(source / "Assets" / "Materials" / "Crate.bmat");
	const Json project = ReadJsonFile(source / "FeatureTest.bproject");
	const Json theme = Json::parse(WriteUITheme(GetDefaultUITheme()));
	Json importSettings;
	importSettings["Version"] = 1;
	importSettings["FrontFaces"] = "+Z";

	const std::filesystem::path folder = GetFuzzDirectory("Settings");
	AssetManager::SetAssetDirectory(folder);
	RunFuzzer(0x5E77u, 800, 4, [&](Mutator& mutator, size_t target)
	{
		switch (target)
		{
			case 0:
			{
				const std::string text = mutator.MutateJson(material);
				WriteInput(folder / "Input.bmat", text.data(), text.size());
				AssetManager::LoadMaterialFile(folder / "Input.bmat");
				break;
			}
			case 1:
			{
				const std::string text = mutator.MutateJson(theme);
				WriteInput(folder / "Input.btheme", text.data(), text.size());
				std::string error, warnings;
				ParseUITheme(text, &error, &warnings);
				break;
			}
			case 2:
			{
				const std::string text = mutator.MutateJson(importSettings);
				WriteInput(folder / "Model.glb.bimport", text.data(), text.size());
				MeshImportSettings::Load(folder / "Model.glb");
				break;
			}
			default:
			{
				const std::string text = mutator.MutateJson(project);
				WriteInput(folder / "Input.bproject", text.data(), text.size());
				Project::Load(folder / "Input.bproject");
				break;
			}
		}
	});
	AssetManager::SetAssetDirectory({});
}
