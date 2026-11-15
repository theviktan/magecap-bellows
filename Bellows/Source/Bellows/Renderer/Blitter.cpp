#include "bwpch.h"
#include "Bellows/Renderer/Blitter.h"

#include "Bellows/Renderer/Renderer.h"

namespace Bellows {

	Blitter::Blitter()
	{
		m_Constants = Renderer::GetDevice()->createBuffer(nvrhi::BufferDesc()
			.setByteSize(16).setIsConstantBuffer(true).setIsVolatile(true).setMaxVersions(16).setDebugName("BlitConstants"));
	}

	nvrhi::GraphicsPipelineHandle Blitter::GetPipeline(nvrhi::IFramebuffer* framebuffer)
	{
		const nvrhi::FramebufferInfo& info = framebuffer->getFramebufferInfo();
		for (const auto& [pipelineInfo, pipeline] : m_Pipelines)
		{
			if (pipelineInfo == info)
				return pipeline;
		}

		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = Renderer::GetShader("Fullscreen.vert");
		desc.PS = Renderer::GetShader("Blit.frag");
		desc.bindingLayouts = { Renderer::GetPostProcessBindingLayout() };
		desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		nvrhi::GraphicsPipelineHandle pipeline = Renderer::GetDevice()->createGraphicsPipeline(desc, info);
		m_Pipelines.emplace_back(info, pipeline);
		return pipeline;
	}

	void Blitter::Blit(nvrhi::ICommandList* commandList, nvrhi::ITexture* source, nvrhi::IFramebuffer* destination)
	{
		auto it = m_BindingSets.find(source);
		if (it == m_BindingSets.end())
		{
			// Bound textures change rarely (render-target resizes); drop stale entries when that happens.
			if (m_BindingSets.size() > 8)
				m_BindingSets.clear();

			nvrhi::ITexture* white = Renderer::GetWhiteTexture()->GetHandle();
			nvrhi::BindingSetDesc desc;
			desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_Constants));
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, source));
			for (uint32_t slot = 2; slot <= 4; slot++)
				desc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, white));
			desc.addItem(nvrhi::BindingSetItem::Sampler(5, Renderer::GetLinearClampSampler()));
			desc.addItem(nvrhi::BindingSetItem::Sampler(6, Renderer::GetPointClampSampler()));
			nvrhi::BindingSetHandle bindingSet = Renderer::GetDevice()->createBindingSet(desc, Renderer::GetPostProcessBindingLayout());
			it = m_BindingSets.emplace(source, std::make_pair(nvrhi::TextureHandle(source), bindingSet)).first;
		}

		const float unused[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		commandList->writeBuffer(m_Constants, unused, sizeof(unused));

		const nvrhi::FramebufferInfoEx& info = destination->getFramebufferInfo();
		nvrhi::GraphicsState state;
		state.pipeline = GetPipeline(destination);
		state.framebuffer = destination;
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height)));
		state.bindings = { it->second.second };
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

}
