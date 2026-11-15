#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Renderer/Texture.h"

#include <glm/glm.hpp>

#include <filesystem>

namespace Bellows {

	struct ProceduralSkySettings
	{
		glm::vec3 SunDirection = glm::normalize(glm::vec3(0.35f, 0.6f, 0.25f));	// Towards the sun
		glm::vec3 ZenithColor { 0.16f, 0.32f, 0.62f };
		glm::vec3 HorizonColor { 0.62f, 0.70f, 0.78f };
		glm::vec3 GroundColor { 0.13f, 0.12f, 0.11f };
		float SunAngularRadius = 0.02f;
		float SunIntensity = 40.0f;

		bool operator==(const ProceduralSkySettings&) const = default;
	};

	struct EnvironmentQuality
	{
		uint32_t CubeSize = 512;			// Radiance cube face size
		uint32_t IrradianceSize = 32;
		uint32_t PrefilteredSize = 256;
		uint32_t PrefilterSamples = 256;
	};

	// Image-based lighting data: radiance (skybox), diffuse irradiance and GGX-prefiltered specular cubes.
	class Environment
	{
	public:
		// Builds an environment from an equirectangular HDR image (.hdr), e.g. a Poly Haven HDRI.
		static Ref<Environment> CreateFromEquirectangular(const Ref<Texture2D>& equirect, const EnvironmentQuality& quality = {});
		static Ref<Environment> LoadFromFile(const std::filesystem::path& path, const EnvironmentQuality& quality = {});
		static Ref<Environment> CreateProcedural(const ProceduralSkySettings& settings, const EnvironmentQuality& quality = {});

		const Ref<TextureCube>& GetRadianceMap() const { return m_Radiance; }
		const Ref<TextureCube>& GetIrradianceMap() const { return m_Irradiance; }
		const Ref<TextureCube>& GetPrefilteredMap() const { return m_Prefiltered; }
	private:
		bool Build(const Ref<Texture2D>* equirect, const ProceduralSkySettings* sky, const EnvironmentQuality& quality);
	private:
		Ref<TextureCube> m_Radiance;
		Ref<TextureCube> m_Irradiance;
		Ref<TextureCube> m_Prefiltered;
	};

}
