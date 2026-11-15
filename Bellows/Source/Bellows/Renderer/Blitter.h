#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>

namespace Bellows {

	// Copies a texture into any framebuffer with a fullscreen pass (scaling with bilinear filtering).
	class Blitter
	{
	public:
		Blitter();

		void Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* destination);
	private:
		nvrhi::GraphicsPipelineHandle GetPipeline(nvrhi::IFramebuffer* framebuffer);
	private:
		nvrhi::BufferHandle m_Constants;
		std::vector<std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>> m_Pipelines;
		std::unordered_map<nvrhi::ITexture*, std::pair<nvrhi::TextureHandle, nvrhi::BindingSetHandle>> m_BindingSets;
	};

}
