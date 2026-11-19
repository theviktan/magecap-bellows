#include "bwpch.h"
#include "Bellows/Renderer/SceneRenderer.h"

#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/Renderer.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	namespace {

		constexpr nvrhi::Format c_DepthFormat = nvrhi::Format::D32;
		constexpr nvrhi::Format c_NormalFormat = nvrhi::Format::RGBA8_UNORM;
		constexpr nvrhi::Format c_EntityIDFormat = nvrhi::Format::R32_UINT;
		constexpr nvrhi::Format c_HDRFormat = nvrhi::Format::RGBA16_FLOAT;
		constexpr nvrhi::Format c_SSAOFormat = nvrhi::Format::R8_UNORM;
		constexpr nvrhi::Format c_LDRFormat = nvrhi::Format::RGBA8_UNORM;
		constexpr uint32_t c_MaxBloomMips = 6;
		constexpr uint32_t c_VolatileVersions = 64;
		constexpr uint32_t c_PushConstantSlot = 31;

		struct SSAOConstants
		{
			glm::mat4 Projection;
			glm::mat4 InverseProjection;
			glm::vec4 Params;
			glm::vec4 TargetSize;
		};

		struct BlurConstants
		{
			glm::mat4 InverseProjection;
			glm::vec4 Direction;
		};

		struct SkyConstants
		{
			glm::mat4 InverseViewProjection;
			glm::vec4 CameraPosition;
			glm::vec4 Params;
			glm::vec4 FallbackZenith;
			glm::vec4 FallbackHorizon;
		};

		struct BloomConstants
		{
			glm::vec4 SourceTexelSize;
			glm::vec4 Params;
		};

		struct TonemapConstants
		{
			glm::vec4 Params;
			glm::vec4 Params2;
		};

		struct FXAAConstants
		{
			glm::vec4 TexelSize;
			glm::vec4 Params;
		};

		struct GridConstants
		{
			glm::mat4 ViewProjection;
			glm::mat4 InverseViewProjection;
			glm::vec4 CameraPosition;
			glm::vec4 Params;
			glm::vec4 MinorColor;
			glm::vec4 MajorColor;
		};

		struct OutlineConstants
		{
			glm::vec4 Color;
			glm::vec4 Params;
			glm::uvec4 SelectedIDs[8];
		};

		struct FrustumPlanes
		{
			std::array<glm::vec4, 6> Planes;

			explicit FrustumPlanes(const glm::mat4& viewProjection)
			{
				const glm::mat4 m = glm::transpose(viewProjection);
				Planes[0] = m[3] + m[0];	// Left
				Planes[1] = m[3] - m[0];	// Right
				Planes[2] = m[3] + m[1];	// Bottom
				Planes[3] = m[3] - m[1];	// Top
				Planes[4] = m[2];			// Near (depth 0..1)
				Planes[5] = m[3] - m[2];	// Far
				for (glm::vec4& plane : Planes)
					plane /= glm::length(glm::vec3(plane));
			}

			bool Intersects(const AABB& box) const
			{
				for (const glm::vec4& plane : Planes)
				{
					const glm::vec3 positive(
						plane.x >= 0.0f ? box.Max.x : box.Min.x,
						plane.y >= 0.0f ? box.Max.y : box.Min.y,
						plane.z >= 0.0f ? box.Max.z : box.Min.z);
					if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f)
						return false;
				}
				return true;
			}
		};

		glm::mat3 NormalMatrix(const glm::mat4& model)
		{
			return glm::transpose(glm::inverse(glm::mat3(model)));
		}

	}

	const char* TonemapperToString(Tonemapper tonemapper)
	{
		switch (tonemapper)
		{
			case Tonemapper::ACES:		return "ACES";
			case Tonemapper::AgX:		return "AgX";
			case Tonemapper::Reinhard:	return "Reinhard";
			case Tonemapper::None:		return "None";
		}
		return "ACES";
	}

	Tonemapper TonemapperFromString(std::string_view text)
	{
		if (text == "AgX") return Tonemapper::AgX;
		if (text == "Reinhard") return Tonemapper::Reinhard;
		if (text == "None") return Tonemapper::None;
		return Tonemapper::ACES;
	}

	SceneRenderer::SceneRenderer()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();
		m_CommandList = device->createCommandList();

		m_FrameBuffer = CreateVolatileConstantBuffer(sizeof(ShaderInterop::FrameData), "FrameConstants");
		m_LightBuffer = CreateVolatileConstantBuffer(sizeof(ShaderInterop::LightData), "LightConstants");
		m_SSAOConstants = CreateVolatileConstantBuffer(sizeof(SSAOConstants), "SSAOConstants");
		m_SSAOBlurConstantsH = CreateVolatileConstantBuffer(sizeof(BlurConstants), "SSAOBlurH");
		m_SSAOBlurConstantsV = CreateVolatileConstantBuffer(sizeof(BlurConstants), "SSAOBlurV");
		m_SkyConstants = CreateVolatileConstantBuffer(sizeof(SkyConstants), "SkyConstants");
		m_TonemapConstants = CreateVolatileConstantBuffer(sizeof(TonemapConstants), "TonemapConstants");
		m_FXAAConstants = CreateVolatileConstantBuffer(sizeof(FXAAConstants), "FXAAConstants");
		m_GridConstants = CreateVolatileConstantBuffer(sizeof(GridConstants), "GridConstants");
		m_OutlineConstants = CreateVolatileConstantBuffer(sizeof(OutlineConstants), "OutlineConstants");
		for (uint32_t i = 0; i < c_MaxBloomMips * 2; i++)
			m_BloomConstants.push_back(CreateVolatileConstantBuffer(sizeof(BloomConstants), "BloomConstants"));

		CreatePipelines();
		m_AppliedSettings = m_Settings;
	}

	SceneRenderer::~SceneRenderer()
	{
		// GPU work referencing our resources may still be in flight.
		Renderer::GetDevice()->waitForIdle();
	}

	nvrhi::BufferHandle SceneRenderer::CreateVolatileConstantBuffer(size_t size, const char* name)
	{
		return Renderer::GetDevice()->createBuffer(nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsConstantBuffer(true)
			.setIsVolatile(true)
			.setMaxVersions(c_VolatileVersions)
			.setDebugName(name));
	}

	nvrhi::GraphicsPipelineHandle SceneRenderer::CreateFullscreenPipeline(const char* fragmentShader, const nvrhi::FramebufferInfo& framebufferInfo,
		const nvrhi::BlendState::RenderTarget& blend)
	{
		nvrhi::GraphicsPipelineDesc desc;
		desc.VS = Renderer::GetShader("Fullscreen.vert");
		desc.PS = Renderer::GetShader(fragmentShader);
		desc.primType = nvrhi::PrimitiveType::TriangleList;
		desc.bindingLayouts = { Renderer::GetPostProcessBindingLayout() };
		desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
		desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
		desc.renderState.blendState.targets[0] = blend;
		return Renderer::GetDevice()->createGraphicsPipeline(desc, framebufferInfo);
	}

	void SceneRenderer::CreatePipelines()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();

		nvrhi::ShaderHandle meshVS = Renderer::GetShader("Mesh.vert");
		nvrhi::ShaderHandle shadowVS = Renderer::GetShader("Shadow.vert");
		nvrhi::ShaderHandle lineVS = Renderer::GetShader("Line.vert");

		const nvrhi::VertexAttributeDesc meshAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Position)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(Vertex, Normal)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(Vertex, Tangent)).setElementStride(sizeof(Vertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(Vertex, TexCoord)).setElementStride(sizeof(Vertex)),
		};
		m_MeshInputLayout = device->createInputLayout(meshAttributes, 4, meshVS);

		const nvrhi::VertexAttributeDesc lineAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(LineVertex, Position)).setElementStride(sizeof(LineVertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(LineVertex, Color)).setElementStride(sizeof(LineVertex)),
		};
		m_LineInputLayout = device->createInputLayout(lineAttributes, 2, lineVS);

		nvrhi::ShaderHandle textVS = Renderer::GetShader("Text.vert");
		const nvrhi::VertexAttributeDesc textAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(offsetof(TextVertex, Position)).setElementStride(sizeof(TextVertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(TextVertex, TexCoord)).setElementStride(sizeof(TextVertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(TextVertex, Color)).setElementStride(sizeof(TextVertex)),
			nvrhi::VertexAttributeDesc().setName("OUTLINE_COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(TextVertex, OutlineColor)).setElementStride(sizeof(TextVertex)),
			nvrhi::VertexAttributeDesc().setName("OUTLINE_WIDTH").setFormat(nvrhi::Format::R32_FLOAT).setOffset(offsetof(TextVertex, OutlineWidth)).setElementStride(sizeof(TextVertex)),
			nvrhi::VertexAttributeDesc().setName("ENTITY_ID").setFormat(nvrhi::Format::R32_UINT).setOffset(offsetof(TextVertex, EntityID)).setElementStride(sizeof(TextVertex)),
		};
		m_TextInputLayout = device->createInputLayout(textAttributes, 6, textVS);

		nvrhi::ShaderHandle uiVS = Renderer::GetShader("UI.vert");
		const nvrhi::VertexAttributeDesc uiAttributes[] = {
			nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(UIVertex, Position)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(offsetof(UIVertex, TexCoord)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(UIVertex, Color)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("COLOR2").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(UIVertex, Color2)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("SHAPE").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(UIVertex, Shape)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("PARAMS").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(offsetof(UIVertex, Params)).setElementStride(sizeof(UIVertex)),
			nvrhi::VertexAttributeDesc().setName("ENTITY_ID").setFormat(nvrhi::Format::R32_UINT).setOffset(offsetof(UIVertex, EntityID)).setElementStride(sizeof(UIVertex)),
		};
		m_UIInputLayout = device->createInputLayout(uiAttributes, 7, uiVS);

		auto createMeshPipeline = [&](nvrhi::IShader* vs, nvrhi::IShader* ps, nvrhi::IBindingLayout* passLayout, const nvrhi::FramebufferInfo& info,
			const nvrhi::RenderState& state)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = vs;
			desc.PS = ps;
			desc.inputLayout = m_MeshInputLayout;
			desc.primType = nvrhi::PrimitiveType::TriangleList;
			desc.bindingLayouts = { passLayout, Renderer::GetMaterialBindingLayout() };
			desc.renderState = state;
			return device->createGraphicsPipeline(desc, info);
		};

		// Meshes use counter-clockwise front faces (glTF convention) in Y-up clip space.
		nvrhi::RasterState cullBack;
		cullBack.setCullBack().setFrontCounterClockwise(true).setDepthClipEnable(true);
		nvrhi::RasterState cullNone;
		cullNone.setCullNone().setFrontCounterClockwise(true).setDepthClipEnable(true);

		// Shadow pass
		{
			nvrhi::RenderState state;
			state.rasterState = cullNone;
			state.rasterState.setDepthBias(1).setSlopeScaleDepthBias(1.75f);
			state.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(nvrhi::ComparisonFunc::Less);
			m_ShadowPipeline = createMeshPipeline(shadowVS, Renderer::GetShader("Shadow.frag"), Renderer::GetShadowBindingLayout(),
				nvrhi::FramebufferInfo().setDepthFormat(c_DepthFormat), state);
		}

		// Depth/normal/ID prepass
		{
			nvrhi::FramebufferInfo info;
			info.addColorFormat(c_NormalFormat).addColorFormat(c_EntityIDFormat).setDepthFormat(c_DepthFormat);
			nvrhi::RenderState state;
			state.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(nvrhi::ComparisonFunc::Less);
			state.rasterState = cullBack;
			m_PrepassPipeline = createMeshPipeline(meshVS, Renderer::GetShader("DepthNormal.frag"), Renderer::GetSceneBindingLayout(), info, state);
			state.rasterState = cullNone;
			m_PrepassPipelineDoubleSided = createMeshPipeline(meshVS, Renderer::GetShader("DepthNormal.frag"), Renderer::GetSceneBindingLayout(), info, state);
		}

		nvrhi::FramebufferInfo hdrDepthInfo;
		hdrDepthInfo.addColorFormat(c_HDRFormat).setDepthFormat(c_DepthFormat);

		// Opaque lighting (depth EQUAL against the prepass)
		{
			nvrhi::RenderState state;
			state.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(nvrhi::ComparisonFunc::Equal);
			state.rasterState = cullBack;
			m_OpaquePipeline = createMeshPipeline(meshVS, Renderer::GetShader("PBR.frag"), Renderer::GetSceneBindingLayout(), hdrDepthInfo, state);
			state.rasterState = cullNone;
			m_OpaquePipelineDoubleSided = createMeshPipeline(meshVS, Renderer::GetShader("PBR.frag"), Renderer::GetSceneBindingLayout(), hdrDepthInfo, state);
		}

		// Transparent
		{
			nvrhi::RenderState state;
			state.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			state.blendState.targets[0].setBlendEnable(true)
				.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
				.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
			state.rasterState = cullBack;
			m_TransparentPipeline = createMeshPipeline(meshVS, Renderer::GetShader("PBR.frag"), Renderer::GetSceneBindingLayout(), hdrDepthInfo, state);
			state.rasterState = cullNone;
			m_TransparentPipelineDoubleSided = createMeshPipeline(meshVS, Renderer::GetShader("PBR.frag"), Renderer::GetSceneBindingLayout(), hdrDepthInfo, state);
		}

		nvrhi::BlendState::RenderTarget alphaBlend;
		alphaBlend.setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha).setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		nvrhi::BlendState::RenderTarget additive;
		additive.setBlendEnable(true)
			.setSrcBlend(nvrhi::BlendFactor::One).setDestBlend(nvrhi::BlendFactor::One)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One).setDestBlendAlpha(nvrhi::BlendFactor::One);

		m_SkyboxPipeline = CreateFullscreenPipeline("Skybox.frag", nvrhi::FramebufferInfo().addColorFormat(c_HDRFormat));
		m_SSAOPipeline = CreateFullscreenPipeline("SSAO.frag", nvrhi::FramebufferInfo().addColorFormat(c_SSAOFormat));
		m_SSAOBlurPipeline = CreateFullscreenPipeline("SSAOBlur.frag", nvrhi::FramebufferInfo().addColorFormat(c_SSAOFormat));
		m_BloomDownsamplePipeline = CreateFullscreenPipeline("BloomDownsample.frag", nvrhi::FramebufferInfo().addColorFormat(c_HDRFormat));
		m_BloomUpsamplePipeline = CreateFullscreenPipeline("BloomUpsample.frag", nvrhi::FramebufferInfo().addColorFormat(c_HDRFormat), additive);
		m_TonemapPipeline = CreateFullscreenPipeline("Tonemap.frag", nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat));
		m_FXAAPipeline = CreateFullscreenPipeline("FXAA.frag", nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat));
		m_OutlinePipeline = CreateFullscreenPipeline("Outline.frag", nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat), alphaBlend);

		// Grid: fullscreen, writes depth so scene geometry occludes it
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = Renderer::GetShader("Fullscreen.vert");
			desc.PS = Renderer::GetShader("Grid.frag");
			desc.bindingLayouts = { Renderer::GetPostProcessBindingLayout() };
			desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			desc.renderState.blendState.targets[0] = alphaBlend;
			m_GridPipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat).setDepthFormat(c_DepthFormat));
		}

		// Lines
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = lineVS;
			desc.PS = Renderer::GetShader("Line.frag");
			desc.inputLayout = m_LineInputLayout;
			desc.primType = nvrhi::PrimitiveType::LineList;
			desc.bindingLayouts = { Renderer::GetLineBindingLayout() };
			desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			desc.renderState.blendState.targets[0] = alphaBlend;
			m_LinePipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat).setDepthFormat(c_DepthFormat));
		}

		nvrhi::BindingSetDesc shadowSetDesc;
		shadowSetDesc.addItem(nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ShaderInterop::ShadowObjectData)));
		m_ShadowSet = device->createBindingSet(shadowSetDesc, Renderer::GetShadowBindingLayout());

		// Text: blended over the final image, and opaque glyph pixels into the entity ID image. Neither
		// writes depth, so blocks of text are drawn from far to near.
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = textVS;
			desc.PS = Renderer::GetShader("Text.frag");
			desc.inputLayout = m_TextInputLayout;
			desc.bindingLayouts = { Renderer::GetTextBindingLayout() };
			desc.renderState.rasterState.setCullNone().setDepthClipEnable(true);
			desc.renderState.depthStencilState.setDepthTestEnable(true).setDepthWriteEnable(false).setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
			desc.renderState.blendState.targets[0] = alphaBlend;
			m_TextPipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat).setDepthFormat(c_DepthFormat));

			desc.PS = Renderer::GetShader("TextID.frag");
			desc.renderState.blendState.targets[0] = nvrhi::BlendState::RenderTarget();
			m_TextIDPipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_EntityIDFormat).setDepthFormat(c_DepthFormat));
		}

		// Screen UI: on top of everything, so no depth at all.
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.VS = uiVS;
			desc.PS = Renderer::GetShader("UI.frag");
			desc.inputLayout = m_UIInputLayout;
			desc.bindingLayouts = { Renderer::GetTextBindingLayout() };
			desc.renderState.rasterState.setCullNone().setScissorEnable(true);
			desc.renderState.depthStencilState.setDepthTestEnable(false).setDepthWriteEnable(false);
			desc.renderState.blendState.targets[0] = alphaBlend;
			m_UIPipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_LDRFormat));

			desc.PS = Renderer::GetShader("UIID.frag");
			desc.renderState.blendState.targets[0] = nvrhi::BlendState::RenderTarget();
			m_UIIDPipeline = device->createGraphicsPipeline(desc, nvrhi::FramebufferInfo().addColorFormat(c_EntityIDFormat));
		}

		nvrhi::BindingSetDesc lineSetDesc;
		lineSetDesc.addItem(nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(glm::mat4)));
		m_LineSet = device->createBindingSet(lineSetDesc, Renderer::GetLineBindingLayout());

		BW_CORE_VERIFY(m_ShadowPipeline && m_PrepassPipeline && m_OpaquePipeline && m_TransparentPipeline && m_SkyboxPipeline
			&& m_SSAOPipeline && m_SSAOBlurPipeline && m_BloomDownsamplePipeline && m_BloomUpsamplePipeline
			&& m_TonemapPipeline && m_FXAAPipeline && m_GridPipeline && m_LinePipeline && m_OutlinePipeline
			&& m_TextPipeline && m_TextIDPipeline && m_UIPipeline && m_UIIDPipeline,
			"SceneRenderer: failed to create one or more pipelines");
	}

	void SceneRenderer::CreateRenderTargets()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();
		const uint32_t width = m_ViewportWidth;
		const uint32_t height = m_ViewportHeight;

		auto createTarget = [&](nvrhi::Format format, uint32_t w, uint32_t h, const char* name, nvrhi::Color clear)
		{
			nvrhi::TextureDesc desc;
			desc.width = std::max(w, 1u);
			desc.height = std::max(h, 1u);
			desc.format = format;
			desc.isRenderTarget = true;
			desc.isShaderResource = true;
			desc.debugName = name;
			desc.setClearValue(clear);
			desc.initialState = format == c_DepthFormat ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return device->createTexture(desc);
		};

		m_DepthTexture = createTarget(c_DepthFormat, width, height, "SceneDepth", nvrhi::Color(1.0f));
		m_NormalTexture = createTarget(c_NormalFormat, width, height, "SceneNormals", nvrhi::Color(0.0f));
		m_EntityIDTexture = createTarget(c_EntityIDFormat, width, height, "SceneEntityIDs", nvrhi::Color(0.0f));
		m_HDRTexture = createTarget(c_HDRFormat, width, height, "SceneHDR", nvrhi::Color(0.0f));
		m_LDRTexture = createTarget(c_LDRFormat, width, height, "SceneLDR", nvrhi::Color(0.0f));
		m_FinalTexture = createTarget(c_LDRFormat, width, height, "SceneFinal", nvrhi::Color(0.0f));

		const uint32_t ssaoDivisor = m_Settings.SSAOHalfResolution ? 2 : 1;
		m_SSAOWidth = std::max(width / ssaoDivisor, 1u);
		m_SSAOHeight = std::max(height / ssaoDivisor, 1u);
		m_SSAOTexture = createTarget(c_SSAOFormat, m_SSAOWidth, m_SSAOHeight, "SSAO", nvrhi::Color(1.0f));
		m_SSAOBlurTexture = createTarget(c_SSAOFormat, m_SSAOWidth, m_SSAOHeight, "SSAOBlur", nvrhi::Color(1.0f));

		m_BloomMips.clear();
		m_BloomFramebuffers.clear();
		uint32_t bloomWidth = std::max(width / 2, 1u);
		uint32_t bloomHeight = std::max(height / 2, 1u);
		for (uint32_t i = 0; i < c_MaxBloomMips && bloomWidth >= 4 && bloomHeight >= 4; i++)
		{
			nvrhi::TextureHandle mip = createTarget(c_HDRFormat, bloomWidth, bloomHeight, "BloomMip", nvrhi::Color(0.0f));
			m_BloomMips.push_back(mip);
			m_BloomFramebuffers.push_back(device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(mip)));
			bloomWidth /= 2;
			bloomHeight /= 2;
		}
		if (m_BloomMips.empty())
		{
			nvrhi::TextureHandle mip = createTarget(c_HDRFormat, 1, 1, "BloomMip", nvrhi::Color(0.0f));
			m_BloomMips.push_back(mip);
			m_BloomFramebuffers.push_back(device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(mip)));
		}

		m_PrepassFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc()
			.addColorAttachment(m_NormalTexture).addColorAttachment(m_EntityIDTexture).setDepthAttachment(m_DepthTexture));
		m_HDRDepthFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc()
			.addColorAttachment(m_HDRTexture)
			.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(m_DepthTexture).setReadOnly(true)));
		m_HDRFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_HDRTexture));
		m_SSAOFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_SSAOTexture));
		m_SSAOBlurFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_SSAOBlurTexture));
		m_LDRFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_LDRTexture));
		m_FinalFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_FinalTexture));
		m_FinalDepthFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_FinalTexture).setDepthAttachment(m_DepthTexture));
		m_EntityIDDepthFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_EntityIDTexture).setDepthAttachment(m_DepthTexture));
		m_EntityIDFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_EntityIDTexture));
	}

	void SceneRenderer::CreateShadowMap()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();
		const uint32_t size = std::clamp(m_Settings.ShadowMapSize, 256u, 8192u);
		const uint32_t cascades = std::clamp(m_Settings.CascadeCount, 1u, ShaderInterop::MaxCascades);

		nvrhi::TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.arraySize = ShaderInterop::MaxCascades;
		desc.dimension = nvrhi::TextureDimension::Texture2DArray;
		desc.format = c_DepthFormat;
		desc.isRenderTarget = true;
		desc.isShaderResource = true;
		desc.debugName = "ShadowCascades";
		desc.setClearValue(nvrhi::Color(1.0f));
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		m_ShadowMap = device->createTexture(desc);

		m_ShadowFramebuffers.clear();
		for (uint32_t i = 0; i < cascades; i++)
		{
			m_ShadowFramebuffers.push_back(device->createFramebuffer(nvrhi::FramebufferDesc()
				.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(m_ShadowMap).setArraySlice(i))));
		}
	}

	nvrhi::BindingSetHandle SceneRenderer::CreatePostProcessSet(nvrhi::IBuffer* constants, std::initializer_list<nvrhi::ITexture*> textures,
		nvrhi::TextureDimension firstDimension)
	{
		nvrhi::ITexture* white = Renderer::GetWhiteTexture()->GetHandle();

		nvrhi::BindingSetDesc desc;
		desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, constants));
		uint32_t slot = 1;
		for (nvrhi::ITexture* texture : textures)
		{
			const nvrhi::TextureDimension dimension = slot == 1 ? firstDimension : nvrhi::TextureDimension::Texture2D;
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, texture ? texture : white, nvrhi::Format::UNKNOWN,
				nvrhi::AllSubresources, texture ? dimension : nvrhi::TextureDimension::Texture2D));
			slot++;
		}
		for (; slot <= 4; slot++)
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, white));
		desc.addItem(nvrhi::BindingSetItem::Sampler(5, Renderer::GetLinearClampSampler()));
		desc.addItem(nvrhi::BindingSetItem::Sampler(6, Renderer::GetPointClampSampler()));
		return Renderer::GetDevice()->createBindingSet(desc, Renderer::GetPostProcessBindingLayout());
	}

	void SceneRenderer::CreateBindingSets()
	{
		nvrhi::IDevice* device = Renderer::GetDevice();

		nvrhi::ITexture* irradiance = Renderer::GetBlackCubeTexture();
		nvrhi::ITexture* prefiltered = Renderer::GetBlackCubeTexture();
		nvrhi::ITexture* radiance = Renderer::GetBlackCubeTexture();
		if (m_BoundEnvironment)
		{
			irradiance = m_BoundEnvironment->GetIrradianceMap()->GetHandle();
			prefiltered = m_BoundEnvironment->GetPrefilteredMap()->GetHandle();
			radiance = m_BoundEnvironment->GetRadianceMap()->GetHandle();
		}

		nvrhi::BindingSetDesc sceneDesc;
		sceneDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameBuffer));
		sceneDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_LightBuffer));
		sceneDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(2, m_ShadowMap, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray));
		sceneDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, m_SSAOTexture));
		sceneDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(4, irradiance, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube));
		sceneDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(5, prefiltered, nvrhi::Format::UNKNOWN, nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube));
		sceneDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(6, Renderer::GetBRDFLut()));
		sceneDesc.addItem(nvrhi::BindingSetItem::Sampler(7, Renderer::GetLinearClampSampler()));
		sceneDesc.addItem(nvrhi::BindingSetItem::Sampler(8, Renderer::GetShadowCompareSampler()));
		sceneDesc.addItem(nvrhi::BindingSetItem::Sampler(9, Renderer::GetPointClampSampler()));
		sceneDesc.addItem(nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ShaderInterop::ObjectData)));
		m_SceneSet = device->createBindingSet(sceneDesc, Renderer::GetSceneBindingLayout());

		m_SSAOSet = CreatePostProcessSet(m_SSAOConstants, { m_DepthTexture, m_NormalTexture });
		m_SSAOBlurSetH = CreatePostProcessSet(m_SSAOBlurConstantsH, { m_SSAOTexture, m_DepthTexture });
		m_SSAOBlurSetV = CreatePostProcessSet(m_SSAOBlurConstantsV, { m_SSAOBlurTexture, m_DepthTexture });
		m_SkySet = CreatePostProcessSet(m_SkyConstants, { radiance, m_DepthTexture }, nvrhi::TextureDimension::TextureCube);

		m_BloomDownsampleSets.clear();
		m_BloomUpsampleSets.clear();
		for (size_t i = 0; i < m_BloomMips.size(); i++)
		{
			nvrhi::ITexture* source = i == 0 ? m_HDRTexture.Get() : m_BloomMips[i - 1].Get();
			m_BloomDownsampleSets.push_back(CreatePostProcessSet(m_BloomConstants[i], { source }));
			m_BloomUpsampleSets.push_back(CreatePostProcessSet(m_BloomConstants[c_MaxBloomMips + i], { m_BloomMips[i] }));
		}

		m_TonemapSet = CreatePostProcessSet(m_TonemapConstants, { m_HDRTexture, m_BloomMips[0] });
		m_FXAASet = CreatePostProcessSet(m_FXAAConstants, { m_LDRTexture });
		m_GridSet = CreatePostProcessSet(m_GridConstants, {});

		// The outline pass reads the integer ID buffer.
		nvrhi::BindingSetDesc outlineDesc;
		outlineDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_OutlineConstants));
		outlineDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_EntityIDTexture));
		for (uint32_t slot = 2; slot <= 4; slot++)
			outlineDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(slot, Renderer::GetWhiteTexture()->GetHandle()));
		outlineDesc.addItem(nvrhi::BindingSetItem::Sampler(5, Renderer::GetLinearClampSampler()));
		outlineDesc.addItem(nvrhi::BindingSetItem::Sampler(6, Renderer::GetPointClampSampler()));
		m_OutlineSet = device->createBindingSet(outlineDesc, Renderer::GetPostProcessBindingLayout());
	}

	void SceneRenderer::SetViewportSize(uint32_t width, uint32_t height)
	{
		width = std::max(width, 1u);
		height = std::max(height, 1u);
		if (width == m_ViewportWidth && height == m_ViewportHeight)
			return;
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		m_TargetsDirty = true;
	}

	void SceneRenderer::BeginScene(const RenderCamera& camera)
	{
		BW_CORE_ASSERT(!m_InScene, "BeginScene called twice without EndScene");
		m_InScene = true;
		m_Camera = camera;
		m_HasDirectionalLight = false;
		m_PointLights.clear();
		m_SpotLights.clear();
		m_OpaqueDraws.clear();
		m_TransparentDraws.clear();
		m_LineVertices.clear();
		m_TextVertices.clear();
		m_TextDraws.clear();
		m_UIVertices.clear();
		m_UIDraws.clear();
		m_FrameMeshes.clear();
		m_FrameMaterials.clear();
		m_SceneBounds = AABB();
		m_Statistics = {};

		// Forget textures nothing but this renderer holds any more (a font or image no longer used).
		std::erase_if(m_TextureSets, [](const auto& entry) { return entry.second.Owner.use_count() <= 1; });
	}

	void SceneRenderer::SetEnvironment(const EnvironmentSettings& environment)
	{
		m_Environment = environment;
	}

	void SceneRenderer::SubmitDirectionalLight(const DirectionalLightData& light)
	{
		// The first submitted directional light is the one that is shaded (the sun).
		if (m_HasDirectionalLight)
			return;
		m_DirectionalLight = light;
		const float length = glm::length(m_DirectionalLight.Direction);
		m_DirectionalLight.Direction = length > 1e-6f ? m_DirectionalLight.Direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
		m_HasDirectionalLight = true;
	}

	void SceneRenderer::SubmitPointLight(const PointLightData& light)
	{
		if (m_PointLights.size() < ShaderInterop::MaxPointLights)
			m_PointLights.push_back(light);
	}

	void SceneRenderer::SubmitSpotLight(const SpotLightData& light)
	{
		if (m_SpotLights.size() < ShaderInterop::MaxSpotLights)
			m_SpotLights.push_back(light);
	}

	void SceneRenderer::SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, uint32_t entityID,
		const Ref<Material>& materialOverride, bool castShadows)
	{
		if (!mesh || !mesh->GetVertexBuffer())
			return;

		m_FrameMeshes.push_back(mesh);
		if (materialOverride)
			m_FrameMaterials.push_back(materialOverride);

		const auto& submeshes = mesh->GetSubmeshes();
		const auto& materials = mesh->GetMaterials();
		for (uint32_t i = 0; i < submeshes.size(); i++)
		{
			const Submesh& submesh = submeshes[i];
			DrawCommand draw;
			draw.MeshPtr = mesh.get();
			draw.SubmeshIndex = i;
			draw.MaterialPtr = materialOverride ? materialOverride.get() : materials[submesh.MaterialIndex].get();
			draw.Transform = transform * submesh.Transform;
			draw.EntityID = entityID;
			draw.CastShadows = castShadows;

			const AABB worldBounds = submesh.BoundingBox.Transformed(draw.Transform);
			m_SceneBounds.Expand(worldBounds);
			const glm::vec3 center = worldBounds.GetCenter();
			draw.SortKey = glm::dot(center - m_Camera.Position, center - m_Camera.Position);

			if (draw.MaterialPtr->GetProperties().Mode == AlphaMode::Blend)
				m_TransparentDraws.push_back(draw);
			else
				m_OpaqueDraws.push_back(draw);
		}
		m_Statistics.Meshes++;
	}

	void SceneRenderer::SubmitLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
	{
		m_LineVertices.push_back({ from, color });
		m_LineVertices.push_back({ to, color });
	}

	void SceneRenderer::SubmitWireBox(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color)
	{
		std::array<glm::vec3, 8> corners;
		for (int i = 0; i < 8; i++)
		{
			const glm::vec3 local((i & 1) ? halfExtents.x : -halfExtents.x, (i & 2) ? halfExtents.y : -halfExtents.y, (i & 4) ? halfExtents.z : -halfExtents.z);
			corners[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
		}
		constexpr int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
		for (const auto& edge : edges)
			SubmitLine(corners[edge[0]], corners[edge[1]], color);
	}

	void SceneRenderer::SubmitWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
	{
		constexpr int segments = 48;
		for (int axis = 0; axis < 3; axis++)
		{
			for (int i = 0; i < segments; i++)
			{
				const float a0 = static_cast<float>(i) / segments * glm::two_pi<float>();
				const float a1 = static_cast<float>(i + 1) / segments * glm::two_pi<float>();
				auto point = [&](float angle)
				{
					const float c = std::cos(angle) * radius;
					const float s = std::sin(angle) * radius;
					if (axis == 0) return center + glm::vec3(0.0f, c, s);
					if (axis == 1) return center + glm::vec3(c, 0.0f, s);
					return center + glm::vec3(c, s, 0.0f);
				};
				SubmitLine(point(a0), point(a1), color);
			}
		}
	}

	void SceneRenderer::SubmitWireCapsule(const glm::mat4& transform, float radius, float halfHeight, const glm::vec4& color)
	{
		constexpr int segments = 32;
		auto toWorld = [&](const glm::vec3& local) { return glm::vec3(transform * glm::vec4(local, 1.0f)); };

		for (int i = 0; i < segments; i++)
		{
			const float a0 = static_cast<float>(i) / segments * glm::two_pi<float>();
			const float a1 = static_cast<float>(i + 1) / segments * glm::two_pi<float>();
			for (float y : { -halfHeight, halfHeight })
			{
				SubmitLine(toWorld({ std::cos(a0) * radius, y, std::sin(a0) * radius }),
					toWorld({ std::cos(a1) * radius, y, std::sin(a1) * radius }), color);
			}
		}
		for (int i = 0; i < 4; i++)
		{
			const float angle = static_cast<float>(i) * glm::half_pi<float>();
			const glm::vec3 offset(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
			SubmitLine(toWorld(offset + glm::vec3(0.0f, -halfHeight, 0.0f)), toWorld(offset + glm::vec3(0.0f, halfHeight, 0.0f)), color);
		}
		for (int plane = 0; plane < 2; plane++)
		{
			for (int i = 0; i < segments / 2; i++)
			{
				const float a0 = static_cast<float>(i) / (segments / 2) * glm::pi<float>();
				const float a1 = static_cast<float>(i + 1) / (segments / 2) * glm::pi<float>();
				for (int cap = 0; cap < 2; cap++)
				{
					const float sign = cap == 0 ? 1.0f : -1.0f;
					auto point = [&](float angle)
					{
						const float horizontal = std::cos(angle) * radius;
						const float vertical = std::sin(angle) * radius * sign + halfHeight * sign;
						return plane == 0 ? glm::vec3(horizontal, vertical, 0.0f) : glm::vec3(0.0f, vertical, horizontal);
					};
					SubmitLine(toWorld(point(a0)), toWorld(point(a1)), color);
				}
			}
		}
	}

	void SceneRenderer::SetSelectedEntities(const std::vector<uint32_t>& entityIDs)
	{
		m_SelectedEntities = entityIDs;
	}

	void SceneRenderer::ComputeCascades(ShaderInterop::FrameData& frame)
	{
		const uint32_t cascadeCount = std::clamp(m_Settings.CascadeCount, 1u, ShaderInterop::MaxCascades);
		const float nearClip = m_Camera.NearClip;
		const float farClip = std::min(m_Camera.FarClip, std::max(m_Settings.ShadowDistance, nearClip + 1.0f));
		const float lambda = std::clamp(m_Settings.CascadeSplitLambda, 0.0f, 1.0f);

		// Frustum corners at the near and far plane in world space.
		const glm::mat4 inverseViewProjection = glm::inverse(m_Camera.Projection * m_Camera.View);
		std::array<glm::vec3, 4> nearCorners, farCorners;
		const glm::vec2 ndcCorners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
		for (int i = 0; i < 4; i++)
		{
			glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcCorners[i], 0.0f, 1.0f);
			glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcCorners[i], 1.0f, 1.0f);
			nearCorners[i] = glm::vec3(nearPoint) / nearPoint.w;
			farCorners[i] = glm::vec3(farPoint) / farPoint.w;
		}

		const float cameraRange = m_Camera.FarClip - m_Camera.NearClip;
		const glm::vec3 lightDirection = m_DirectionalLight.Direction;
		const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const float shadowMapSize = static_cast<float>(std::clamp(m_Settings.ShadowMapSize, 256u, 8192u));

		float previousSplit = nearClip;
		for (uint32_t cascade = 0; cascade < ShaderInterop::MaxCascades; cascade++)
		{
			if (cascade >= cascadeCount)
			{
				frame.CascadeViewProjection[cascade] = frame.CascadeViewProjection[cascadeCount - 1];
				frame.CascadeInfo[cascade] = frame.CascadeInfo[cascadeCount - 1];
				frame.CascadeSplits[static_cast<int>(cascade)] = farClip;
				continue;
			}

			// Practical split scheme: blend of logarithmic and uniform distributions.
			const float p = static_cast<float>(cascade + 1) / static_cast<float>(cascadeCount);
			const float logSplit = nearClip * std::pow(farClip / nearClip, p);
			const float uniformSplit = nearClip + (farClip - nearClip) * p;
			const float split = lambda * logSplit + (1.0f - lambda) * uniformSplit;

			const float t0 = (previousSplit - m_Camera.NearClip) / cameraRange;
			const float t1 = (split - m_Camera.NearClip) / cameraRange;

			std::array<glm::vec3, 8> corners;
			glm::vec3 center(0.0f);
			for (int i = 0; i < 4; i++)
			{
				corners[i] = glm::mix(nearCorners[i], farCorners[i], t0);
				corners[i + 4] = glm::mix(nearCorners[i], farCorners[i], t1);
			}
			for (const glm::vec3& corner : corners)
				center += corner;
			center /= 8.0f;

			// A bounding sphere keeps the projection size constant while the camera rotates (no shimmering).
			float radius = 0.0f;
			for (const glm::vec3& corner : corners)
				radius = std::max(radius, glm::length(corner - center));
			radius = std::ceil(radius * 16.0f) / 16.0f;

			// Pull the light's near plane back far enough to include every caster in the scene.
			float casterExtension = 0.0f;
			if (m_SceneBounds.IsValid())
			{
				float minimumProjection = std::numeric_limits<float>::max();
				for (int i = 0; i < 8; i++)
				{
					const glm::vec3 corner((i & 1) ? m_SceneBounds.Max.x : m_SceneBounds.Min.x,
						(i & 2) ? m_SceneBounds.Max.y : m_SceneBounds.Min.y, (i & 4) ? m_SceneBounds.Max.z : m_SceneBounds.Min.z);
					minimumProjection = std::min(minimumProjection, glm::dot(corner, lightDirection));
				}
				const float sphereNear = glm::dot(center, lightDirection) - radius;
				casterExtension = std::clamp(sphereNear - minimumProjection, 0.0f, 1000.0f);
			}

			glm::mat4 lightView = glm::lookAt(center - lightDirection * (radius + casterExtension), center, up);

			// Snap the cascade origin to whole shadow-map texels.
			const float texelsPerUnit = shadowMapSize / (radius * 2.0f);
			glm::vec4 origin = lightView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			origin *= texelsPerUnit;
			const glm::vec2 rounded = glm::round(glm::vec2(origin));
			const glm::vec2 offset = (rounded - glm::vec2(origin)) / texelsPerUnit;
			lightView[3][0] += offset.x;
			lightView[3][1] += offset.y;

			const float depthRange = radius * 2.0f + casterExtension;
			const glm::mat4 lightProjection = Math::OrthographicProjection(-radius, radius, -radius, radius, 0.0f, depthRange);

			frame.CascadeViewProjection[cascade] = lightProjection * lightView;
			frame.CascadeInfo[cascade] = glm::vec4(depthRange, radius * 2.0f, 0.0f, 0.0f);
			frame.CascadeSplits[static_cast<int>(cascade)] = split;
			previousSplit = split;
		}

		frame.ShadowParams = glm::vec4(m_Settings.ShadowBias, m_Settings.ShadowNormalBias, m_Settings.ShadowSoftness, static_cast<float>(cascadeCount));
		frame.ShadowParams2 = glm::vec4(1.0f / shadowMapSize, farClip, farClip * 0.1f, static_cast<float>(std::clamp(m_Settings.ShadowPCFSamples, 1u, 16u)));
	}

	void SceneRenderer::UpdateFrameConstants(nvrhi::ICommandList* commandList)
	{
		ShaderInterop::FrameData& frame = m_FrameData;
		frame = {};

		const glm::mat4 viewProjection = m_Camera.Projection * m_Camera.View;
		frame.View = m_Camera.View;
		frame.Projection = m_Camera.Projection;
		frame.ViewProjection = viewProjection;
		frame.InverseView = glm::inverse(m_Camera.View);
		frame.InverseProjection = glm::inverse(m_Camera.Projection);
		frame.InverseViewProjection = glm::inverse(viewProjection);
		frame.CameraPosition = glm::vec4(m_Camera.Position, 0.0f);
		frame.ViewportSize = glm::vec4(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight),
			1.0f / static_cast<float>(m_ViewportWidth), 1.0f / static_cast<float>(m_ViewportHeight));
		frame.CameraParams = glm::vec4(m_Camera.NearClip, m_Camera.FarClip, m_Settings.Exposure, 0.0f);

		const bool castShadows = m_HasDirectionalLight && m_DirectionalLight.CastShadows && m_Settings.ShadowsEnabled;
		if (m_HasDirectionalLight)
		{
			frame.DirectionalLightDirection = glm::vec4(m_DirectionalLight.Direction, m_DirectionalLight.Intensity);
			frame.DirectionalLightColor = glm::vec4(m_DirectionalLight.Color, castShadows ? 1.0f : 0.0f);
			ComputeCascades(frame);
		}

		const bool hasEnvironment = m_BoundEnvironment != nullptr;
		const float prefilteredMips = hasEnvironment ? static_cast<float>(m_BoundEnvironment->GetPrefilteredMap()->GetMipCount()) : 1.0f;
		frame.EnvironmentParams = glm::vec4(m_Environment.Intensity, m_Environment.Rotation, prefilteredMips, hasEnvironment ? 1.0f : 0.0f);
		frame.AmbientColor = glm::vec4(m_Environment.AmbientColor, 1.0f);
		frame.FeatureFlags = glm::vec4(m_Settings.SSAOEnabled ? 1.0f : 0.0f, m_Settings.SSAOStrength,
			static_cast<float>(m_PointLights.size()), static_cast<float>(m_SpotLights.size()));

		commandList->writeBuffer(m_FrameBuffer, &frame, sizeof(frame));

		ShaderInterop::LightData lights {};
		for (size_t i = 0; i < m_PointLights.size(); i++)
		{
			const PointLightData& light = m_PointLights[i];
			lights.PointLights[i].PositionRange = glm::vec4(light.Position, std::max(light.Range, 0.01f));
			lights.PointLights[i].ColorIntensity = glm::vec4(light.Color, light.Intensity);
		}
		for (size_t i = 0; i < m_SpotLights.size(); i++)
		{
			const SpotLightData& light = m_SpotLights[i];
			const float outer = std::clamp(light.OuterAngle, 0.001f, glm::half_pi<float>() - 0.001f);
			const float inner = std::clamp(light.InnerAngle, 0.0f, outer);
			lights.SpotLights[i].PositionRange = glm::vec4(light.Position, std::max(light.Range, 0.01f));
			lights.SpotLights[i].DirectionCosOuter = glm::vec4(glm::normalize(light.Direction), std::cos(outer));
			lights.SpotLights[i].ColorIntensity = glm::vec4(light.Color, light.Intensity);
			lights.SpotLights[i].Params = glm::vec4(std::cos(inner), 0.0f, 0.0f, 0.0f);
		}
		commandList->writeBuffer(m_LightBuffer, &lights, sizeof(lights));

		m_Statistics.PointLights = static_cast<uint32_t>(m_PointLights.size());
		m_Statistics.SpotLights = static_cast<uint32_t>(m_SpotLights.size());
	}

	void SceneRenderer::EndScene()
	{
		BW_CORE_ASSERT(m_InScene, "EndScene called without BeginScene");
		m_InScene = false;

		// React to settings that require new GPU resources.
		const bool shadowResize = !m_ShadowMap || m_Settings.ShadowMapSize != m_AppliedSettings.ShadowMapSize
			|| m_Settings.CascadeCount != m_AppliedSettings.CascadeCount;
		const bool ssaoResize = m_Settings.SSAOHalfResolution != m_AppliedSettings.SSAOHalfResolution;
		const bool environmentChanged = m_Environment.EnvironmentMap != m_BoundEnvironment;
		if (m_TargetsDirty || shadowResize || ssaoResize || environmentChanged || !m_SceneSet)
		{
			Renderer::GetDevice()->waitForIdle();
			if (m_TargetsDirty || ssaoResize || !m_DepthTexture)
				CreateRenderTargets();
			if (shadowResize)
				CreateShadowMap();
			m_BoundEnvironment = m_Environment.EnvironmentMap;
			CreateBindingSets();
			m_TargetsDirty = false;
			m_AppliedSettings = m_Settings;
		}

		// Frustum culling and sorting.
		const FrustumPlanes frustum(m_Camera.Projection * m_Camera.View);
		auto culled = [&frustum](const DrawCommand& draw)
		{
			const Submesh& submesh = draw.MeshPtr->GetSubmeshes()[draw.SubmeshIndex];
			return !frustum.Intersects(submesh.BoundingBox.Transformed(draw.Transform));
		};

		std::vector<DrawCommand> shadowCasters;
		shadowCasters.reserve(m_OpaqueDraws.size());
		for (const DrawCommand& draw : m_OpaqueDraws)
		{
			if (draw.CastShadows)
				shadowCasters.push_back(draw);
		}
		m_OpaqueDraws.erase(std::remove_if(m_OpaqueDraws.begin(), m_OpaqueDraws.end(), culled), m_OpaqueDraws.end());
		m_TransparentDraws.erase(std::remove_if(m_TransparentDraws.begin(), m_TransparentDraws.end(), culled), m_TransparentDraws.end());

		std::sort(m_OpaqueDraws.begin(), m_OpaqueDraws.end(), [](const DrawCommand& a, const DrawCommand& b)
		{
			if (a.MaterialPtr != b.MaterialPtr)
				return a.MaterialPtr < b.MaterialPtr;
			return a.SortKey < b.SortKey;
		});
		std::sort(m_TransparentDraws.begin(), m_TransparentDraws.end(), [](const DrawCommand& a, const DrawCommand& b)
		{
			return a.SortKey > b.SortKey;
		});

		nvrhi::ICommandList* commandList = m_CommandList;
		commandList->open();

		// Material constants and binding sets are refreshed up front.
		for (const auto* draws : { &m_OpaqueDraws, &m_TransparentDraws, &shadowCasters })
		{
			for (const DrawCommand& draw : *draws)
				draw.MaterialPtr->Prepare(commandList);
		}

		UpdateFrameConstants(commandList);

		RenderShadows(commandList, shadowCasters);
		RenderPrepass(commandList);
		RenderSSAO(commandList);
		RenderOpaque(commandList);
		RenderSkybox(commandList);
		RenderTransparent(commandList);
		RenderBloom(commandList);
		RenderTonemap(commandList);
		RenderText(commandList);
		RenderOverlays(commandList);
		RenderUI(commandList);
		RenderSelectionOutline(commandList);

		commandList->close();
		Renderer::GetDevice()->executeCommandList(commandList);
	}

	void SceneRenderer::RenderShadows(nvrhi::ICommandList* commandList, const std::vector<DrawCommand>& casters)
	{
		commandList->clearDepthStencilTexture(m_ShadowMap, nvrhi::AllSubresources, true, 1.0f, false, 0);
		if (m_HasDirectionalLight && m_DirectionalLight.CastShadows && m_Settings.ShadowsEnabled)
		{
			for (uint32_t cascade = 0; cascade < m_ShadowFramebuffers.size(); cascade++)
			{
				const glm::mat4& lightViewProjection = m_FrameData.CascadeViewProjection[cascade];
				const FrustumPlanes cascadeFrustum(lightViewProjection);
				nvrhi::IFramebuffer* framebuffer = m_ShadowFramebuffers[cascade];
				const uint32_t size = m_ShadowMap->getDesc().width;

				for (const DrawCommand& draw : casters)
				{
					const Submesh& submesh = draw.MeshPtr->GetSubmeshes()[draw.SubmeshIndex];
					const AABB bounds = submesh.BoundingBox.Transformed(draw.Transform);
					// Only cull sideways against the cascade: casters in front of the near plane still cast.
					bool outside = false;
					for (int plane = 0; plane < 4 && !outside; plane++)
					{
						const glm::vec4& p = cascadeFrustum.Planes[plane];
						const glm::vec3 positive(p.x >= 0 ? bounds.Max.x : bounds.Min.x, p.y >= 0 ? bounds.Max.y : bounds.Min.y, p.z >= 0 ? bounds.Max.z : bounds.Min.z);
						outside = glm::dot(glm::vec3(p), positive) + p.w < 0.0f;
					}
					if (outside)
						continue;

					nvrhi::GraphicsState state;
					state.pipeline = m_ShadowPipeline;
					state.framebuffer = framebuffer;
					state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(size), static_cast<float>(size)));
					state.bindings = { m_ShadowSet, draw.MaterialPtr->Prepare(commandList) };
					state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(draw.MeshPtr->GetVertexBuffer()).setSlot(0).setOffset(0) };
					state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(draw.MeshPtr->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
					commandList->setGraphicsState(state);

					ShaderInterop::ShadowObjectData push;
					push.Model = draw.Transform;
					push.LightViewProjection = lightViewProjection;
					commandList->setPushConstants(&push, sizeof(push));

					nvrhi::DrawArguments args;
					args.vertexCount = submesh.IndexCount;
					args.startIndexLocation = submesh.BaseIndex;
					args.startVertexLocation = submesh.BaseVertex;
					commandList->drawIndexed(args);
					m_Statistics.ShadowDrawCalls++;
				}
			}
		}
	}

	void SceneRenderer::DrawMeshes(nvrhi::ICommandList* commandList, const std::vector<DrawCommand>& draws, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IGraphicsPipeline* singleSided, nvrhi::IGraphicsPipeline* doubleSided)
	{
		const nvrhi::Viewport viewport(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight));
		for (const DrawCommand& draw : draws)
		{
			const Submesh& submesh = draw.MeshPtr->GetSubmeshes()[draw.SubmeshIndex];

			nvrhi::GraphicsState state;
			state.pipeline = draw.MaterialPtr->GetProperties().DoubleSided ? doubleSided : singleSided;
			state.framebuffer = framebuffer;
			state.viewport.addViewportAndScissorRect(viewport);
			state.bindings = { m_SceneSet, draw.MaterialPtr->Prepare(commandList) };
			state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(draw.MeshPtr->GetVertexBuffer()).setSlot(0).setOffset(0) };
			state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(draw.MeshPtr->GetIndexBuffer()).setFormat(nvrhi::Format::R32_UINT).setOffset(0);
			commandList->setGraphicsState(state);

			ShaderInterop::ObjectData push {};
			push.Model = draw.Transform;
			const glm::mat3 normalMatrix = NormalMatrix(draw.Transform);
			for (int i = 0; i < 3; i++)
				push.NormalMatrix[i] = glm::vec4(normalMatrix[i], 0.0f);
			push.EntityID = draw.EntityID;
			commandList->setPushConstants(&push, sizeof(push));

			nvrhi::DrawArguments args;
			args.vertexCount = submesh.IndexCount;
			args.startIndexLocation = submesh.BaseIndex;
			args.startVertexLocation = submesh.BaseVertex;
			commandList->drawIndexed(args);

			m_Statistics.DrawCalls++;
			m_Statistics.Triangles += submesh.IndexCount / 3;
		}
	}

	void SceneRenderer::DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IBindingSet* bindingSet, uint32_t width, uint32_t height)
	{
		nvrhi::GraphicsState state;
		state.pipeline = pipeline;
		state.framebuffer = framebuffer;
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height)));
		state.bindings = { bindingSet };
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

	void SceneRenderer::RenderPrepass(nvrhi::ICommandList* commandList)
	{
		commandList->clearDepthStencilTexture(m_DepthTexture, nvrhi::AllSubresources, true, 1.0f, false, 0);
		commandList->clearTextureFloat(m_NormalTexture, nvrhi::AllSubresources, nvrhi::Color(0.5f, 0.5f, 1.0f, 0.0f));
		commandList->clearTextureUInt(m_EntityIDTexture, nvrhi::AllSubresources, 0);
		DrawMeshes(commandList, m_OpaqueDraws, m_PrepassFramebuffer, m_PrepassPipeline, m_PrepassPipelineDoubleSided);
	}

	void SceneRenderer::RenderSSAO(nvrhi::ICommandList* commandList)
	{
		if (!m_Settings.SSAOEnabled)
		{
			commandList->clearTextureFloat(m_SSAOTexture, nvrhi::AllSubresources, nvrhi::Color(1.0f));
			return;
		}

		SSAOConstants constants;
		constants.Projection = m_Camera.Projection;
		constants.InverseProjection = glm::inverse(m_Camera.Projection);
		constants.Params = glm::vec4(m_Settings.SSAORadius, m_Settings.SSAOBias, m_Settings.SSAOPower,
			static_cast<float>(std::clamp(m_Settings.SSAOSamples, 4u, 64u)));
		constants.TargetSize = glm::vec4(static_cast<float>(m_SSAOWidth), static_cast<float>(m_SSAOHeight),
			1.0f / static_cast<float>(m_SSAOWidth), 1.0f / static_cast<float>(m_SSAOHeight));
		commandList->writeBuffer(m_SSAOConstants, &constants, sizeof(constants));
		DrawFullscreen(commandList, m_SSAOPipeline, m_SSAOFramebuffer, m_SSAOSet, m_SSAOWidth, m_SSAOHeight);

		BlurConstants blur;
		blur.InverseProjection = constants.InverseProjection;
		blur.Direction = glm::vec4(1.0f / static_cast<float>(m_SSAOWidth), 0.0f, 8.0f, 0.0f);
		commandList->writeBuffer(m_SSAOBlurConstantsH, &blur, sizeof(blur));
		DrawFullscreen(commandList, m_SSAOBlurPipeline, m_SSAOBlurFramebuffer, m_SSAOBlurSetH, m_SSAOWidth, m_SSAOHeight);

		blur.Direction = glm::vec4(0.0f, 1.0f / static_cast<float>(m_SSAOHeight), 8.0f, 0.0f);
		commandList->writeBuffer(m_SSAOBlurConstantsV, &blur, sizeof(blur));
		DrawFullscreen(commandList, m_SSAOBlurPipeline, m_SSAOFramebuffer, m_SSAOBlurSetV, m_SSAOWidth, m_SSAOHeight);
	}

	void SceneRenderer::RenderOpaque(nvrhi::ICommandList* commandList)
	{
		commandList->clearTextureFloat(m_HDRTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		DrawMeshes(commandList, m_OpaqueDraws, m_HDRDepthFramebuffer, m_OpaquePipeline, m_OpaquePipelineDoubleSided);
	}

	void SceneRenderer::RenderSkybox(nvrhi::ICommandList* commandList)
	{
		SkyConstants constants;
		constants.InverseViewProjection = glm::inverse(m_Camera.Projection * m_Camera.View);
		constants.CameraPosition = glm::vec4(m_Camera.Position, 1.0f);
		constants.Params = glm::vec4(m_Environment.Intensity, m_Environment.Rotation, m_Environment.SkyboxBlur, m_BoundEnvironment ? 1.0f : 0.0f);
		constants.FallbackZenith = glm::vec4(0.09f, 0.11f, 0.15f, 1.0f);
		constants.FallbackHorizon = glm::vec4(0.20f, 0.22f, 0.25f, 1.0f);
		commandList->writeBuffer(m_SkyConstants, &constants, sizeof(constants));
		DrawFullscreen(commandList, m_SkyboxPipeline, m_HDRFramebuffer, m_SkySet, m_ViewportWidth, m_ViewportHeight);
	}

	void SceneRenderer::RenderTransparent(nvrhi::ICommandList* commandList)
	{
		DrawMeshes(commandList, m_TransparentDraws, m_HDRDepthFramebuffer, m_TransparentPipeline, m_TransparentPipelineDoubleSided);
	}

	void SceneRenderer::RenderBloom(nvrhi::ICommandList* commandList)
	{
		if (!m_Settings.BloomEnabled)
			return;

		for (size_t i = 0; i < m_BloomMips.size(); i++)
		{
			const nvrhi::TextureDesc& source = i == 0 ? m_HDRTexture->getDesc() : m_BloomMips[i - 1]->getDesc();
			const nvrhi::TextureDesc& target = m_BloomMips[i]->getDesc();
			BloomConstants constants;
			constants.SourceTexelSize = glm::vec4(1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height), 0.0f, 0.0f);
			constants.Params = glm::vec4(i == 0 ? 1.0f : 0.0f, m_Settings.BloomThreshold, m_Settings.BloomKnee, 0.0f);
			commandList->writeBuffer(m_BloomConstants[i], &constants, sizeof(constants));
			DrawFullscreen(commandList, m_BloomDownsamplePipeline, m_BloomFramebuffers[i], m_BloomDownsampleSets[i], target.width, target.height);
		}

		for (size_t i = m_BloomMips.size() - 1; i > 0; i--)
		{
			const nvrhi::TextureDesc& source = m_BloomMips[i]->getDesc();
			const nvrhi::TextureDesc& target = m_BloomMips[i - 1]->getDesc();
			BloomConstants constants;
			constants.SourceTexelSize = glm::vec4(1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height), 0.0f, 0.0f);
			constants.Params = glm::vec4(m_Settings.BloomRadius, 0.0f, 0.0f, 0.0f);
			commandList->writeBuffer(m_BloomConstants[c_MaxBloomMips + i], &constants, sizeof(constants));
			DrawFullscreen(commandList, m_BloomUpsamplePipeline, m_BloomFramebuffers[i - 1], m_BloomUpsampleSets[i], target.width, target.height);
		}
	}

	void SceneRenderer::RenderTonemap(nvrhi::ICommandList* commandList)
	{
		TonemapConstants tonemap;
		tonemap.Params = glm::vec4(m_Settings.Exposure, m_Settings.BloomIntensity, static_cast<float>(m_Settings.ToneMapper), m_Settings.Vignette);
		tonemap.Params2 = glm::vec4(m_Settings.BloomEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		commandList->writeBuffer(m_TonemapConstants, &tonemap, sizeof(tonemap));
		DrawFullscreen(commandList, m_TonemapPipeline, m_LDRFramebuffer, m_TonemapSet, m_ViewportWidth, m_ViewportHeight);

		FXAAConstants fxaa;
		fxaa.TexelSize = glm::vec4(1.0f / static_cast<float>(m_ViewportWidth), 1.0f / static_cast<float>(m_ViewportHeight), 0.0f, 0.0f);
		fxaa.Params = glm::vec4(m_Settings.FXAAEnabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		commandList->writeBuffer(m_FXAAConstants, &fxaa, sizeof(fxaa));
		DrawFullscreen(commandList, m_FXAAPipeline, m_FinalFramebuffer, m_FXAASet, m_ViewportWidth, m_ViewportHeight);
	}

	void SceneRenderer::SubmitText(const Ref<Font>& font, const TextLayout& layout, const glm::mat4& layoutToWorld, const TextStyle& style,
		uint32_t entityID, const glm::vec4* clipRect)
	{
		if (!font || layout.Quads.empty())
			return;
		// The atlas texture is uploaded here, before EndScene opens the frame's command list.
		GetTextBindingSet(font);

		const float outlineWidth = std::clamp(style.OutlineWidth * font->GetGlyphSize() / font->GetDistanceRange(), 0.0f, 0.49f);
		TextDraw draw;
		draw.FontRef = font;
		draw.FirstVertex = static_cast<uint32_t>(m_TextVertices.size());
		draw.Distance = glm::length(glm::vec3(layoutToWorld[3]) - m_Camera.Position);
		for (const TextGlyphQuad& glyph : layout.Quads)
		{
			glm::vec2 min = glyph.Min, max = glyph.Max;
			glm::vec2 uvMin = glyph.UVMin, uvMax = glyph.UVMax;
			if (clipRect)
			{
				// Cut the quad at the clip edges, moving its UVs with it.
				const glm::vec2 clippedMin = glm::max(min, glm::vec2(clipRect->x, clipRect->y));
				const glm::vec2 clippedMax = glm::min(max, glm::vec2(clipRect->z, clipRect->w));
				if (clippedMin.x >= clippedMax.x || clippedMin.y >= clippedMax.y)
					continue;
				const glm::vec2 size = max - min;
				const glm::vec2 uvSize = uvMax - uvMin;
				const glm::vec2 newUVMin = uvMin + (clippedMin - min) / size * uvSize;
				const glm::vec2 newUVMax = uvMin + (clippedMax - min) / size * uvSize;
				min = clippedMin;
				max = clippedMax;
				uvMin = newUVMin;
				uvMax = newUVMax;
			}

			const auto vertex = [&](float x, float y, float u, float v)
			{
				TextVertex result;
				result.Position = glm::vec3(layoutToWorld * glm::vec4(x, y, 0.0f, 1.0f));
				result.TexCoord = glm::vec2(u, v);
				result.Color = style.Color;
				result.OutlineColor = style.OutlineColor;
				result.OutlineWidth = outlineWidth;
				result.EntityID = entityID;
				m_TextVertices.push_back(result);
			};
			vertex(min.x, min.y, uvMin.x, uvMin.y);
			vertex(max.x, min.y, uvMax.x, uvMin.y);
			vertex(max.x, max.y, uvMax.x, uvMax.y);
			vertex(min.x, min.y, uvMin.x, uvMin.y);
			vertex(max.x, max.y, uvMax.x, uvMax.y);
			vertex(min.x, max.y, uvMin.x, uvMax.y);
		}
		draw.VertexCount = static_cast<uint32_t>(m_TextVertices.size()) - draw.FirstVertex;
		if (draw.VertexCount > 0)
			m_TextDraws.push_back(std::move(draw));
	}

	nvrhi::IBindingSet* SceneRenderer::GetTextureBindingSet(nvrhi::ITexture* texture, std::shared_ptr<const void> owner)
	{
		auto it = m_TextureSets.find(texture);
		if (it != m_TextureSets.end())
			return it->second.Set;

		nvrhi::BindingSetDesc desc;
		desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture));
		desc.addItem(nvrhi::BindingSetItem::Sampler(1, Renderer::GetLinearClampSampler()));
		desc.addItem(nvrhi::BindingSetItem::PushConstants(c_PushConstantSlot, sizeof(ShaderInterop::TextData)));
		TextureBinding binding { std::move(owner), Renderer::GetDevice()->createBindingSet(desc, Renderer::GetTextBindingLayout()) };
		return m_TextureSets.emplace(texture, std::move(binding)).first->second.Set;
	}

	nvrhi::IBindingSet* SceneRenderer::GetTextBindingSet(const Ref<Font>& font)
	{
		return GetTextureBindingSet(font->GetAtlasTexture()->GetHandle(), font);
	}

	void SceneRenderer::RenderText(nvrhi::ICommandList* commandList)
	{
		if (m_TextDraws.empty())
			return;

		if (m_TextVertices.size() > m_TextVertexCapacity)
		{
			m_TextVertexCapacity = std::max<size_t>(m_TextVertices.size() * 2, 6 * 1024);
			m_TextVertexBuffer = Renderer::GetDevice()->createBuffer(nvrhi::BufferDesc()
				.setByteSize(m_TextVertexCapacity * sizeof(TextVertex))
				.setIsVertexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::VertexBuffer)
				.setKeepInitialState(true)
				.setDebugName("TextVertices"));
		}
		commandList->writeBuffer(m_TextVertexBuffer, m_TextVertices.data(), m_TextVertices.size() * sizeof(TextVertex));

		// Without depth writes, nearer text must be drawn last to land on top.
		std::stable_sort(m_TextDraws.begin(), m_TextDraws.end(), [](const TextDraw& a, const TextDraw& b) { return a.Distance > b.Distance; });

		ShaderInterop::TextData constants;
		constants.ViewProjection = m_Camera.Projection * m_Camera.View;
		for (const auto& [pipeline, framebuffer] : { std::pair(m_TextPipeline.Get(), m_FinalDepthFramebuffer.Get()),
			std::pair(m_TextIDPipeline.Get(), m_EntityIDDepthFramebuffer.Get()) })
		{
			for (const TextDraw& draw : m_TextDraws)
			{
				nvrhi::GraphicsState state;
				state.pipeline = pipeline;
				state.framebuffer = framebuffer;
				state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight)));
				state.bindings = { GetTextBindingSet(draw.FontRef) };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_TextVertexBuffer).setSlot(0).setOffset(0) };
				commandList->setGraphicsState(state);
				constants.Params = glm::vec4(draw.FontRef->GetDistanceRange(), 0.0f, 0.0f, 0.0f);
				commandList->setPushConstants(&constants, sizeof(constants));
				commandList->draw(nvrhi::DrawArguments().setVertexCount(draw.VertexCount).setStartVertexLocation(draw.FirstVertex));
				m_Statistics.DrawCalls++;
			}
		}
	}

	void SceneRenderer::RenderOverlays(nvrhi::ICommandList* commandList)
	{
		const glm::mat4 viewProjection = m_Camera.Projection * m_Camera.View;

		if (m_Settings.ShowGrid)
		{
			GridConstants grid;
			grid.ViewProjection = viewProjection;
			grid.InverseViewProjection = glm::inverse(viewProjection);
			grid.CameraPosition = glm::vec4(m_Camera.Position, 1.0f);
			grid.Params = glm::vec4(std::max(m_Settings.GridCellSize, 0.01f), std::max(150.0f, std::abs(m_Camera.Position.y) * 30.0f), 1.0f, 0.0f);
			grid.MinorColor = glm::vec4(0.55f, 0.57f, 0.62f, 0.22f);
			grid.MajorColor = glm::vec4(0.62f, 0.64f, 0.70f, 0.42f);
			commandList->writeBuffer(m_GridConstants, &grid, sizeof(grid));
			DrawFullscreen(commandList, m_GridPipeline, m_FinalDepthFramebuffer, m_GridSet, m_ViewportWidth, m_ViewportHeight);
		}

		if (!m_LineVertices.empty())
		{
			if (m_LineVertices.size() > m_LineVertexCapacity)
			{
				m_LineVertexCapacity = std::max<size_t>(m_LineVertices.size() * 2, 4096);
				m_LineVertexBuffer = Renderer::GetDevice()->createBuffer(nvrhi::BufferDesc()
					.setByteSize(m_LineVertexCapacity * sizeof(LineVertex))
					.setIsVertexBuffer(true)
					.setInitialState(nvrhi::ResourceStates::VertexBuffer)
					.setKeepInitialState(true)
					.setDebugName("DebugLines"));
			}
			commandList->writeBuffer(m_LineVertexBuffer, m_LineVertices.data(), m_LineVertices.size() * sizeof(LineVertex));

			nvrhi::GraphicsState state;
			state.pipeline = m_LinePipeline;
			state.framebuffer = m_FinalDepthFramebuffer;
			state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(m_ViewportWidth), static_cast<float>(m_ViewportHeight)));
			state.bindings = { m_LineSet };
			state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_LineVertexBuffer).setSlot(0).setOffset(0) };
			commandList->setGraphicsState(state);
			commandList->setPushConstants(&viewProjection, sizeof(viewProjection));
			commandList->draw(nvrhi::DrawArguments().setVertexCount(static_cast<uint32_t>(m_LineVertices.size())));
		}
	}

	void SceneRenderer::RenderSelectionOutline(nvrhi::ICommandList* commandList)
	{
		// Last, so selected UI (drawn into the entity ID image too) gets its outline on top.
		if (m_Settings.ShowSelectionOutline && !m_SelectedEntities.empty())
		{
			OutlineConstants outline {};
			outline.Color = m_Settings.OutlineColor;
			const uint32_t count = static_cast<uint32_t>(std::min<size_t>(m_SelectedEntities.size(), 32));
			outline.Params = glm::vec4(2.0f, static_cast<float>(count), 0.0f, 0.0f);
			for (uint32_t i = 0; i < count; i++)
				outline.SelectedIDs[i / 4][static_cast<int>(i % 4)] = m_SelectedEntities[i];
			commandList->writeBuffer(m_OutlineConstants, &outline, sizeof(outline));
			DrawFullscreen(commandList, m_OutlinePipeline, m_FinalFramebuffer, m_OutlineSet, m_ViewportWidth, m_ViewportHeight);
		}
	}

	void SceneRenderer::AddUIQuad(nvrhi::IBindingSet* set, float distanceRange, const glm::vec4* clipRect, const UIVertex (&corners)[4])
	{
		glm::ivec4 scissor(0, 0, static_cast<int>(m_ViewportWidth), static_cast<int>(m_ViewportHeight));
		if (clipRect)
		{
			// Whole pixels the clip rectangle touches, inside the viewport.
			scissor = glm::ivec4(
				std::max(scissor.x, static_cast<int>(std::floor(clipRect->x))), std::max(scissor.y, static_cast<int>(std::floor(clipRect->y))),
				std::min(scissor.z, static_cast<int>(std::ceil(clipRect->z))), std::min(scissor.w, static_cast<int>(std::ceil(clipRect->w))));
			if (scissor.x >= scissor.z || scissor.y >= scissor.w)
				return;
		}

		// Consecutive quads with the same texture, range and clip share one draw.
		if (m_UIDraws.empty() || m_UIDraws.back().Set != set || m_UIDraws.back().DistanceRange != distanceRange
			|| m_UIDraws.back().Scissor != scissor)
		{
			UIDraw draw;
			draw.Set = set;
			draw.DistanceRange = distanceRange;
			draw.Scissor = scissor;
			draw.FirstVertex = static_cast<uint32_t>(m_UIVertices.size());
			m_UIDraws.push_back(draw);
		}
		for (int index : { 0, 1, 2, 0, 2, 3 })
			m_UIVertices.push_back(corners[index]);
		m_UIDraws.back().VertexCount += 6;
	}

	void SceneRenderer::SubmitUIRect(const glm::vec2& min, const glm::vec2& max, const UIRectStyle& style, const Ref<Texture2D>& texture,
		uint32_t entityID, const glm::vec4* clipRect, const glm::vec4& uvRect)
	{
		if (!m_Settings.ShowUI || max.x <= min.x || max.y <= min.y)
			return;
		const bool hasBorder = style.BorderWidth > 0.0f && style.BorderColor.a > 0.0f;
		if (style.Color.a <= 0.0f && !hasBorder)
			return;

		// Created here, before EndScene opens the frame's command list.
		const Ref<Texture2D>& image = texture ? texture : Renderer::GetWhiteTexture();
		nvrhi::IBindingSet* set = GetTextureBindingSet(image->GetHandle(), image);

		const glm::vec2 center = (min + max) * 0.5f;
		const glm::vec2 halfSize = (max - min) * 0.5f;
		const glm::vec4 params(std::max(style.CornerRadius, 0.0f), hasBorder ? style.BorderWidth : 0.0f, 0.0f, 0.0f);
		UIVertex corners[4];
		const glm::vec2 positions[4] = { min, { max.x, min.y }, max, { min.x, max.y } };
		const glm::vec2 texCoords[4] = { { uvRect.x, uvRect.y }, { uvRect.z, uvRect.y }, { uvRect.z, uvRect.w }, { uvRect.x, uvRect.w } };
		for (int i = 0; i < 4; i++)
		{
			corners[i].Position = positions[i];
			corners[i].TexCoord = texCoords[i];
			corners[i].Color = style.Color;
			corners[i].Color2 = style.BorderColor;
			corners[i].Shape = glm::vec4(positions[i] - center, halfSize);
			corners[i].Params = params;
			corners[i].EntityID = entityID;
		}
		AddUIQuad(set, 0.0f, clipRect, corners);
	}

	void SceneRenderer::SubmitUIText(const Ref<Font>& font, const TextLayout& layout, const glm::vec2& origin, const TextStyle& style,
		uint32_t entityID, const glm::vec4* clipRect)
	{
		if (!m_Settings.ShowUI || !font || layout.Quads.empty())
			return;
		nvrhi::IBindingSet* set = GetTextBindingSet(font);
		const float outlineWidth = std::clamp(style.OutlineWidth * font->GetGlyphSize() / font->GetDistanceRange(), 0.0f, 0.49f);
		const glm::vec4 params(0.0f, 0.0f, outlineWidth, 1.0f);
		for (const TextGlyphQuad& glyph : layout.Quads)
		{
			const glm::vec2 min = origin + glyph.Min, max = origin + glyph.Max;
			const glm::vec2 positions[4] = { min, { max.x, min.y }, max, { min.x, max.y } };
			const glm::vec2 texCoords[4] = { glyph.UVMin, { glyph.UVMax.x, glyph.UVMin.y }, glyph.UVMax, { glyph.UVMin.x, glyph.UVMax.y } };
			UIVertex corners[4];
			for (int i = 0; i < 4; i++)
			{
				corners[i].Position = positions[i];
				corners[i].TexCoord = texCoords[i];
				corners[i].Color = style.Color;
				corners[i].Color2 = style.OutlineColor;
				corners[i].Shape = glm::vec4(0.0f);
				corners[i].Params = params;
				corners[i].EntityID = entityID;
			}
			AddUIQuad(set, font->GetDistanceRange(), clipRect, corners);
		}
	}

	void SceneRenderer::RenderUI(nvrhi::ICommandList* commandList)
	{
		if (m_UIDraws.empty())
			return;

		if (m_UIVertices.size() > m_UIVertexCapacity)
		{
			m_UIVertexCapacity = std::max<size_t>(m_UIVertices.size() * 2, 6 * 1024);
			m_UIVertexBuffer = Renderer::GetDevice()->createBuffer(nvrhi::BufferDesc()
				.setByteSize(m_UIVertexCapacity * sizeof(UIVertex))
				.setIsVertexBuffer(true)
				.setInitialState(nvrhi::ResourceStates::VertexBuffer)
				.setKeepInitialState(true)
				.setDebugName("UIVertices"));
		}
		commandList->writeBuffer(m_UIVertexBuffer, m_UIVertices.data(), m_UIVertices.size() * sizeof(UIVertex));

		// Pixels to clip space: x right, y down from the top-left, Y-up clip space.
		const float width = static_cast<float>(m_ViewportWidth), height = static_cast<float>(m_ViewportHeight);
		ShaderInterop::TextData constants;
		constants.ViewProjection = glm::mat4(1.0f);
		constants.ViewProjection[0][0] = 2.0f / width;
		constants.ViewProjection[1][1] = -2.0f / height;
		constants.ViewProjection[3] = glm::vec4(-1.0f, 1.0f, 0.0f, 1.0f);
		for (const auto& [pipeline, framebuffer] : { std::pair(m_UIPipeline.Get(), m_FinalFramebuffer.Get()),
			std::pair(m_UIIDPipeline.Get(), m_EntityIDFramebuffer.Get()) })
		{
			for (const UIDraw& draw : m_UIDraws)
			{
				nvrhi::GraphicsState state;
				state.pipeline = pipeline;
				state.framebuffer = framebuffer;
				state.viewport.addViewport(nvrhi::Viewport(width, height));
				state.viewport.addScissorRect(nvrhi::Rect(draw.Scissor.x, draw.Scissor.z, draw.Scissor.y, draw.Scissor.w));
				state.bindings = { draw.Set };
				state.vertexBuffers = { nvrhi::VertexBufferBinding().setBuffer(m_UIVertexBuffer).setSlot(0).setOffset(0) };
				commandList->setGraphicsState(state);
				constants.Params = glm::vec4(draw.DistanceRange, 0.0f, 0.0f, 0.0f);
				commandList->setPushConstants(&constants, sizeof(constants));
				commandList->draw(nvrhi::DrawArguments().setVertexCount(draw.VertexCount).setStartVertexLocation(draw.FirstVertex));
				m_Statistics.DrawCalls++;
			}
		}
	}

	nvrhi::ITexture* SceneRenderer::GetFinalImage() const
	{
		return m_FinalTexture;
	}

	nvrhi::ITexture* SceneRenderer::GetEntityIDImage() const
	{
		return m_EntityIDTexture;
	}

	nvrhi::ITexture* SceneRenderer::GetHDRImage() const
	{
		return m_HDRTexture;
	}

	uint32_t SceneRenderer::ReadEntityID(uint32_t x, uint32_t y) const
	{
		return Renderer::ReadbackPixelUInt(m_EntityIDTexture, x, y);
	}

}
