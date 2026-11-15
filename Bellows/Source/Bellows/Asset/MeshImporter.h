#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Mesh.h"

#include <filesystem>

namespace Bellows {

	// Imports glTF 2.0 files (.gltf with external/embedded buffers, or binary .glb).
	// All mesh primitives of the default scene become submeshes with baked node transforms;
	// glTF PBR metallic-roughness materials and their textures are imported as Materials.
	class MeshImporter
	{
	public:
		static Ref<Mesh> ImportGLTF(const std::filesystem::path& path);
	};

}
