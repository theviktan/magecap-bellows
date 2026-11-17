#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Renderer/Texture.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Bellows {

	enum class AssetType : uint8_t
	{
		None = 0,
		Scene,			// .bscene
		Prefab,			// .bprefab
		Mesh,			// .gltf .glb
		Material,		// .bmat
		Texture,		// .png .jpg .jpeg .tga .bmp
		Environment,	// .hdr
		Audio,			// .wav .mp3 .flac .ogg
		Script,			// .py
	};

	const char* AssetTypeToString(AssetType type);

	// Loads and caches assets by project-relative path ("Models/Ship.glb").
	// Paths starting with "Builtin/" refer to engine-provided assets (primitive meshes, default material).
	class AssetManager
	{
	public:
		static void SetAssetDirectory(const std::filesystem::path& directory);
		static const std::filesystem::path& GetAssetDirectory();

		static std::filesystem::path GetAbsolutePath(const std::string& assetPath);
		// Converts an absolute path inside the asset directory to an asset path (empty if outside).
		static std::string GetAssetPath(const std::filesystem::path& absolutePath);
		static bool Exists(const std::string& assetPath);
		static AssetType GetAssetType(const std::filesystem::path& path);
		// True when the file can fill a slot of the given type. Besides its own type, an OpenEXR image
		// (a Texture) can be a sky Environment, and a .hdr image (an Environment) can be a material map.
		static bool CanUseAs(const std::filesystem::path& path, AssetType type);

		static bool IsBuiltinPath(std::string_view assetPath);
		static const std::vector<std::string>& GetBuiltinMeshPaths();

		static Ref<Mesh> GetMesh(const std::string& assetPath);
		static Ref<Material> GetMaterial(const std::string& assetPath);
		static Ref<Texture2D> GetTexture(const std::string& assetPath, TextureColorSpace colorSpace);
		static Ref<Environment> GetEnvironment(const std::string& assetPath);

		// Registers an in-memory material under a path (e.g. materials created by scripts at runtime).
		static void RegisterMaterial(const std::string& assetPath, const Ref<Material>& material);

		static bool SaveMaterial(const Ref<Material>& material, const std::string& assetPath);
		static Ref<Material> LoadMaterialFile(const std::filesystem::path& absolutePath);

		// Drops one cached asset (it is reloaded on next use) or everything.
		static void Invalidate(const std::string& assetPath);
		static void ClearCache();
	};

}
