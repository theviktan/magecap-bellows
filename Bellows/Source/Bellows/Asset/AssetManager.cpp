#include "bwpch.h"
#include "Bellows/Asset/AssetManager.h"

#include "Bellows/Asset/MeshImporter.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Serialization/JsonUtils.h"

namespace Bellows {

	namespace {

		struct AssetCache
		{
			std::filesystem::path AssetDirectory;
			std::unordered_map<std::string, Ref<Mesh>> Meshes;
			std::unordered_map<std::string, Ref<Material>> Materials;
			std::unordered_map<std::string, Ref<Texture2D>> Textures;
			std::unordered_map<std::string, Ref<Environment>> Environments;
			std::unordered_set<std::string> ReportedMissing;
		};

		AssetCache& GetCache()
		{
			static AssetCache s_Cache;
			return s_Cache;
		}

		constexpr std::string_view c_BuiltinPrefix = "Builtin/";

		std::string ToLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		void ReportMissing(const std::string& assetPath, const char* kind)
		{
			if (GetCache().ReportedMissing.insert(assetPath).second)
				BW_CORE_ERROR("{} asset '{}' could not be loaded", kind, assetPath);
		}

	}

	const char* AssetTypeToString(AssetType type)
	{
		switch (type)
		{
			case AssetType::None:			return "None";
			case AssetType::Scene:			return "Scene";
			case AssetType::Prefab:			return "Prefab";
			case AssetType::Mesh:			return "Mesh";
			case AssetType::Material:		return "Material";
			case AssetType::Texture:		return "Texture";
			case AssetType::Environment:	return "Environment";
			case AssetType::Audio:			return "Audio";
			case AssetType::Script:			return "Script";
		}
		return "None";
	}

	void AssetManager::SetAssetDirectory(const std::filesystem::path& directory)
	{
		AssetCache& cache = GetCache();
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(directory, error);
		if (cache.AssetDirectory != absolute)
		{
			ClearCache();
			cache.AssetDirectory = absolute;
		}
	}

	const std::filesystem::path& AssetManager::GetAssetDirectory()
	{
		return GetCache().AssetDirectory;
	}

	std::filesystem::path AssetManager::GetAbsolutePath(const std::string& assetPath)
	{
		const std::filesystem::path path(reinterpret_cast<const char8_t*>(assetPath.c_str()));
		if (path.is_absolute())
			return path;
		return GetCache().AssetDirectory / path;
	}

	std::string AssetManager::GetAssetPath(const std::filesystem::path& absolutePath)
	{
		return FileSystem::MakeRelativeGenericPath(absolutePath, GetCache().AssetDirectory);
	}

	bool AssetManager::Exists(const std::string& assetPath)
	{
		if (IsBuiltinPath(assetPath))
		{
			const auto& builtins = GetBuiltinMeshPaths();
			return std::find(builtins.begin(), builtins.end(), assetPath) != builtins.end() || assetPath == "Builtin/DefaultMaterial";
		}
		return !assetPath.empty() && FileSystem::Exists(GetAbsolutePath(assetPath));
	}

	AssetType AssetManager::GetAssetType(const std::filesystem::path& path)
	{
		const std::string extension = ToLower(path.extension().string());
		if (extension == ".bscene") return AssetType::Scene;
		if (extension == ".bprefab") return AssetType::Prefab;
		if (extension == ".gltf" || extension == ".glb") return AssetType::Mesh;
		if (extension == ".bmat") return AssetType::Material;
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp") return AssetType::Texture;
		if (extension == ".hdr") return AssetType::Environment;
		if (extension == ".wav" || extension == ".mp3" || extension == ".flac" || extension == ".ogg") return AssetType::Audio;
		if (extension == ".forge" || extension == ".py") return AssetType::Script;
		return AssetType::None;
	}

	bool AssetManager::IsBuiltinPath(std::string_view assetPath)
	{
		return assetPath.rfind(c_BuiltinPrefix, 0) == 0;
	}

	const std::vector<std::string>& AssetManager::GetBuiltinMeshPaths()
	{
		static const std::vector<std::string> s_Paths = {
			"Builtin/Cube", "Builtin/Sphere", "Builtin/Plane", "Builtin/Capsule", "Builtin/Cylinder", "Builtin/Cone", "Builtin/Quad",
		};
		return s_Paths;
	}

