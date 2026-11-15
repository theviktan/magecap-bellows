#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Image.h"
#include "Bellows/Renderer/Mesh.h"
#include "Bellows/Renderer/Texture.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <functional>
#include <string>

namespace Bellows {

	class RenderDevice;

	// Process-wide rendering services: shaders, shared binding layouts, samplers,
	// default textures, built-in meshes and GPU readback.
	class Renderer
	{
	public:
		static bool Init(RenderDevice& device);
		static void Shutdown();
		static bool IsInitialized();

		static nvrhi::IDevice* GetDevice();
		static RenderDevice& GetRenderDevice();

		// Loads "<ShaderDirectory>/<name>.spv" (e.g. "PBR.frag"). The stage is taken from the extension.
		static nvrhi::ShaderHandle GetShader(const std::string& name);
		static void SetShaderDirectory(const std::filesystem::path& directory);
		static const std::filesystem::path& GetShaderDirectory();

		// Shared binding layouts (see the GLSL include files for the matching declarations).
		static nvrhi::IBindingLayout* GetSceneBindingLayout();		// set 0 of mesh passes
		static nvrhi::IBindingLayout* GetMaterialBindingLayout();	// set 1 of mesh passes
		static nvrhi::IBindingLayout* GetShadowBindingLayout();		// set 0 of the shadow pass
		static nvrhi::IBindingLayout* GetPostProcessBindingLayout();	// fullscreen passes
		static nvrhi::IBindingLayout* GetComputeBindingLayout();		// image-based lighting compute passes
		static nvrhi::IBindingLayout* GetLineBindingLayout();		// debug lines

		static nvrhi::ISampler* GetLinearClampSampler();
		static nvrhi::ISampler* GetLinearWrapSampler();
		static nvrhi::ISampler* GetPointClampSampler();
		static nvrhi::ISampler* GetShadowCompareSampler();
		static nvrhi::ISampler* GetMaterialSampler();		// Trilinear, anisotropic, wrap

		static const Ref<Texture2D>& GetWhiteTexture();
		static const Ref<Texture2D>& GetBlackTexture();
		static const Ref<Texture2D>& GetFlatNormalTexture();
		static nvrhi::ITexture* GetBlackCubeTexture();
		static nvrhi::ITexture* GetBlackTextureArray();
		static nvrhi::ITexture* GetBRDFLut();

		// Built-in primitive meshes are created once and shared.
		static Ref<Mesh> GetPrimitiveMesh(PrimitiveMesh primitive);

		// Records commands into a fresh command list and submits it.
		// When waitForCompletion is true the call blocks until the GPU has finished.
		static void ExecuteImmediate(const std::function<void(nvrhi::ICommandList*)>& record, bool waitForCompletion = false);

		// Copies a color texture to the CPU as RGBA8 (supports RGBA8, BGRA8, sRGB variants and RGBA16F).
		static Image ReadbackTexture(nvrhi::ITexture* texture);
		// Reads one pixel of an R32_UINT texture (entity picking).
		static uint32_t ReadbackPixelUInt(nvrhi::ITexture* texture, uint32_t x, uint32_t y);
	};

}
