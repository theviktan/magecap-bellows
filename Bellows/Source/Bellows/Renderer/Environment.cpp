#include "bwpch.h"
#include "Bellows/Renderer/Environment.h"

#include "Bellows/Renderer/Renderer.h"

namespace Bellows {

	namespace {

		nvrhi::TextureHandle CreateCube(uint32_t size, uint32_t mipCount, const char* name)
		{
			nvrhi::TextureDesc desc;
			desc.width = size;
			desc.height = size;
			desc.arraySize = 6;
			desc.mipLevels = mipCount;
			desc.dimension = nvrhi::TextureDimension::TextureCube;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.isUAV = true;
			desc.debugName = name;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return Renderer::GetDevice()->createTexture(desc);
		}

		// Helper that runs one compute dispatch over all six faces of a cube mip.
		class ComputePass
		{
		public:
			explicit ComputePass(const std::string& shaderName)
			{
				nvrhi::IDevice* device = Renderer::GetDevice();
				nvrhi::ComputePipelineDesc desc;
				desc.CS = Renderer::GetShader(shaderName);
				desc.bindingLayouts = { Renderer::GetComputeBindingLayout() };
				if (desc.CS)
					m_Pipeline = device->createComputePipeline(desc);
			}

			bool IsValid() const { return m_Pipeline != nullptr; }

