#include "bwpch.h"
#include "Bellows/ImGui/ImGuiRenderer.h"

#include "Bellows/Renderer/Renderer.h"

#include <imgui.h>

namespace Bellows {

	namespace {

		constexpr uint32_t c_PushConstantSlot = 31;

		struct ImGuiPushConstants
		{
			float Scale[2];
			float Translate[2];
		};

		// Backend storage for textures created on behalf of Dear ImGui (the font atlas).
		struct ImGuiTextureBackendData
		{
			nvrhi::TextureHandle Texture;
		};

		// Identifier for ImGui's "reset render state" draw callback. The full state is set for every draw, so it needs no work.
		void ResetRenderStateCallback(const ImDrawList*, const ImDrawCmd*) {}

	}

	ImGuiRenderer::ImGuiRenderer()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();
		m_CommandList = device->createCommandList();
		m_VertexShader = Renderer::GetShader("ImGui.vert");
		m_PixelShader = Renderer::GetShader("ImGui.frag");

		const nvrhi::VertexAttributeDesc attributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, pos)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(ImDrawVert, uv)).setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA8_UNORM).setOffset(offsetof(ImDrawVert, col)).setElementStride(sizeof(ImDrawVert)),
		};
		m_InputLayout = device->createInputLayout(attributes, 3, m_VertexShader);

		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.visibility = nvrhi::ShaderType::All;
		layoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets().setShaderResourceOffset(0).setSamplerOffset(0).setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);
		layoutDesc.setRegisterSpaceAndDescriptorSet(0);
		layoutDesc.addItem(nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, sizeof(ImGuiPushConstants)));
		layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		layoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(2));
		m_BindingLayout = device->createBindingLayout(layoutDesc);

		m_Sampler = device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));

		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = "Bellows NVRHI";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
		ImGui::GetPlatformIO().DrawCallback_ResetRenderState = ResetRenderStateCallback;
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		Shutdown();
	}

	void ImGuiRenderer::Shutdown()
	{
		if (ImGui::GetCurrentContext())
		{
			ImGui::GetPlatformIO().DrawCallback_ResetRenderState = nullptr;
			for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
			{
				if (texture->RefCount == 1 && texture->BackendUserData)
				{
					delete static_cast<ImGuiTextureBackendData*>(texture->BackendUserData);
					texture->BackendUserData = nullptr;
					texture->SetTexID(ImTextureID_Invalid);
					texture->SetStatus(ImTextureStatus_Destroyed);
				}
			}
		}
		m_BindingSets.clear();
		m_Pipeline = nullptr;
	}

	void ImGuiRenderer::CreatePipeline(const nvrhi::FramebufferInfo& framebufferInfo)
	{
		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = m_VertexShader;
		desc.PS = m_PixelShader;
		desc.inputLayout = m_InputLayout;
		desc.primType = nvrhi::PrimitiveType::TriangleList;
		desc.bindingLayouts = { m_BindingLayout };
		desc.renderState.rasterState.setCullNone().setScissorEnable(true).setDepthClipEnable(true);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		desc.renderState.blendState.targets[0].setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		m_Pipeline = Renderer::GetDevice()->createGraphicsPipeline(desc, framebufferInfo);
		m_PipelineFramebufferInfo = framebufferInfo;
	}

	void ImGuiRenderer::UpdateTexture(ImTextureData* texture, nvrhi::ICommandList* commandList)
	{
		nvrhi::IDevice* device = Renderer::GetDevice();

		if (texture->Status == ImTextureStatus_WantCreate)
		{
			BW_CORE_ASSERT(texture->Format == ImTextureFormat_RGBA32, "Only RGBA32 ImGui textures are supported");

			nvrhi::TextureDesc desc;
			desc.width = static_cast<uint32_t>(texture->Width);
			desc.height = static_cast<uint32_t>(texture->Height);
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.debugName = "ImGuiTexture";
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;

			auto* backendData = new ImGuiTextureBackendData();
			backendData->Texture = device->createTexture(desc);
			commandList->writeTexture(backendData->Texture, 0, 0, texture->GetPixels(), static_cast<size_t>(texture->GetPitch()));

			texture->SetTexID(reinterpret_cast<ImTextureID>(backendData->Texture.Get()));
			texture->BackendUserData = backendData;
			texture->SetStatus(ImTextureStatus_OK);
		}
		else if (texture->Status == ImTextureStatus_WantUpdates)
		{
			auto* backendData = static_cast<ImGuiTextureBackendData*>(texture->BackendUserData);
			// Re-uploading the whole atlas keeps the code simple; atlas updates are rare (new glyph sizes).
			commandList->writeTexture(backendData->Texture, 0, 0, texture->GetPixels(), static_cast<size_t>(texture->GetPitch()));
			texture->SetStatus(ImTextureStatus_OK);
		}
		else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 2)
		{
			auto* backendData = static_cast<ImGuiTextureBackendData*>(texture->BackendUserData);
			if (backendData)
			{
				m_BindingSets.erase(backendData->Texture.Get());
				delete backendData;
			}
			texture->BackendUserData = nullptr;
			texture->SetTexID(ImTextureID_Invalid);
			texture->SetStatus(ImTextureStatus_Destroyed);
		}
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		auto it = m_BindingSets.find(texture);
		if (it != m_BindingSets.end())
		{
			it->second.LastUsedFrame = m_FrameCounter;
			return it->second.BindingSet;
		}

		nvrhi::BindingSetDesc desc;
		desc.addItem(nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ImGuiPushConstants)));
		desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, texture));
		desc.addItem(nvrhi::BindingSetItem::Sampler(2, m_Sampler));

		CachedBindingSet entry;
		entry.Texture = texture;
		entry.BindingSet = Renderer::GetDevice()->createBindingSet(desc, m_BindingLayout);
		entry.LastUsedFrame = m_FrameCounter;
		nvrhi::IBindingSet* result = entry.BindingSet;
		m_BindingSets.emplace(texture, std::move(entry));
		return result;
	}

	void ImGuiRenderer::Render(ImDrawData* drawData, nvrhi::IFramebuffer* framebuffer, bool clear)
	{
		m_FrameCounter++;
		nvrhi::ICommandList* commandList = m_CommandList;
		commandList->open();

		if (drawData->Textures)
		{
			for (ImTextureData* texture : *drawData->Textures)
			{
				if (texture->Status != ImTextureStatus_OK)
					UpdateTexture(texture, commandList);
			}
		}

		const nvrhi::FramebufferInfoEx& framebufferInfo = framebuffer->getFramebufferInfo();
		if (clear)
			commandList->clearTextureFloat(framebuffer->getDesc().colorAttachments[0].texture, nvrhi::AllSubresources, nvrhi::Color(0.05f, 0.055f, 0.065f, 1.0f));

		if (!m_Pipeline || !(m_PipelineFramebufferInfo == static_cast<const nvrhi::FramebufferInfo&>(framebufferInfo)))
			CreatePipeline(framebufferInfo);

		const float framebufferWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
		const float framebufferHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;
		if (drawData->TotalVtxCount > 0 && framebufferWidth > 0.0f && framebufferHeight > 0.0f)
		{
			nvrhi::IDevice* device = Renderer::GetDevice();
			const size_t vertexCount = static_cast<size_t>(drawData->TotalVtxCount);
			const size_t indexCount = static_cast<size_t>(drawData->TotalIdxCount);
			if (vertexCount > m_VertexCapacity)
			{
				m_VertexCapacity = vertexCount + 5000;
				m_VertexBuffer = device->createBuffer(nvrhi::BufferDesc().setByteSize(m_VertexCapacity * sizeof(ImDrawVert))
					.setIsVertexBuffer(true).setInitialState(nvrhi::ResourceStates::VertexBuffer).setKeepInitialState(true).setDebugName("ImGuiVertices"));
			}
			if (indexCount > m_IndexCapacity)
			{
				m_IndexCapacity = indexCount + 10000;
				m_IndexBuffer = device->createBuffer(nvrhi::BufferDesc().setByteSize(m_IndexCapacity * sizeof(ImDrawIdx))
					.setIsIndexBuffer(true).setInitialState(nvrhi::ResourceStates::IndexBuffer).setKeepInitialState(true).setDebugName("ImGuiIndices"));
			}

			std::vector<ImDrawVert> vertices;
			std::vector<ImDrawIdx> indices;
			vertices.reserve(vertexCount);
			indices.reserve(indexCount);
			for (const ImDrawList* drawList : drawData->CmdLists)
			{
				vertices.insert(vertices.end(), drawList->VtxBuffer.Data, drawList->VtxBuffer.Data + drawList->VtxBuffer.Size);
				indices.insert(indices.end(), drawList->IdxBuffer.Data, drawList->IdxBuffer.Data + drawList->IdxBuffer.Size);
			}
			commandList->writeBuffer(m_VertexBuffer, vertices.data(), vertices.size() * sizeof(ImDrawVert));
			commandList->writeBuffer(m_IndexBuffer, indices.data(), indices.size() * sizeof(ImDrawIdx));

			ImGuiPushConstants push;
			// ImGui uses a top-left origin; clip space is Y-up (NVRHI flips the Vulkan viewport).
			push.Scale[0] = 2.0f / drawData->DisplaySize.x;
			push.Scale[1] = -2.0f / drawData->DisplaySize.y;
			push.Translate[0] = -1.0f - drawData->DisplayPos.x * push.Scale[0];
			push.Translate[1] = 1.0f - drawData->DisplayPos.y * push.Scale[1];

			const ImVec2 clipOffset = drawData->DisplayPos;
			const ImVec2 clipScale = drawData->FramebufferScale;
			const nvrhi::Viewport viewport(framebufferWidth, framebufferHeight);

			uint32_t globalVertexOffset = 0;
			uint32_t globalIndexOffset = 0;
			for (const ImDrawList* drawList : drawData->CmdLists)
			{
				for (const ImDrawCmd& command : drawList->CmdBuffer)
				{
					if (command.UserCallback)
					{
						if (command.UserCallback != ResetRenderStateCallback)
							command.UserCallback(drawList, &command);
						continue;
					}

					ImVec2 clipMin((command.ClipRect.x - clipOffset.x) * clipScale.x, (command.ClipRect.y - clipOffset.y) * clipScale.y);
					ImVec2 clipMax((command.ClipRect.z - clipOffset.x) * clipScale.x, (command.ClipRect.w - clipOffset.y) * clipScale.y);
					clipMin.x = std::max(clipMin.x, 0.0f);
					clipMin.y = std::max(clipMin.y, 0.0f);
					clipMax.x = std::min(clipMax.x, framebufferWidth);
					clipMax.y = std::min(clipMax.y, framebufferHeight);
					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
						continue;

					auto* texture = reinterpret_cast<nvrhi::ITexture*>(command.GetTexID());
					if (!texture)
						continue;

					nvrhi::GraphicsState state;
					state.pipeline = m_Pipeline;
					state.framebuffer = framebuffer;
					state.viewport.addViewport(viewport);
					state.viewport.addScissorRect(nvrhi::Rect(static_cast<int>(clipMin.x), static_cast<int>(clipMax.x),
						static_cast<int>(clipMin.y), static_cast<int>(clipMax.y)));
					state.bindings = { GetBindingSet(texture) };
					state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0).setOffset(0) };
					state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_IndexBuffer)
						.setFormat(sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT).setOffset(0);
					commandList->setGraphicsState(state);
					commandList->setPushConstants(&push, sizeof(push));

					nvrhi::DrawArguments args;
					args.vertexCount = command.ElemCount;
					args.startIndexLocation = command.IdxOffset + globalIndexOffset;
					args.startVertexLocation = command.VtxOffset + globalVertexOffset;
					commandList->drawIndexed(args);
				}
				globalVertexOffset += static_cast<uint32_t>(drawList->VtxBuffer.Size);
				globalIndexOffset += static_cast<uint32_t>(drawList->IdxBuffer.Size);
			}
		}

		commandList->close();
		Renderer::GetDevice()->executeCommandList(commandList);

		// Forget binding sets for user textures that have not been drawn for a while.
		for (auto it = m_BindingSets.begin(); it != m_BindingSets.end();)
		{
			if (m_FrameCounter - it->second.LastUsedFrame > 120)
				it = m_BindingSets.erase(it);
			else
				++it;
		}
	}

}
