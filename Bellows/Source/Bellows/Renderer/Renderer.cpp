#include "bwpch.h"
#include "Bellows/Renderer/Renderer.h"

#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/ShaderInterop.h"

#include <glm/gtc/packing.hpp>

namespace Bellows {

	namespace {

		struct RendererData
		{
			RenderDevice* Device = nullptr;
			std::filesystem::path ShaderDirectory;
			std::unordered_map<std::string, nvrhi::ShaderHandle> Shaders;

			nvrhi::BindingLayoutHandle SceneLayout;
			nvrhi::BindingLayoutHandle MaterialLayout;
			nvrhi::BindingLayoutHandle ShadowLayout;
			nvrhi::BindingLayoutHandle SkinLayout;
			nvrhi::BindingLayoutHandle PostProcessLayout;
			nvrhi::BindingLayoutHandle ComputeLayout;
			nvrhi::BindingLayoutHandle LineLayout;
			nvrhi::BindingLayoutHandle TextLayout;

			nvrhi::SamplerHandle LinearClamp;
			nvrhi::SamplerHandle LinearWrap;
			nvrhi::SamplerHandle PointClamp;
			nvrhi::SamplerHandle ShadowCompare;
			nvrhi::SamplerHandle MaterialSamplers[2][3][3];		// [filter][wrap U][wrap V]

			Ref<Texture2D> WhiteTexture;
			Ref<Texture2D> BlackTexture;
			Ref<Texture2D> FlatNormalTexture;
			nvrhi::TextureHandle BlackCube;
			nvrhi::TextureHandle BlackArray;
			nvrhi::TextureHandle BRDFLut;

			std::unordered_map<PrimitiveMesh, Ref<Mesh>> Primitives;
		};

		RendererData* s_Data = nullptr;

		// All of our GLSL uses explicit binding numbers, so NVRHI's per-type register offsets are disabled.
		constexpr nvrhi::VulkanBindingOffsets c_NoBindingOffsets = nvrhi::VulkanBindingOffsets()
			.setShaderResourceOffset(0).setSamplerOffset(0).setConstantBufferOffset(0).setUnorderedAccessViewOffset(0);

		constexpr uint32_t c_PushConstantSlot = 31;

		nvrhi::ShaderType ShaderTypeFromName(const std::string& name)
		{
			const std::string extension = std::filesystem::path(name).extension().string();
			if (extension == ".vert") return nvrhi::ShaderType::Vertex;
			if (extension == ".frag") return nvrhi::ShaderType::Pixel;
			if (extension == ".comp") return nvrhi::ShaderType::Compute;
			return nvrhi::ShaderType::None;
		}