			void Dispatch(nvrhi::ICommandList* commandList, const glm::vec4* constants, size_t constantCount,
				nvrhi::ITexture* source, nvrhi::TextureDimension sourceDimension,
				nvrhi::ITexture* destination, uint32_t destinationMip, uint32_t size,
				nvrhi::TextureSubresourceSet sourceSubresources = nvrhi::AllSubresources)
			{
				nvrhi::IDevice* device = Renderer::GetDevice();
				nvrhi::BufferHandle constantBuffer = device->createBuffer(nvrhi::BufferDesc()
					.setByteSize(sizeof(glm::vec4) * constantCount).setIsConstantBuffer(true)
					.setInitialState(nvrhi::ResourceStates::ConstantBuffer).setKeepInitialState(true).setDebugName("IBLConstants"));
				commandList->writeBuffer(constantBuffer, constants, sizeof(glm::vec4) * constantCount);

				nvrhi::BindingSetDesc setDesc;
				setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, constantBuffer));
				setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, source, nvrhi::Format::UNKNOWN, sourceSubresources, sourceDimension));
				setDesc.addItem(nvrhi::BindingSetItem::Sampler(2, Renderer::GetLinearClampSampler()));
				setDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(3, destination, nvrhi::Format::UNKNOWN,
					nvrhi::TextureSubresourceSet(destinationMip, 1, 0, 6), nvrhi::TextureDimension::Texture2DArray));
				nvrhi::BindingSetHandle bindingSet = device->createBindingSet(setDesc, Renderer::GetComputeBindingLayout());

				nvrhi::ComputeState state;
				state.pipeline = m_Pipeline;
				state.bindings = { bindingSet };
				commandList->setComputeState(state);
				const uint32_t groups = std::max((size + 7) / 8, 1u);
				commandList->dispatch(groups, groups, 6);
			}
		private:
			nvrhi::ComputePipelineHandle m_Pipeline;
		};

	}

	Ref<Environment> Environment::CreateFromEquirectangular(const Ref<Texture2D>& equirect, const EnvironmentQuality& quality)
	{
		if (!equirect)
			return nullptr;
		Ref<Environment> environment = CreateRef<Environment>();
		if (!environment->Build(&equirect, nullptr, quality))
			return nullptr;
		return environment;
	}

	Ref<Environment> Environment::LoadFromFile(const std::filesystem::path& path, const EnvironmentQuality& quality)
	{
		Ref<Texture2D> equirect = Texture2D::LoadFromFile(path, TextureColorSpace::Linear);
		if (!equirect)
			return nullptr;
		return CreateFromEquirectangular(equirect, quality);
	}

	Ref<Environment> Environment::CreateProcedural(const ProceduralSkySettings& settings, const EnvironmentQuality& quality)
	{
		Ref<Environment> environment = CreateRef<Environment>();
		if (!environment->Build(nullptr, &settings, quality))
			return nullptr;
		return environment;
	}

	bool Environment::Build(const Ref<Texture2D>* equirect, const ProceduralSkySettings* sky, const EnvironmentQuality& quality)
	{
		const uint32_t cubeSize = std::max(quality.CubeSize, 16u);
		const uint32_t cubeMips = Texture2D::CalculateMipCount(cubeSize, cubeSize);
		const uint32_t irradianceSize = std::max(quality.IrradianceSize, 8u);
		const uint32_t prefilteredSize = std::max(quality.PrefilteredSize, 16u);
		const uint32_t prefilteredMips = std::min(Texture2D::CalculateMipCount(prefilteredSize, prefilteredSize), 6u);

		nvrhi::TextureHandle radiance = CreateCube(cubeSize, cubeMips, "EnvironmentRadiance");
		nvrhi::TextureHandle irradiance = CreateCube(irradianceSize, 1, "EnvironmentIrradiance");
		nvrhi::TextureHandle prefiltered = CreateCube(prefilteredSize, prefilteredMips, "EnvironmentPrefiltered");
		if (!radiance || !irradiance || !prefiltered)
			return false;

		ComputePass equirectPass("EquirectToCube.comp");
		ComputePass skyPass("ProceduralSky.comp");
		ComputePass downsamplePass("CubeDownsample.comp");
		ComputePass irradiancePass("Irradiance.comp");
		ComputePass prefilterPass("Prefilter.comp");
		if (!equirectPass.IsValid() || !skyPass.IsValid() || !downsamplePass.IsValid() || !irradiancePass.IsValid() || !prefilterPass.IsValid())
		{
			BW_CORE_ERROR("Environment: image-based lighting shaders are missing");
			return false;
		}

		Renderer::ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			// 1) Base radiance cube
			if (equirect)
			{
				const glm::vec4 constants[] = { glm::vec4(static_cast<float>(cubeSize), 64.0f, 0.0f, 0.0f) };
				equirectPass.Dispatch(commandList, constants, 1, (*equirect)->GetHandle(), nvrhi::TextureDimension::Texture2D, radiance, 0, cubeSize);
			}
			else
			{
				const glm::vec4 constants[] = {
					glm::vec4(static_cast<float>(cubeSize), sky->SunAngularRadius, sky->SunIntensity, 0.0f),
					glm::vec4(glm::normalize(sky->SunDirection), 0.0f),
					glm::vec4(sky->ZenithColor, 1.0f),
					glm::vec4(sky->HorizonColor, 1.0f),
					glm::vec4(sky->GroundColor, 1.0f),
				};
				skyPass.Dispatch(commandList, constants, 5, Renderer::GetWhiteTexture()->GetHandle(), nvrhi::TextureDimension::Texture2D, radiance, 0, cubeSize);
			}

			// 2) Radiance mip chain (used for filtered importance sampling and blurred skyboxes)
			for (uint32_t mip = 1; mip < cubeMips; mip++)
			{
				const uint32_t size = std::max(cubeSize >> mip, 1u);
				// The source view only contains the previous mip, so it is sampled at LOD 0.
				const glm::vec4 constants[] = { glm::vec4(static_cast<float>(size), 0.0f, 0.0f, 0.0f) };
				downsamplePass.Dispatch(commandList, constants, 1, radiance, nvrhi::TextureDimension::TextureCube, radiance, mip, size,
					nvrhi::TextureSubresourceSet(mip - 1, 1, 0, 6));
			}

			// 3) Diffuse irradiance
			{
				const float sourceMip = std::max(static_cast<float>(cubeMips) - 6.0f, 0.0f);
				const glm::vec4 constants[] = { glm::vec4(static_cast<float>(irradianceSize), sourceMip, 0.0f, 0.0f) };
				irradiancePass.Dispatch(commandList, constants, 1, radiance, nvrhi::TextureDimension::TextureCube, irradiance, 0, irradianceSize);
			}

			// 4) Specular prefiltering: one roughness level per mip
			for (uint32_t mip = 0; mip < prefilteredMips; mip++)
			{
				const uint32_t size = std::max(prefilteredSize >> mip, 1u);
				const float roughness = prefilteredMips > 1 ? static_cast<float>(mip) / static_cast<float>(prefilteredMips - 1) : 0.0f;
				const glm::vec4 constants[] = { glm::vec4(static_cast<float>(size), roughness, static_cast<float>(cubeSize), static_cast<float>(quality.PrefilterSamples)) };
				prefilterPass.Dispatch(commandList, constants, 1, radiance, nvrhi::TextureDimension::TextureCube, prefiltered, mip, size);
			}
		});

		m_Radiance = CreateRef<TextureCube>(radiance, cubeSize, cubeMips);
		m_Irradiance = CreateRef<TextureCube>(irradiance, irradianceSize, 1);
		m_Prefiltered = CreateRef<TextureCube>(prefiltered, prefilteredSize, prefilteredMips);
		return true;
	}

}