	Ref<Mesh> AssetManager::GetMesh(const std::string& assetPath)
	{
		if (assetPath.empty())
			return nullptr;

		AssetCache& cache = GetCache();
		auto it = cache.Meshes.find(assetPath);
		if (it != cache.Meshes.end())
			return it->second;

		Ref<Mesh> mesh;
		if (IsBuiltinPath(assetPath))
		{
			const std::string_view name = std::string_view(assetPath).substr(c_BuiltinPrefix.size());
			for (uint8_t i = 0; i <= static_cast<uint8_t>(PrimitiveMesh::Quad); i++)
			{
				const PrimitiveMesh primitive = static_cast<PrimitiveMesh>(i);
				if (name == PrimitiveMeshToString(primitive))
					mesh = Renderer::GetPrimitiveMesh(primitive);
			}
		}
		else if (GetAssetType(assetPath) == AssetType::Mesh)
		{
			mesh = MeshImporter::ImportGLTF(GetAbsolutePath(assetPath));
		}

		if (!mesh)
		{
			ReportMissing(assetPath, "Mesh");
			return nullptr;
		}
		cache.Meshes[assetPath] = mesh;
		return mesh;
	}

	Ref<Material> AssetManager::GetMaterial(const std::string& assetPath)
	{
		if (assetPath.empty())
			return nullptr;

		AssetCache& cache = GetCache();
		auto it = cache.Materials.find(assetPath);
		if (it != cache.Materials.end())
			return it->second;

		Ref<Material> material;
		if (assetPath == "Builtin/DefaultMaterial")
			material = CreateRef<Material>("Default");
		else
			material = LoadMaterialFile(GetAbsolutePath(assetPath));

		if (!material)
		{
			ReportMissing(assetPath, "Material");
			return nullptr;
		}
		cache.Materials[assetPath] = material;
		return material;
	}

	Ref<Texture2D> AssetManager::GetTexture(const std::string& assetPath, TextureColorSpace colorSpace)
	{
		if (assetPath.empty())
			return nullptr;

		AssetCache& cache = GetCache();
		const std::string key = assetPath + (colorSpace == TextureColorSpace::SRGB ? "|srgb" : "|linear");
		auto it = cache.Textures.find(key);
		if (it != cache.Textures.end())
			return it->second;

		Ref<Texture2D> texture = Texture2D::LoadFromFile(GetAbsolutePath(assetPath), colorSpace);
		if (!texture)
		{
			ReportMissing(assetPath, "Texture");
			return nullptr;
		}
		cache.Textures[key] = texture;
		return texture;
	}

	Ref<Environment> AssetManager::GetEnvironment(const std::string& assetPath)
	{
		if (assetPath.empty())
			return nullptr;

		AssetCache& cache = GetCache();
		auto it = cache.Environments.find(assetPath);
		if (it != cache.Environments.end())
			return it->second;

		Ref<Environment> environment = Environment::LoadFromFile(GetAbsolutePath(assetPath));
		if (!environment)
		{
			ReportMissing(assetPath, "Environment");
			return nullptr;
		}
		cache.Environments[assetPath] = environment;
		return environment;
	}

	void AssetManager::RegisterMaterial(const std::string& assetPath, const Ref<Material>& material)
	{
		GetCache().Materials[assetPath] = material;
	}