		bool CreateBindingLayouts()
		{
			nvrhi::IDevice* device = s_Data->Device->GetDevice();

			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				// Per-frame data lives in volatile (dynamic) constant buffers.
				desc.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
				desc.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1));
				for (uint32_t slot = 2; slot <= 6; slot++)
					desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
				for (uint32_t slot = 7; slot <= 9; slot++)
					desc.addItem(nvrhi::BindingLayoutItem::Sampler(slot));
				desc.addItem(nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, 128));
				s_Data->SceneLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(1);	// set = 1
				desc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
				for (uint32_t slot = 1; slot <= 5; slot++)
					desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(6));
				// Separate roughness and metallic maps (downloaded texture sets) come after the sampler.
				desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(7));
				desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(8));
				s_Data->MaterialLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				desc.addItem(nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, 128));
				s_Data->ShadowLayout = device->createBindingLayout(desc);
			}
			{
				// Skinned draws add the frame's joint matrices as set 2.
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::Vertex;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(2);
				desc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0));
				s_Data->SkinLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				desc.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
				for (uint32_t slot = 1; slot <= 4; slot++)
					desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(5));
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(6));
				s_Data->PostProcessLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::Compute;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				desc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
				desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(2));
				desc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3));
				s_Data->ComputeLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				desc.addItem(nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, 64));
				s_Data->LineLayout = device->createBindingLayout(desc);
			}
			{
				nvrhi::BindingLayoutDesc desc;
				desc.visibility = nvrhi::ShaderType::All;
				desc.bindingOffsets = c_NoBindingOffsets;
				desc.setRegisterSpaceAndDescriptorSet(0);
				desc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(1));
				desc.addItem(nvrhi::BindingLayoutItem::PushConstants(c_PushConstantSlot, sizeof(ShaderInterop::TextData)));
				s_Data->TextLayout = device->createBindingLayout(desc);
			}

			return s_Data->SceneLayout && s_Data->MaterialLayout && s_Data->ShadowLayout && s_Data->SkinLayout
				&& s_Data->PostProcessLayout && s_Data->ComputeLayout && s_Data->LineLayout && s_Data->TextLayout;
		}

		void CreateSamplers()
		{
			nvrhi::IDevice* device = s_Data->Device->GetDevice();

			s_Data->LinearClamp = device->createSampler(nvrhi::SamplerDesc()
				.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
			s_Data->LinearWrap = device->createSampler(nvrhi::SamplerDesc()
				.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
			s_Data->PointClamp = device->createSampler(nvrhi::SamplerDesc()
				.setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
			s_Data->ShadowCompare = device->createSampler(nvrhi::SamplerDesc()
				.setAllFilters(true).setMipFilter(false)
				.setAllAddressModes(nvrhi::SamplerAddressMode::Border).setBorderColor(nvrhi::Color(1.0f))
				.setReductionType(nvrhi::SamplerReductionType::Comparison));
			auto addressMode = [](int wrap)
			{
				switch (static_cast<TextureWrap>(wrap))
				{
					case TextureWrap::Clamp:	return nvrhi::SamplerAddressMode::Clamp;
					case TextureWrap::Mirror:	return nvrhi::SamplerAddressMode::Mirror;
					case TextureWrap::Repeat:	break;
				}
				return nvrhi::SamplerAddressMode::Wrap;
			};
			for (int filter = 0; filter < 2; filter++)
				for (int wrapU = 0; wrapU < 3; wrapU++)
					for (int wrapV = 0; wrapV < 3; wrapV++)
					{
						// Pixelated textures take the nearest pixel of the nearest mip level, so a palette
						// cell never blends into its neighbor; anisotropy would filter, so it's off.
						const bool smooth = static_cast<TextureFilter>(filter) == TextureFilter::Linear;
						nvrhi::SamplerDesc desc;
						desc.setAllFilters(smooth).setMaxAnisotropy(smooth ? 8.0f : 1.0f)
							.setAddressU(addressMode(wrapU)).setAddressV(addressMode(wrapV)).setAddressW(nvrhi::SamplerAddressMode::Wrap);
						s_Data->MaterialSamplers[filter][wrapU][wrapV] = device->createSampler(desc);
					}
		}

		void CreateDefaultTextures()
		{
			const uint32_t white = 0xFFFFFFFF;
			const uint32_t black = 0xFF000000;
			const uint8_t flatNormal[4] = { 128, 128, 255, 255 };

			s_Data->WhiteTexture = Texture2D::CreateFromRGBA8(1, 1, &white, TextureColorSpace::Linear, "DefaultWhite", false);
			s_Data->BlackTexture = Texture2D::CreateFromRGBA8(1, 1, &black, TextureColorSpace::Linear, "DefaultBlack", false);
			s_Data->FlatNormalTexture = Texture2D::CreateFromRGBA8(1, 1, flatNormal, TextureColorSpace::Linear, "DefaultNormal", false);

			nvrhi::IDevice* device = s_Data->Device->GetDevice();

			nvrhi::TextureDesc cubeDesc;
			cubeDesc.width = 1;
			cubeDesc.height = 1;
			cubeDesc.arraySize = 6;
			cubeDesc.dimension = nvrhi::TextureDimension::TextureCube;
			cubeDesc.format = nvrhi::Format::RGBA16_FLOAT;
			cubeDesc.debugName = "DefaultBlackCube";
			cubeDesc.isRenderTarget = true;
			cubeDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			cubeDesc.keepInitialState = true;
			s_Data->BlackCube = device->createTexture(cubeDesc);

			nvrhi::TextureDesc arrayDesc;
			arrayDesc.width = 1;
			arrayDesc.height = 1;
			arrayDesc.arraySize = 4;
			arrayDesc.dimension = nvrhi::TextureDimension::Texture2DArray;
			arrayDesc.format = nvrhi::Format::D32;
			arrayDesc.debugName = "DefaultShadowArray";
			arrayDesc.isRenderTarget = true;
			arrayDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			arrayDesc.keepInitialState = true;
			s_Data->BlackArray = device->createTexture(arrayDesc);

			Renderer::ExecuteImmediate([](nvrhi::ICommandList* commandList)
			{
				commandList->clearTextureFloat(s_Data->BlackCube, nvrhi::AllSubresources, nvrhi::Color(0.0f));
				commandList->clearDepthStencilTexture(s_Data->BlackArray, nvrhi::AllSubresources, true, 1.0f, false, 0);
			});
		}

		bool CreateBRDFLut()
		{
			nvrhi::IDevice* device = s_Data->Device->GetDevice();
			constexpr uint32_t lutSize = 256;

			nvrhi::TextureDesc desc;
			desc.width = lutSize;
			desc.height = lutSize;
			desc.format = nvrhi::Format::RG16_FLOAT;
			desc.isUAV = true;
			desc.debugName = "BRDFLut";
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			s_Data->BRDFLut = device->createTexture(desc);

			nvrhi::ShaderHandle shader = Renderer::GetShader("BRDFLut.comp");
			if (!shader || !s_Data->BRDFLut)
				return false;

			nvrhi::ComputePipelineDesc pipelineDesc;
			pipelineDesc.CS = shader;
			pipelineDesc.bindingLayouts = { s_Data->ComputeLayout };
			nvrhi::ComputePipelineHandle pipeline = device->createComputePipeline(pipelineDesc);

			nvrhi::BufferHandle constants = device->createBuffer(nvrhi::BufferDesc()
				.setByteSize(sizeof(glm::vec4)).setIsConstantBuffer(true)
				.setInitialState(nvrhi::ResourceStates::ConstantBuffer).setKeepInitialState(true).setDebugName("BRDFLutConstants"));

			nvrhi::BindingSetDesc setDesc;
			setDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, constants));
			setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, s_Data->WhiteTexture->GetHandle()));
			setDesc.addItem(nvrhi::BindingSetItem::Sampler(2, s_Data->LinearClamp));
			setDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(3, s_Data->BRDFLut, nvrhi::Format::UNKNOWN,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1), nvrhi::TextureDimension::Texture2D));
			nvrhi::BindingSetHandle bindingSet = device->createBindingSet(setDesc, s_Data->ComputeLayout);

			Renderer::ExecuteImmediate([&](nvrhi::ICommandList* commandList)
			{
				const glm::vec4 params(static_cast<float>(lutSize), 0.0f, 0.0f, 0.0f);
				commandList->writeBuffer(constants, &params, sizeof(params));

				nvrhi::ComputeState state;
				state.pipeline = pipeline;
				state.bindings = { bindingSet };
				commandList->setComputeState(state);
				commandList->dispatch(lutSize / 8, lutSize / 8, 1);
			});
			return true;
		}

		float HalfToFloat(uint16_t value)
		{
			return glm::unpackHalf1x16(value);
		}

	}

	bool Renderer::Init(RenderDevice& device)
	{
		if (s_Data)
			return true;

		s_Data = new RendererData();
		s_Data->Device = &device;
		if (s_Data->ShaderDirectory.empty())
			s_Data->ShaderDirectory = Platform::GetExecutableDirectory() / "Shaders";

		if (!CreateBindingLayouts())
		{
			BW_CORE_CRITICAL("Failed to create renderer binding layouts");
			Shutdown();
			return false;
		}
		CreateSamplers();
		CreateDefaultTextures();
		if (!CreateBRDFLut())
		{
			BW_CORE_CRITICAL("Failed to create the BRDF lookup table (are the shaders in '{}'?)", s_Data->ShaderDirectory.string());
			Shutdown();
			return false;
		}
		return true;
	}

	void Renderer::Shutdown()
	{
		if (!s_Data)
			return;
		if (s_Data->Device)
			s_Data->Device->WaitIdle();
		delete s_Data;
		s_Data = nullptr;
	}

	bool Renderer::IsInitialized()
	{
		return s_Data != nullptr;
	}

	nvrhi::IDevice* Renderer::GetDevice()
	{
		BW_CORE_ASSERT(s_Data, "Renderer is not initialized");
		return s_Data->Device->GetDevice();
	}

	RenderDevice& Renderer::GetRenderDevice()
	{
		BW_CORE_ASSERT(s_Data, "Renderer is not initialized");
		return *s_Data->Device;
	}

	void Renderer::SetShaderDirectory(const std::filesystem::path& directory)
	{
		if (s_Data)
		{
			s_Data->ShaderDirectory = directory;
			s_Data->Shaders.clear();
		}
	}

	const std::filesystem::path& Renderer::GetShaderDirectory()
	{
		return s_Data->ShaderDirectory;
	}

	nvrhi::ShaderHandle Renderer::GetShader(const std::string& name)
	{
		auto it = s_Data->Shaders.find(name);
		if (it != s_Data->Shaders.end())
			return it->second;

		const std::filesystem::path path = s_Data->ShaderDirectory / (name + ".spv");
		Buffer binary = FileSystem::ReadBinaryFile(path);
		if (!binary)
		{
			BW_CORE_ERROR("Shader '{}' not found at '{}'", name, path.string());
			return nullptr;
		}

		nvrhi::ShaderDesc desc;
		desc.shaderType = ShaderTypeFromName(name);
		desc.debugName = name;
		desc.entryName = "main";
		nvrhi::ShaderHandle shader = GetDevice()->createShader(desc, binary.Data(), binary.Size());
		if (!shader)
		{
			BW_CORE_ERROR("Failed to create shader '{}'", name);
			return nullptr;
		}
		s_Data->Shaders[name] = shader;
		return shader;
	}

	nvrhi::IBindingLayout* Renderer::GetSceneBindingLayout() { return s_Data->SceneLayout; }
	nvrhi::IBindingLayout* Renderer::GetMaterialBindingLayout() { return s_Data->MaterialLayout; }
	nvrhi::IBindingLayout* Renderer::GetShadowBindingLayout() { return s_Data->ShadowLayout; }
	nvrhi::IBindingLayout* Renderer::GetSkinBindingLayout() { return s_Data->SkinLayout; }
	nvrhi::IBindingLayout* Renderer::GetPostProcessBindingLayout() { return s_Data->PostProcessLayout; }
	nvrhi::IBindingLayout* Renderer::GetComputeBindingLayout() { return s_Data->ComputeLayout; }
	nvrhi::IBindingLayout* Renderer::GetLineBindingLayout() { return s_Data->LineLayout; }
	nvrhi::IBindingLayout* Renderer::GetTextBindingLayout() { return s_Data->TextLayout; }

	nvrhi::ISampler* Renderer::GetLinearClampSampler() { return s_Data->LinearClamp; }
	nvrhi::ISampler* Renderer::GetLinearWrapSampler() { return s_Data->LinearWrap; }
	nvrhi::ISampler* Renderer::GetPointClampSampler() { return s_Data->PointClamp; }
	nvrhi::ISampler* Renderer::GetShadowCompareSampler() { return s_Data->ShadowCompare; }
	nvrhi::ISampler* Renderer::GetMaterialSampler(TextureFilter filter, TextureWrap wrapU, TextureWrap wrapV)
	{
		const auto index = [](auto value, size_t count) { return std::min(static_cast<size_t>(value), count - 1); };
		return s_Data->MaterialSamplers[index(filter, 2)][index(wrapU, 3)][index(wrapV, 3)];
	}

	const Ref<Texture2D>& Renderer::GetWhiteTexture() { return s_Data->WhiteTexture; }
	const Ref<Texture2D>& Renderer::GetBlackTexture() { return s_Data->BlackTexture; }
	const Ref<Texture2D>& Renderer::GetFlatNormalTexture() { return s_Data->FlatNormalTexture; }
	nvrhi::ITexture* Renderer::GetBlackCubeTexture() { return s_Data->BlackCube; }
	nvrhi::ITexture* Renderer::GetBlackTextureArray() { return s_Data->BlackArray; }
	nvrhi::ITexture* Renderer::GetBRDFLut() { return s_Data->BRDFLut; }

	Ref<Mesh> Renderer::GetPrimitiveMesh(PrimitiveMesh primitive)
	{
		auto it = s_Data->Primitives.find(primitive);
		if (it != s_Data->Primitives.end())
			return it->second;

		Ref<Mesh> mesh = MeshFactory::CreatePrimitive(primitive);
		s_Data->Primitives[primitive] = mesh;
		return mesh;
	}

	void Renderer::ExecuteImmediate(const std::function<void(nvrhi::ICommandList*)>& record, bool waitForCompletion)
	{
		nvrhi::IDevice* device = GetDevice();
		nvrhi::CommandListHandle commandList = device->createCommandList();
		commandList->open();
		record(commandList);
		commandList->close();
		device->executeCommandList(commandList);
		if (waitForCompletion)
			device->waitForIdle();
	}

	Image Renderer::ReadbackTexture(nvrhi::ITexture* texture)
	{
		Image image;
		if (!texture)
			return image;

		const nvrhi::TextureDesc& sourceDesc = texture->getDesc();
		nvrhi::TextureDesc stagingDesc;
		stagingDesc.width = sourceDesc.width;
		stagingDesc.height = sourceDesc.height;
		stagingDesc.format = sourceDesc.format;
		stagingDesc.debugName = "ReadbackStaging";

		nvrhi::IDevice* device = GetDevice();
		nvrhi::StagingTextureHandle staging = device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
		if (!staging)
			return image;

		ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			commandList->copyTexture(staging, nvrhi::TextureSlice(), texture, nvrhi::TextureSlice());
		}, true);

		size_t rowPitch = 0;
		const auto* mapped = static_cast<const uint8_t*>(device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		if (!mapped)
			return image;

		image.Width = sourceDesc.width;
		image.Height = sourceDesc.height;
		image.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4);

		const nvrhi::Format format = sourceDesc.format;
		for (uint32_t y = 0; y < image.Height; y++)
		{
			const uint8_t* row = mapped + rowPitch * y;
			uint8_t* destination = image.Pixels.data() + static_cast<size_t>(y) * image.Width * 4;
			switch (format)
			{
				case nvrhi::Format::RGBA8_UNORM:
				case nvrhi::Format::SRGBA8_UNORM:
					std::memcpy(destination, row, static_cast<size_t>(image.Width) * 4);
					break;
				case nvrhi::Format::BGRA8_UNORM:
				case nvrhi::Format::SBGRA8_UNORM:
					for (uint32_t x = 0; x < image.Width; x++)
					{
						destination[x * 4 + 0] = row[x * 4 + 2];
						destination[x * 4 + 1] = row[x * 4 + 1];
						destination[x * 4 + 2] = row[x * 4 + 0];
						destination[x * 4 + 3] = row[x * 4 + 3];
					}
					break;
				case nvrhi::Format::RGBA16_FLOAT:
				{
					const auto* halves = reinterpret_cast<const uint16_t*>(row);
					for (uint32_t x = 0; x < image.Width * 4; x++)
						destination[x] = static_cast<uint8_t>(std::lround(std::clamp(HalfToFloat(halves[x]), 0.0f, 1.0f) * 255.0f));
					break;
				}
				case nvrhi::Format::R32_UINT:
				{
					// Integer IDs are visualized: zero is black, any other value white.
					const auto* values = reinterpret_cast<const uint32_t*>(row);
					for (uint32_t x = 0; x < image.Width; x++)
					{
						const uint8_t v = values[x] != 0 ? 255 : 0;
						destination[x * 4 + 0] = v;
						destination[x * 4 + 1] = v;
						destination[x * 4 + 2] = v;
						destination[x * 4 + 3] = 255;
					}
					break;
				}
				default:
					BW_CORE_ERROR("ReadbackTexture: unsupported format {}", static_cast<int>(format));
					device->unmapStagingTexture(staging);
					return {};
			}
		}
		device->unmapStagingTexture(staging);
		return image;
	}

	uint32_t Renderer::ReadbackPixelUInt(nvrhi::ITexture* texture, uint32_t x, uint32_t y)
	{
		if (!texture)
			return 0;
		const nvrhi::TextureDesc& sourceDesc = texture->getDesc();
		if (x >= sourceDesc.width || y >= sourceDesc.height)
			return 0;

		nvrhi::TextureDesc stagingDesc;
		stagingDesc.width = 1;
		stagingDesc.height = 1;
		stagingDesc.format = sourceDesc.format;
		stagingDesc.debugName = "PickStaging";

		nvrhi::IDevice* device = GetDevice();
		nvrhi::StagingTextureHandle staging = device->createStagingTexture(stagingDesc, nvrhi::CpuAccessMode::Read);
		if (!staging)
			return 0;

		ExecuteImmediate([&](nvrhi::ICommandList* commandList)
		{
			nvrhi::TextureSlice sourceSlice;
			sourceSlice.x = x;
			sourceSlice.y = y;
			sourceSlice.width = 1;
			sourceSlice.height = 1;
			nvrhi::TextureSlice destinationSlice;
			destinationSlice.width = 1;
			destinationSlice.height = 1;
			commandList->copyTexture(staging, destinationSlice, texture, sourceSlice);
		}, true);

		size_t rowPitch = 0;
		const auto* mapped = static_cast<const uint32_t*>(device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch));
		const uint32_t value = mapped ? *mapped : 0;
		device->unmapStagingTexture(staging);
		return value;
	}

}
