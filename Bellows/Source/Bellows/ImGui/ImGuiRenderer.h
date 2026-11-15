#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

struct ImDrawData;
struct ImTextureData;

namespace Bellows {

	// Renders Dear ImGui draw data with NVRHI. Supports ImGuiBackendFlags_RendererHasTextures,
	// so fonts can be rebuilt at any size at runtime. ImTextureID values are nvrhi::ITexture pointers.
	class ImGuiRenderer
	{
	public:
		ImGuiRenderer();
		~ImGuiRenderer();

		void Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer, bool clear);
		void Shutdown();
	private:
		void UpdateTexture(ImTextureData* texture, nvrhi::ICommandList* commandList);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		void CreatePipeline(const nvrhi::FramebufferInfo& framebufferInfo);
	private:
		struct CachedBindingSet
		{
			nvrhi::TextureHandle Texture;
			nvrhi::BindingSetHandle BindingSet;
			uint64_t LastUsedFrame = 0;
		};

		nvrhi::CommandListHandle m_CommandList;
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::FramebufferInfo m_PipelineFramebufferInfo;

		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		size_t m_VertexCapacity = 0;
		size_t m_IndexCapacity = 0;

		std::unordered_map<nvrhi::ITexture*, CachedBindingSet> m_BindingSets;
		uint64_t m_FrameCounter = 0;
	};

}