	bool AssetManager::SaveMaterial(const Ref<Material>& material, const std::string& assetPath)
	{
		if (!material || assetPath.empty() || IsBuiltinPath(assetPath))
			return false;

		const MaterialProperties& properties = material->GetProperties();
		Json json;
		json["Name"] = material->GetName();
		json["AlbedoColor"] = properties.AlbedoColor;
		json["Metallic"] = properties.Metallic;
		json["Roughness"] = properties.Roughness;
		json["EmissiveColor"] = properties.EmissiveColor;
		json["EmissiveIntensity"] = properties.EmissiveIntensity;
		json["NormalStrength"] = properties.NormalStrength;
		json["AlphaMode"] = AlphaModeToString(properties.Mode);
		json["AlphaCutoff"] = properties.AlphaCutoff;
		json["DoubleSided"] = properties.DoubleSided;
		json["UVTiling"] = properties.UVTiling;
		json["UVOffset"] = properties.UVOffset;
		json["AlbedoMap"] = material->AlbedoMapPath;
		json["NormalMap"] = material->NormalMapPath;
		json["MetallicRoughnessMap"] = material->MetallicRoughnessMapPath;
		json["OcclusionMap"] = material->OcclusionMapPath;
		json["EmissiveMap"] = material->EmissiveMapPath;

		if (!FileSystem::WriteTextFile(GetAbsolutePath(assetPath), json.dump(1, '\t')))
		{
			BW_CORE_ERROR("Failed to save material '{}'", assetPath);
			return false;
		}
		GetCache().Materials[assetPath] = material;
		GetCache().ReportedMissing.erase(assetPath);
		return true;
	}

	Ref<Material> AssetManager::LoadMaterialFile(const std::filesystem::path& absolutePath)
	{
		const std::optional<std::string> text = FileSystem::ReadTextFile(absolutePath);
		if (!text)
			return nullptr;

		Json json = Json::parse(*text, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			BW_CORE_ERROR("Material file '{}' is not valid JSON", absolutePath.string());
			return nullptr;
		}

		Ref<Material> material = CreateRef<Material>(absolutePath.stem().string());
		std::string name = material->GetName();
		JsonRead(json, "Name", name);
		material->SetName(name);

		MaterialProperties properties;
		JsonRead(json, "AlbedoColor", properties.AlbedoColor);
		JsonRead(json, "Metallic", properties.Metallic);
		JsonRead(json, "Roughness", properties.Roughness);
		JsonRead(json, "EmissiveColor", properties.EmissiveColor);
		JsonRead(json, "EmissiveIntensity", properties.EmissiveIntensity);
		JsonRead(json, "NormalStrength", properties.NormalStrength);
		JsonRead(json, "AlphaCutoff", properties.AlphaCutoff);
		JsonRead(json, "DoubleSided", properties.DoubleSided);
		JsonRead(json, "UVTiling", properties.UVTiling);
		JsonRead(json, "UVOffset", properties.UVOffset);
		std::string alphaMode = "Opaque";
		JsonRead(json, "AlphaMode", alphaMode);
		properties.Mode = AlphaModeFromString(alphaMode);
		material->SetProperties(properties);

		JsonRead(json, "AlbedoMap", material->AlbedoMapPath);
		JsonRead(json, "NormalMap", material->NormalMapPath);
		JsonRead(json, "MetallicRoughnessMap", material->MetallicRoughnessMapPath);
		JsonRead(json, "OcclusionMap", material->OcclusionMapPath);
		JsonRead(json, "EmissiveMap", material->EmissiveMapPath);

		material->SetAlbedoMap(GetTexture(material->AlbedoMapPath, TextureColorSpace::SRGB));
		material->SetNormalMap(GetTexture(material->NormalMapPath, TextureColorSpace::Linear));
		material->SetMetallicRoughnessMap(GetTexture(material->MetallicRoughnessMapPath, TextureColorSpace::Linear));
		material->SetOcclusionMap(GetTexture(material->OcclusionMapPath, TextureColorSpace::Linear));
		material->SetEmissiveMap(GetTexture(material->EmissiveMapPath, TextureColorSpace::SRGB));
		return material;
	}

	void AssetManager::Invalidate(const std::string& assetPath)
	{
		AssetCache& cache = GetCache();
		cache.Meshes.erase(assetPath);
		cache.Materials.erase(assetPath);
		cache.Environments.erase(assetPath);
		cache.Textures.erase(assetPath + "|srgb");
		cache.Textures.erase(assetPath + "|linear");
		cache.ReportedMissing.erase(assetPath);
	}

	void AssetManager::ClearCache()
	{
		AssetCache& cache = GetCache();
		cache.Meshes.clear();
		cache.Materials.clear();
		cache.Textures.clear();
		cache.Environments.clear();
		cache.ReportedMissing.clear();
	}

}
