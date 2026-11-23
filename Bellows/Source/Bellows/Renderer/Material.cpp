#include "bwpch.h"
#include "Bellows/Renderer/Material.h"

#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Renderer/ShaderInterop.h"

namespace Bellows {

	const char* AlphaModeToString(AlphaMode mode)
	{
		switch (mode)
		{
			case AlphaMode::Opaque:	return "Opaque";
			case AlphaMode::Mask:	return "Mask";
			case AlphaMode::Blend:	return "Blend";
		}
		return "Opaque";
	}

	AlphaMode AlphaModeFromString(std::string_view text)
	{
		if (text == "Mask")
			return AlphaMode::Mask;
		if (text == "Blend")
			return AlphaMode::Blend;
		return AlphaMode::Opaque;
	}

	const char* TextureFilterToString(TextureFilter filter)
	{
		return filter == TextureFilter::Nearest ? "Nearest" : "Linear";
	}

	std::optional<TextureFilter> TextureFilterFromString(std::string_view text)
	{
		if (text == "Linear")
			return TextureFilter::Linear;
		if (text == "Nearest")
			return TextureFilter::Nearest;
		return std::nullopt;
	}

	const char* TextureWrapToString(TextureWrap wrap)
	{
		switch (wrap)
		{
			case TextureWrap::Repeat:	return "Repeat";
			case TextureWrap::Clamp:	return "Clamp";
			case TextureWrap::Mirror:	return "Mirror";
		}
		return "Repeat";
	}

	std::optional<TextureWrap> TextureWrapFromString(std::string_view text)
	{
		if (text == "Repeat")
			return TextureWrap::Repeat;
		if (text == "Clamp")
			return TextureWrap::Clamp;
		if (text == "Mirror")
			return TextureWrap::Mirror;
		return std::nullopt;
	}

	Material::Material(std::string name)
		: m_Name(std::move(name))
	{
	}

	nvrhi::IBindingSet* Material::Prepare(nvrhi::ICommandList* commandList)
	{
		nvrhi::IDevice* device = Renderer::GetDevice();

		if (!m_ConstantBuffer)
		{
			m_ConstantBuffer = device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(sizeof(ShaderInterop::MaterialData))
				.setIsConstantBuffer(true)
				.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
				.setKeepInitialState(true)
				.setDebugName("MaterialConstants_" + m_Name));
			m_Dirty = true;
			m_BindingSetDirty = true;
		}

		if (m_Dirty)
		{
			ShaderInterop::MaterialData data {};
			data.AlbedoColor = m_Properties.AlbedoColor;
			data.EmissiveColor = glm::vec4(m_Properties.EmissiveColor * m_Properties.EmissiveIntensity, 1.0f);
			data.Params = glm::vec4(m_Properties.Metallic, m_Properties.Roughness, m_Properties.NormalStrength, m_Properties.AlphaCutoff);
			data.UVTransform = glm::vec4(m_Properties.UVTiling, m_Properties.UVOffset);

			uint32_t textureBits = 0;
			if (m_AlbedoMap) textureBits |= ShaderInterop::TextureAlbedo;
			if (m_NormalMap) textureBits |= ShaderInterop::TextureNormal;
			if (m_MetallicRoughnessMap) textureBits |= ShaderInterop::TextureMetallicRoughness;
			if (m_OcclusionMap) textureBits |= ShaderInterop::TextureOcclusion;
			if (m_EmissiveMap) textureBits |= ShaderInterop::TextureEmissive;
			if (m_RoughnessMap) textureBits |= ShaderInterop::TextureRoughness;
			if (m_MetallicMap) textureBits |= ShaderInterop::TextureMetallic;
			data.Flags = glm::uvec4(static_cast<uint32_t>(m_Properties.Mode), textureBits, 0u, 0u);

			commandList->writeBuffer(m_ConstantBuffer, &data, sizeof(data));
			m_Dirty = false;
		}

		nvrhi::ISampler* sampler = Renderer::GetMaterialSampler(m_Properties.Filter, m_Properties.WrapU, m_Properties.WrapV);
		if (m_BindingSetDirty || !m_BindingSet || sampler != m_BoundSampler)
		{
			auto textureOr = [](const Ref<Texture2D>& texture, nvrhi::ITexture* fallback)
			{
				return texture ? texture->GetHandle() : fallback;
			};

			nvrhi::ITexture* white = Renderer::GetWhiteTexture()->GetHandle();
			nvrhi::ITexture* normal = Renderer::GetFlatNormalTexture()->GetHandle();

			nvrhi::BindingSetDesc desc;
			desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, textureOr(m_AlbedoMap, white)));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(2, textureOr(m_NormalMap, normal)));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, textureOr(m_MetallicRoughnessMap, white)));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(4, textureOr(m_OcclusionMap, white)));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(5, textureOr(m_EmissiveMap, white)));
			desc.addItem(nvrhi::BindingSetItem::Sampler(6, sampler));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(7, textureOr(m_RoughnessMap, white)));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(8, textureOr(m_MetallicMap, white)));

			m_BindingSet = device->createBindingSet(desc, Renderer::GetMaterialBindingLayout());
			m_BindingSetDirty = false;
			m_BoundSampler = sampler;
		}

		return m_BindingSet;
	}

	Ref<Material> Material::Clone() const
	{
		Ref<Material> clone = CreateRef<Material>(m_Name);
		clone->m_Properties = m_Properties;
		clone->m_AlbedoMap = m_AlbedoMap;
		clone->m_NormalMap = m_NormalMap;
		clone->m_MetallicRoughnessMap = m_MetallicRoughnessMap;
		clone->m_OcclusionMap = m_OcclusionMap;
		clone->m_EmissiveMap = m_EmissiveMap;
		clone->m_RoughnessMap = m_RoughnessMap;
		clone->m_MetallicMap = m_MetallicMap;
		clone->AlbedoMapPath = AlbedoMapPath;
		clone->NormalMapPath = NormalMapPath;
		clone->MetallicRoughnessMapPath = MetallicRoughnessMapPath;
		clone->OcclusionMapPath = OcclusionMapPath;
		clone->EmissiveMapPath = EmissiveMapPath;
		clone->RoughnessMapPath = RoughnessMapPath;
		clone->MetallicMapPath = MetallicMapPath;
		return clone;
	}

}
