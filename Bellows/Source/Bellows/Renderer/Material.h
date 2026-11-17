#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <string>

namespace Bellows {

	enum class AlphaMode : uint8_t
	{
		Opaque = 0,
		Mask,		// Alpha-tested against AlphaCutoff
		Blend,		// Alpha-blended, rendered in the transparent pass
	};

	const char* AlphaModeToString(AlphaMode mode);
	AlphaMode AlphaModeFromString(std::string_view text);

	// Scalar PBR parameters of a material (metallic/roughness workflow).
	struct MaterialProperties
	{
		glm::vec4 AlbedoColor { 0.8f, 0.8f, 0.8f, 1.0f };	// Linear RGBA
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		glm::vec3 EmissiveColor { 0.0f };	// Linear RGB
		float EmissiveIntensity = 1.0f;
		float NormalStrength = 1.0f;
		float AlphaCutoff = 0.5f;
		glm::vec2 UVTiling { 1.0f };
		glm::vec2 UVOffset { 0.0f };
		AlphaMode Mode = AlphaMode::Opaque;
		bool DoubleSided = false;

		bool operator==(const MaterialProperties&) const = default;
	};

	// A PBR material: parameters plus optional texture maps.
	// GPU resources are created lazily and refreshed automatically after edits.
	class Material
	{
	public:
		explicit Material(std::string name = "Material");

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		const MaterialProperties& GetProperties() const { return m_Properties; }
		// Returns mutable properties and schedules a GPU upload.
		MaterialProperties& EditProperties() { m_Dirty = true; return m_Properties; }
		void SetProperties(const MaterialProperties& properties) { m_Properties = properties; m_Dirty = true; }

		const Ref<Texture2D>& GetAlbedoMap() const { return m_AlbedoMap; }
		const Ref<Texture2D>& GetNormalMap() const { return m_NormalMap; }
		const Ref<Texture2D>& GetMetallicRoughnessMap() const { return m_MetallicRoughnessMap; }
		const Ref<Texture2D>& GetOcclusionMap() const { return m_OcclusionMap; }
		const Ref<Texture2D>& GetEmissiveMap() const { return m_EmissiveMap; }
		// Grayscale maps, as downloaded PBR texture sets ship them; they multiply Roughness / Metallic
		// (and the packed metallic-roughness map, when both are set).
		const Ref<Texture2D>& GetRoughnessMap() const { return m_RoughnessMap; }
		const Ref<Texture2D>& GetMetallicMap() const { return m_MetallicMap; }

		void SetAlbedoMap(Ref<Texture2D> texture) { m_AlbedoMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetNormalMap(Ref<Texture2D> texture) { m_NormalMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetMetallicRoughnessMap(Ref<Texture2D> texture) { m_MetallicRoughnessMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetOcclusionMap(Ref<Texture2D> texture) { m_OcclusionMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetEmissiveMap(Ref<Texture2D> texture) { m_EmissiveMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetRoughnessMap(Ref<Texture2D> texture) { m_RoughnessMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }
		void SetMetallicMap(Ref<Texture2D> texture) { m_MetallicMap = std::move(texture); m_BindingSetDirty = true; m_Dirty = true; }

		// Texture asset paths (project-relative) used for serialization.
		std::string AlbedoMapPath, NormalMapPath, MetallicRoughnessMapPath, OcclusionMapPath, EmissiveMapPath, RoughnessMapPath, MetallicMapPath;

		// Uploads changed parameters and returns the binding set for set 1 of the mesh passes.
		nvrhi::IBindingSet* Prepare(nvrhi::ICommandList* commandList);

		Ref<Material> Clone() const;
	private:
		std::string m_Name;
		MaterialProperties m_Properties;
		Ref<Texture2D> m_AlbedoMap, m_NormalMap, m_MetallicRoughnessMap, m_OcclusionMap, m_EmissiveMap, m_RoughnessMap, m_MetallicMap;

		nvrhi::BufferHandle m_ConstantBuffer;
		nvrhi::BindingSetHandle m_BindingSet;
		bool m_Dirty = true;
		bool m_BindingSetDirty = true;
	};

}
