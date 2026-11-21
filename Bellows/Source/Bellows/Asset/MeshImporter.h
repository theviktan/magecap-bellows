#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Mesh.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace Bellows {

	// Which way a model's front faces in its file. Bellows' forward is -Z; glTF's convention (and
	// Blender's exporter) puts the front at +Z, so such a model is turned 180 degrees about its up axis.
	enum class ModelFront : uint8_t
	{
		NegativeZ,	// As made: the model already faces Bellows' forward
		PositiveZ	// Turned at import, around its own origin, to face forward
	};

	const char* ModelFrontToString(ModelFront front);	// "-Z" or "+Z"
	std::optional<ModelFront> ModelFrontFromString(std::string_view text);

	// A model's import settings, kept beside it in '<model file>.bimport' (JSON).
	struct MeshImportSettings
	{
		ModelFront Front = ModelFront::NegativeZ;

		static std::filesystem::path GetFilePath(const std::filesystem::path& modelPath);
		// The model's settings file, or nothing when it has none; a damaged file is logged and ignored.
		static std::optional<MeshImportSettings> Load(const std::filesystem::path& modelPath);
		bool Save(const std::filesystem::path& modelPath) const;
	};

	// Imports glTF 2.0 files (.gltf with external/embedded buffers, or binary .glb).
	// All mesh primitives of the default scene become submeshes with baked node transforms;
	// glTF PBR metallic-roughness materials and their textures are imported as Materials.
	class MeshImporter
	{
	public:
		static Ref<Mesh> ImportGLTF(const std::filesystem::path& path, const MeshImportSettings& settings = {});
	};

}
