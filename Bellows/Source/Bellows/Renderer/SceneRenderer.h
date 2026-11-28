#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Math/AABB.h"
#include "Bellows/Renderer/Environment.h"
#include "Bellows/Renderer/Material.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Renderer/ShaderInterop.h"
#include "Bellows/Renderer/Texture.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"

#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace Bellows {

	class GpuTimer;

	enum class Tonemapper : uint8_t
	{
		ACES = 0,
		AgX,
		Reinhard,
		None,
	};

	const char* TonemapperToString(Tonemapper tonemapper);
	Tonemapper TonemapperFromString(std::string_view text);

	struct SceneRendererSettings
	{
		// Shadows (directional light, cascaded)
		bool ShadowsEnabled = true;
		uint32_t ShadowMapSize = 2048;
		uint32_t CascadeCount = 4;
		float ShadowDistance = 80.0f;
		float CascadeSplitLambda = 0.85f;
		float ShadowSoftness = 0.03f;		// Light angular size; larger = softer penumbrae
		float ShadowBias = 0.0003f;
		float ShadowNormalBias = 1.5f;		// In shadow-map texels
		uint32_t ShadowPCFSamples = 16;

		// Ambient occlusion
		bool SSAOEnabled = true;
		bool SSAOHalfResolution = true;
		float SSAORadius = 0.6f;
		float SSAOBias = 0.025f;
		float SSAOPower = 1.6f;
		float SSAOStrength = 1.0f;
		uint32_t SSAOSamples = 16;

		// Bloom
		bool BloomEnabled = true;
		float BloomIntensity = 0.05f;
		float BloomThreshold = 1.0f;
		float BloomKnee = 0.5f;
		float BloomRadius = 1.0f;

		// Tonemapping and post
		float Exposure = 1.0f;
		Tonemapper ToneMapper = Tonemapper::ACES;
		float Vignette = 0.2f;
		bool FXAAEnabled = true;

		// Editor overlays
		bool ShowGrid = false;
		float GridCellSize = 1.0f;
		bool ShowSelectionOutline = true;
		bool ShowSelectionGlow = true;		// A faint glow fading out around the selection's outline
		bool ShowUI = true;					// Screen UI (the editor can hide it while editing the 3D scene)
		glm::vec4 OutlineColor { 1.0f, 0.52f, 0.12f, 1.0f };
	};

	struct RenderCamera
	{
		glm::mat4 View { 1.0f };
		glm::mat4 Projection { 1.0f };	// Depth 0..1, Y-up clip space (see Math::PerspectiveProjection)
		glm::vec3 Position { 0.0f };
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
	};

	struct DirectionalLightData
	{
		glm::vec3 Direction { 0.0f, -1.0f, 0.0f };	// Direction the light travels
		glm::vec3 Color { 1.0f };
		float Intensity = 3.0f;
		bool CastShadows = true;
	};

	struct PointLightData
	{
		glm::vec3 Position { 0.0f };
		glm::vec3 Color { 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
	};

	struct SpotLightData
	{
		glm::vec3 Position { 0.0f };
		glm::vec3 Direction { 0.0f, -1.0f, 0.0f };
		glm::vec3 Color { 1.0f };
		float Intensity = 10.0f;
		float Range = 10.0f;
		float InnerAngle = glm::radians(20.0f);
		float OuterAngle = glm::radians(30.0f);
	};

	struct EnvironmentSettings
	{
		Ref<Environment> EnvironmentMap;	// Null = flat ambient color + gradient background
		float Intensity = 1.0f;
		float Rotation = 0.0f;			// Radians around +Y
		float SkyboxBlur = 0.0f;		// Mip level of the radiance map shown as background
		glm::vec3 AmbientColor { 0.03f, 0.035f, 0.04f };
	};

	// How a block of text is drawn. Colors are sRGB with alpha; the outline grows the glyphs outward
	// by OutlineWidth ems (up to half the font's distance range, 0.1 em with the default atlas).
	struct TextStyle
	{
		glm::vec4 Color { 1.0f };
		glm::vec4 OutlineColor { 0.0f, 0.0f, 0.0f, 1.0f };
		float OutlineWidth = 0.0f;
	};

	// How a screen UI rectangle is drawn. Sizes are in output pixels; colors are sRGB with alpha. The
	// border is drawn inside the rectangle, and the corner radius is capped at half the shorter side.
	struct UIRectStyle
	{
		glm::vec4 Color { 1.0f };
		float CornerRadius = 0.0f;
		float BorderWidth = 0.0f;
		glm::vec4 BorderColor { 0.0f, 0.0f, 0.0f, 1.0f };
	};

	struct SceneRendererStatistics
	{
		uint32_t DrawCalls = 0;
		uint32_t ShadowDrawCalls = 0;
		uint64_t Triangles = 0;
		uint32_t Meshes = 0;
		uint32_t SkinnedMeshes = 0;		// Meshes drawn in a pose (each also counts in Meshes)
		uint32_t PointLights = 0;
		uint32_t SpotLights = 0;
		uint32_t Particles = 0;
		uint32_t Decals = 0;
	};

	// One camera-facing particle for SubmitParticles.
	struct ParticleQuad
	{
		enum class FacingMode : uint8_t
		{
			Camera,		// Toward the camera, turned by Rotation
			Stretched,	// Along Axis (its world velocity) as seen by the camera, Length longer than it is wide
			Flat,		// Lying across the plane whose normal is Axis, turned by Rotation
		};

		glm::vec3 Position { 0.0f };	// World space, the quad's center
		float Size = 0.1f;				// Meters across
		glm::vec4 Color { 1.0f };		// Linear RGB (may be above 1 to glow) and alpha
		glm::vec4 TexRect { 0.0f, 0.0f, 1.0f, 1.0f };	// The part of the texture shown: u0, v0, u1, v1
		float Rotation = 0.0f;			// Radians
		FacingMode Facing = FacingMode::Camera;
		glm::vec3 Axis { 0.0f, 1.0f, 0.0f };	// Stretched: the way it moves; Flat: the plane's normal (world)
		float Length = 0.0f;			// Stretched: meters added to its length along Axis
	};

	// One point along a strip (a trail, a ribbon, a beam): where it is, how wide, its color, and how far
	// along the texture it is (U; V runs across the strip, 0 on one edge and 1 on the other).
	struct StripPoint
	{
		glm::vec3 Position { 0.0f };	// World space
		float Width = 0.1f;				// Meters across
		glm::vec4 Color { 1.0f };		// Linear RGB (may be above 1 to glow) and alpha
		float U = 0.0f;
	};

	// Renders a scene into an off-screen LDR image (GetFinalImage), ready to present or show in the editor.
	//
	// Usage per frame:
	//   BeginScene(camera); Submit...(); EndScene();
	class SceneRenderer
	{
	public:
		SceneRenderer();
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		void SetViewportSize(uint32_t width, uint32_t height);
		uint32_t GetViewportWidth() const { return m_ViewportWidth; }
		uint32_t GetViewportHeight() const { return m_ViewportHeight; }

		// Times this renderer's passes on the GPU for the profiler, under 'name' (a string literal), or
		// stops timing them with nullptr. Off by default, so previews and tests don't take queries.
		void SetGpuTimingName(const char* name);

		SceneRendererSettings& GetSettings() { return m_Settings; }
		const SceneRendererSettings& GetSettings() const { return m_Settings; }

		void BeginScene(const RenderCamera& camera);
		void SetEnvironment(const EnvironmentSettings& environment);
		void SubmitDirectionalLight(const DirectionalLightData& light);
		void SubmitPointLight(const PointLightData& light);
		void SubmitSpotLight(const SpotLightData& light);

		// The most joint matrices a frame can draw (4 MB); skinned meshes past it are drawn as made.
		static constexpr size_t MaxFrameJoints = 65536;

		// Submits every submesh of 'mesh'. 'materialOverride' replaces all of the mesh's own materials.
		// A skinned mesh's skinned submeshes are drawn in the pose 'skinMatrices' gives (one per joint,
		// from Skeleton::ComputeSkinMatrices), or in its rest pose without them.
		void SubmitMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, uint32_t entityID = 0,
			const Ref<Material>& materialOverride = nullptr, bool castShadows = true,
			const std::vector<glm::mat4>* skinMatrices = nullptr);

		// Overlay lines (drawn after tonemapping, depth-tested against the scene).
		void SubmitLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
		void SubmitWireBox(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color);
		void SubmitWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
		void SubmitWireCapsule(const glm::mat4& transform, float radius, float halfHeight, const glm::vec4& color);

		// World text, drawn after tonemapping (its colors show exactly as given), depth-tested against the
		// scene, and into the entity ID image so it can be picked. 'layoutToWorld' maps the layout's box
		// space (x right, y down) into the world. A clip rectangle (min xy, max xy in box space) cuts
		// glyphs at its edges.
		void SubmitText(const Ref<Font>& font, const TextLayout& layout, const glm::mat4& layoutToWorld, const TextStyle& style,
			uint32_t entityID = 0, const glm::vec4* clipRect = nullptr);

		// Screen UI, drawn over everything else in submission order, at the output resolution, after
		// tonemapping, FXAA and the editor overlays (so it is never blurred, bloomed or tonemapped), and
		// into the entity ID image. Positions are output pixels with (0, 0) at the top-left and y down. A
		// clip rectangle (min xy, max xy in pixels) hides everything outside it. Nothing is drawn while
		// SceneRendererSettings::ShowUI is off.
		// 'uvRect' (min uv, max uv) is the part of the texture stretched over the rectangle.
		void SubmitUIRect(const glm::vec2& min, const glm::vec2& max, const UIRectStyle& style, const Ref<Texture2D>& texture = nullptr,
			uint32_t entityID = 0, const glm::vec4* clipRect = nullptr, const glm::vec4& uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
		// 'origin' is where the layout's box (0, 0) lands; the layout's units must be output pixels.
		void SubmitUIText(const Ref<Font>& font, const TextLayout& layout, const glm::vec2& origin, const TextStyle& style,
			uint32_t entityID = 0, const glm::vec4* clipRect = nullptr);

		// Particles: camera-facing quads lit by nothing, drawn into the HDR image after the transparent
		// pass (so they're tonemapped and bloom), depth-tested against the scene without writing depth.
		// Additive ones add light; alpha ones are sorted far to near. Without a texture, each quad is
		// a soft round dot; a texture's color and alpha are multiplied by the particle's color.
		void SubmitParticles(const ParticleQuad* quads, size_t count, bool additive, const Ref<Texture2D>& texture = nullptr);
		// A strip through 'points' (two or more), turned to face the camera along its length, drawn with
		// the particles. 'repeatTexture' tiles the texture along U past 0 to 1 (scrolling beams, tiled
		// trails); otherwise U is clamped. Without a texture, the strip fades out toward its edges.
		void SubmitStrip(const StripPoint* points, size_t count, bool additive, const Ref<Texture2D>& texture = nullptr, bool repeatTexture = false);

		// Decals: an image (or a plain color) projected onto opaque surfaces, blended into their color
		// before lighting, so it's lit and shadowed with them. 'boxToWorld' places the unit box (-0.5
		// to 0.5) it fills; it projects down the box's -Y, with the image's top toward its -Z. Surfaces
		// turned more than 'maxAngleDegrees' from the box's +Y fade out. Up to ShaderInterop::MaxDecals
		// are drawn (higher 'sortOrder' first, and on top of lower), with up to MaxDecalTextures images.
		void SubmitDecal(const glm::mat4& boxToWorld, const glm::vec4& color, const Ref<Texture2D>& texture = nullptr,
			float maxAngleDegrees = 60.0f, int sortOrder = 0);

		void SetSelectedEntities(const std::vector<uint32_t>& entityIDs);

		// Records and submits all passes.
		void EndScene();

		nvrhi::ITexture* GetFinalImage() const;
		nvrhi::ITexture* GetEntityIDImage() const;
		nvrhi::ITexture* GetHDRImage() const;

		// Entity ID (as submitted, 0 = none) at a viewport pixel. Blocks until the GPU has finished.
		uint32_t ReadEntityID(uint32_t x, uint32_t y) const;

		const SceneRendererStatistics& GetStatistics() const { return m_Statistics; }
	private:
		struct DrawCommand
		{
			const Mesh* MeshPtr = nullptr;
			uint32_t SubmeshIndex = 0;
			Material* MaterialPtr = nullptr;
			glm::mat4 Transform { 1.0f };
			AABB Bounds;					// World space; for a skinned draw, of its pose
			uint32_t EntityID = 0;
			uint32_t JointOffset = 0;		// Skinned: the first of its joint matrices in m_JointMatrices
			bool Skinned = false;
			bool Mirrored = false;			// Its transform turns space inside out (a negative scale), so its triangles wind clockwise
			bool CastShadows = true;
			float SortKey = 0.0f;
		};

		// The pipelines of one mesh pass: static or skinned, for one- or two-sided materials, drawn
		// as made or mirrored.
		struct MeshPassPipelines
		{
			nvrhi::GraphicsPipelineHandle Pipelines[2][2][2];	// [skinned][double-sided][mirrored]

			nvrhi::IGraphicsPipeline* Get(bool skinned, bool doubleSided, bool mirrored) const { return Pipelines[skinned][doubleSided][mirrored]; }
			bool IsComplete() const;
		};

		struct LineVertex
		{
			glm::vec3 Position;
			glm::vec4 Color;
		};

		struct TextVertex
		{
			glm::vec3 Position;
			glm::vec2 TexCoord;
			glm::vec4 Color;
			glm::vec4 OutlineColor;
			float OutlineWidth;		// In the atlas's distance units (0-0.5)
			uint32_t EntityID;
		};

		struct UIVertex
		{
			glm::vec2 Position;		// Output pixels
			glm::vec2 TexCoord;
			glm::vec4 Color;
			glm::vec4 Color2;		// Border or outline color
			glm::vec4 Shape;		// Rectangles: offset from the center, half size (pixels)
			glm::vec4 Params;		// Corner radius, border width, outline width, 0 rectangle / 1 glyph
			uint32_t EntityID;
		};

		// A run of UI vertices sharing a texture, a distance range and a clip rectangle.
		struct UIDraw
		{
			nvrhi::IBindingSet* Set = nullptr;
			float DistanceRange = 0.0f;
			glm::ivec4 Scissor { 0 };		// min xy, max xy in pixels
			uint32_t FirstVertex = 0;
			uint32_t VertexCount = 0;
		};

		struct ParticleVertex
		{
			glm::vec3 Position;
			glm::vec2 TexCoord;
			glm::vec4 Color;
		};

		// One emitter's particles: a run of vertices sharing a texture and a blend.
		struct ParticleDraw
		{
			Ref<Texture2D> TextureRef;
			bool Textured = false;
			bool Additive = true;
			bool Strip = false;			// Soft across V (a strip) instead of a round dot, when untextured
			bool RepeatTexture = false;
			uint32_t FirstVertex = 0;
			uint32_t VertexCount = 0;
			float Distance = 0.0f;
		};

		struct DecalDraw
		{
			glm::mat4 BoxToWorld { 1.0f };
			glm::vec4 Color { 1.0f };
			Ref<Texture2D> TextureRef;
			float CosMaxAngle = 0.5f;
			int SortOrder = 0;
		};

		// One layer of the decal atlas and the image it holds.
		struct DecalLayer
		{
			Ref<Texture2D> TextureRef;
			uint64_t LastUsed = 0;		// m_DecalFrame it was last drawn in
			bool Filled = false;		// Its image has been copied in
		};

		// One block of text: a run of vertices sharing a font, drawn far to near.
		struct TextDraw
		{
			Ref<Font> FontRef;
			uint32_t FirstVertex = 0;
			uint32_t VertexCount = 0;
			float Distance = 0.0f;
		};

		void CreatePipelines();
		void CreateRenderTargets();
		void CreateShadowMap();
		void CreateBindingSets();

		void UpdateFrameConstants(nvrhi::ICommandList* commandList);
		void ComputeCascades(ShaderInterop::FrameData& frame);

		void RenderShadows(nvrhi::ICommandList* commandList, const std::vector<DrawCommand>& casters);
		void RenderPrepass(nvrhi::ICommandList* commandList);
		void RenderSSAO(nvrhi::ICommandList* commandList);
		void RenderOpaque(nvrhi::ICommandList* commandList);
		void RenderSkybox(nvrhi::ICommandList* commandList);
		void RenderTransparent(nvrhi::ICommandList* commandList);
		void RenderParticles(nvrhi::ICommandList* commandList);
		// Picks the frame's decals and their atlas layers; true when it made the atlas (new binding sets needed).
		bool PrepareDecals();
		void UploadDecals(nvrhi::ICommandList* commandList);
		void RenderBloom(nvrhi::ICommandList* commandList);
		void RenderTonemap(nvrhi::ICommandList* commandList);
		void RenderText(nvrhi::ICommandList* commandList);
		void RenderOverlays(nvrhi::ICommandList* commandList);
		void RenderUI(nvrhi::ICommandList* commandList);
		void RenderSelectionOutline(nvrhi::ICommandList* commandList);
		// A texture and sampler set for the text and UI pipelines. 'owner' keeps the texture alive while
		// the set exists; sets whose owner nothing else holds are dropped at the next BeginScene.
		nvrhi::IBindingSet* GetTextureBindingSet(nvrhi::ITexture* texture, std::shared_ptr<const void> owner, bool repeat = false);
		nvrhi::IBindingSet* GetTextBindingSet(const Ref<Font>& font);
		void AddUIQuad(nvrhi::IBindingSet* set, float distanceRange, const glm::vec4* clipRect, const UIVertex (&corners)[4]);

		void DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer,
			nvrhi::IBindingSet* bindingSet, uint32_t width, uint32_t height);
		void DrawMeshes(nvrhi::ICommandList* commandList, const std::vector<DrawCommand>& draws, nvrhi::IFramebuffer* framebuffer,
			const MeshPassPipelines& pipelines);
		void UploadJointMatrices(nvrhi::ICommandList* commandList);

		nvrhi::BindingSetHandle CreatePostProcessSet(nvrhi::IBuffer* constants, std::initializer_list<nvrhi::ITexture*> textures,
			nvrhi::TextureDimension firstDimension = nvrhi::TextureDimension::Texture2D);
		nvrhi::BufferHandle CreateVolatileConstantBuffer(size_t size, const char* name);
		nvrhi::GraphicsPipelineHandle CreateFullscreenPipeline(const char* fragmentShader, const nvrhi::FramebufferInfo& framebufferInfo,
			const nvrhi::BlendState::RenderTarget& blend = nvrhi::BlendState::RenderTarget());
	private:
		SceneRendererSettings m_Settings;
		SceneRendererSettings m_AppliedSettings;
		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;
		bool m_TargetsDirty = true;

		// Per-frame scene data
		RenderCamera m_Camera;
		EnvironmentSettings m_Environment;
		Ref<Environment> m_BoundEnvironment;
		DirectionalLightData m_DirectionalLight;
		bool m_HasDirectionalLight = false;
		std::vector<PointLightData> m_PointLights;
		std::vector<SpotLightData> m_SpotLights;
		std::vector<DrawCommand> m_OpaqueDraws;
		std::vector<DrawCommand> m_TransparentDraws;
		std::vector<glm::mat4> m_JointMatrices;		// World space, of every skinned draw this frame
		bool m_WarnedJointLimit = false;
		std::vector<LineVertex> m_LineVertices;
		std::vector<TextVertex> m_TextVertices;
		std::vector<TextDraw> m_TextDraws;
		std::vector<ParticleVertex> m_ParticleVertices;
		std::vector<ParticleDraw> m_ParticleDraws;
		std::vector<glm::vec3> m_StripSides;		// SubmitStrip's working space, kept to avoid allocating each frame
		std::vector<uint8_t> m_StripSideKnown;
		std::vector<uint32_t> m_ParticleOrder;		// Scratch: an alpha emitter's quads far to near
		std::vector<DecalDraw> m_Decals;
		ShaderInterop::DecalData m_DecalData {};
		std::vector<DecalLayer> m_DecalLayers;		// One per atlas layer, once the atlas exists
		uint64_t m_DecalFrame = 0;
		bool m_WarnedDecalLimit = false;
		bool m_WarnedDecalImageLimit = false;
		std::vector<UIVertex> m_UIVertices;
		std::vector<UIDraw> m_UIDraws;
		std::vector<uint32_t> m_SelectedEntities;
		std::vector<Ref<Mesh>> m_FrameMeshes;			// Keeps submitted meshes alive until the frame is recorded
		std::vector<Ref<Material>> m_FrameMaterials;
		AABB m_SceneBounds;
		bool m_InScene = false;

		SceneRendererStatistics m_Statistics;
		ShaderInterop::FrameData m_FrameData {};

		nvrhi::CommandListHandle m_CommandList;
		Scope<GpuTimer> m_GpuTimer;
		nvrhi::InputLayoutHandle m_MeshInputLayout;
		nvrhi::InputLayoutHandle m_SkinnedMeshInputLayout;	// Vertex and SkinVertex streams
		nvrhi::InputLayoutHandle m_LineInputLayout;
		nvrhi::InputLayoutHandle m_TextInputLayout;
		nvrhi::InputLayoutHandle m_UIInputLayout;
		nvrhi::InputLayoutHandle m_ParticleInputLayout;

		// Render targets
		nvrhi::TextureHandle m_DepthTexture;
		nvrhi::TextureHandle m_NormalTexture;
		nvrhi::TextureHandle m_EntityIDTexture;
		nvrhi::TextureHandle m_HDRTexture;
		nvrhi::TextureHandle m_SSAOTexture;
		nvrhi::TextureHandle m_SSAOBlurTexture;
		nvrhi::TextureHandle m_LDRTexture;
		nvrhi::TextureHandle m_FinalTexture;
		std::vector<nvrhi::TextureHandle> m_BloomMips;
		nvrhi::TextureHandle m_ShadowMap;
		uint32_t m_SSAOWidth = 0, m_SSAOHeight = 0;

		nvrhi::FramebufferHandle m_PrepassFramebuffer;
		nvrhi::FramebufferHandle m_HDRDepthFramebuffer;		// HDR color + read-only depth
		nvrhi::FramebufferHandle m_HDRFramebuffer;
		nvrhi::FramebufferHandle m_SSAOFramebuffer;
		nvrhi::FramebufferHandle m_SSAOBlurFramebuffer;
		nvrhi::FramebufferHandle m_LDRFramebuffer;
		nvrhi::FramebufferHandle m_FinalFramebuffer;
		nvrhi::FramebufferHandle m_FinalDepthFramebuffer;
		nvrhi::FramebufferHandle m_EntityIDDepthFramebuffer;	// Entity IDs + read-only depth (text picking)
		nvrhi::FramebufferHandle m_EntityIDFramebuffer;		// Entity IDs alone (UI picking)
		std::vector<nvrhi::FramebufferHandle> m_BloomFramebuffers;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;

		// Pipelines
		nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
		nvrhi::GraphicsPipelineHandle m_ShadowSkinnedPipeline;
		MeshPassPipelines m_PrepassPipelines, m_OpaquePipelines, m_TransparentPipelines;
		nvrhi::GraphicsPipelineHandle m_SkyboxPipeline;
		nvrhi::GraphicsPipelineHandle m_SSAOPipeline, m_SSAOBlurPipeline;
		nvrhi::GraphicsPipelineHandle m_BloomDownsamplePipeline, m_BloomUpsamplePipeline;
		nvrhi::GraphicsPipelineHandle m_TonemapPipeline, m_FXAAPipeline;
		nvrhi::GraphicsPipelineHandle m_GridPipeline, m_LinePipeline, m_OutlinePipeline;
		nvrhi::GraphicsPipelineHandle m_TextPipeline, m_TextIDPipeline;
		nvrhi::GraphicsPipelineHandle m_UIPipeline, m_UIIDPipeline;
		nvrhi::GraphicsPipelineHandle m_ParticleAdditivePipeline, m_ParticleAlphaPipeline;
		nvrhi::GraphicsPipelineHandle m_DecalCopyPipeline;

		// Constant buffers
		nvrhi::BufferHandle m_FrameBuffer;
		nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BufferHandle m_DecalBuffer;
		nvrhi::BufferHandle m_DecalCopyConstants;
		nvrhi::TextureHandle m_DecalAtlas;			// Made when the first decal image is drawn
		std::vector<nvrhi::FramebufferHandle> m_DecalAtlasFramebuffers;	// [layer * mips + mip]
		nvrhi::BufferHandle m_SSAOConstants, m_SSAOBlurConstantsH, m_SSAOBlurConstantsV;
		nvrhi::BufferHandle m_SkyConstants;
		std::vector<nvrhi::BufferHandle> m_BloomConstants;
		nvrhi::BufferHandle m_TonemapConstants, m_FXAAConstants;
		nvrhi::BufferHandle m_GridConstants, m_OutlineConstants;
		nvrhi::BufferHandle m_LineVertexBuffer;
		size_t m_LineVertexCapacity = 0;
		nvrhi::BufferHandle m_TextVertexBuffer;
		size_t m_TextVertexCapacity = 0;
		nvrhi::BufferHandle m_UIVertexBuffer;
		size_t m_UIVertexCapacity = 0;
		nvrhi::BufferHandle m_ParticleVertexBuffer;
		size_t m_ParticleVertexCapacity = 0;

		// Binding sets
		nvrhi::BindingSetHandle m_SceneSet;
		nvrhi::BindingSetHandle m_ShadowSet;
		nvrhi::BufferHandle m_JointBuffer;			// m_JointMatrices on the GPU; grows as needed
		nvrhi::BindingSetHandle m_SkinSet;
		nvrhi::BindingSetHandle m_LineSet;
		nvrhi::BindingSetHandle m_SSAOSet, m_SSAOBlurSetH, m_SSAOBlurSetV;
		nvrhi::BindingSetHandle m_SkySet;
		std::vector<nvrhi::BindingSetHandle> m_BloomDownsampleSets, m_BloomUpsampleSets;
		nvrhi::BindingSetHandle m_TonemapSet, m_FXAASet;
		nvrhi::BindingSetHandle m_GridSet, m_OutlineSet;
		// One per texture (font atlases and UI images).
		struct TextureBinding
		{
			std::shared_ptr<const void> Owner;
			nvrhi::BindingSetHandle Set;
		};
		std::unordered_map<nvrhi::ITexture*, TextureBinding> m_TextureSets;
		std::unordered_map<nvrhi::ITexture*, TextureBinding> m_RepeatTextureSets;	// The same, with a wrapping sampler
	};

}
