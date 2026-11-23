#pragma once

// C++ mirrors of the GLSL uniform blocks (std140). Field order and sizes must match
// Bellows/Shaders/Include/Common.glsl and SceneBindings.glsl exactly; static_asserts guard the layout.

#include <glm/glm.hpp>

#include <cstdint>

namespace Bellows::ShaderInterop {

	constexpr uint32_t MaxCascades = 4;
	constexpr uint32_t MaxPointLights = 64;
	constexpr uint32_t MaxSpotLights = 32;

	struct FrameData
	{
		glm::mat4 View;
		glm::mat4 Projection;
		glm::mat4 ViewProjection;
		glm::mat4 InverseView;
		glm::mat4 InverseProjection;
		glm::mat4 InverseViewProjection;
		glm::vec4 CameraPosition;
		glm::vec4 ViewportSize;
		glm::vec4 CameraParams;

		glm::vec4 DirectionalLightDirection;
		glm::vec4 DirectionalLightColor;
		glm::mat4 CascadeViewProjection[MaxCascades];
		glm::vec4 CascadeInfo[MaxCascades];
		glm::vec4 CascadeSplits;
		glm::vec4 ShadowParams;
		glm::vec4 ShadowParams2;

		glm::vec4 EnvironmentParams;
		glm::vec4 AmbientColor;
		glm::vec4 FeatureFlags;
	};

	struct PointLight
	{
		glm::vec4 PositionRange;
		glm::vec4 ColorIntensity;
	};

	struct SpotLight
	{
		glm::vec4 PositionRange;
		glm::vec4 DirectionCosOuter;
		glm::vec4 ColorIntensity;
		glm::vec4 Params;
	};

	struct LightData
	{
		PointLight PointLights[MaxPointLights];
		SpotLight SpotLights[MaxSpotLights];
	};

	struct ObjectData
	{
		glm::mat4 Model;
		glm::vec4 NormalMatrix[3];
		uint32_t EntityID;
		uint32_t ObjectFlags;
		uint32_t JointOffset;	// Skinned draws: the first of the draw's joint matrices
		uint32_t Padding1;
	};

	struct ShadowObjectData
	{
		glm::mat4 Model;
		glm::mat4 LightViewProjection;
	};

	// ShadowSkinned.vert's push constants. The joint matrices are already in world space, so there's no
	// model matrix; the padding keeps it the size of ShadowObjectData, so one binding set serves both.
	struct ShadowSkinnedData
	{
		glm::mat4 LightViewProjection;
		glm::uvec4 Skin;		// x = the first of the draw's joint matrices
		glm::vec4 Padding[3];
	};

	struct MaterialData
	{
		glm::vec4 AlbedoColor;
		glm::vec4 EmissiveColor;
		glm::vec4 Params;		// metallic, roughness, normal strength, alpha cutoff
		glm::vec4 UVTransform;	// tiling xy, offset zw
		glm::uvec4 Flags;		// alpha mode, texture bits
	};

	// Push constants of the text passes (Text.vert, Text.frag, TextID.frag).
	struct TextData
	{
		glm::mat4 ViewProjection;
		glm::vec4 Params;		// distance range in atlas pixels, unused, unused, unused
	};

	enum MaterialTextureBits : uint32_t
	{
		TextureAlbedo = 1u << 0,
		TextureNormal = 1u << 1,
		TextureMetallicRoughness = 1u << 2,
		TextureOcclusion = 1u << 3,
		TextureEmissive = 1u << 4,
		TextureRoughness = 1u << 5,
		TextureMetallic = 1u << 6,
	};

	static_assert(sizeof(FrameData) % 16 == 0, "FrameData must be 16-byte aligned for std140");
	static_assert(sizeof(FrameData) == 6 * 64 + 3 * 16 + 2 * 16 + 4 * 64 + 4 * 16 + 3 * 16 + 3 * 16, "FrameData layout mismatch");
	static_assert(sizeof(LightData) == MaxPointLights * 32 + MaxSpotLights * 64, "LightData layout mismatch");
	static_assert(sizeof(ObjectData) == 128, "ObjectData must fit the 128-byte push constant minimum");
	static_assert(sizeof(ShadowObjectData) == 128, "ShadowObjectData must fit the 128-byte push constant minimum");
	static_assert(sizeof(ShadowSkinnedData) == sizeof(ShadowObjectData), "ShadowSkinnedData shares the shadow pass's push constants");
	static_assert(sizeof(MaterialData) == 80, "MaterialData layout mismatch");
	static_assert(sizeof(TextData) == 80, "TextData layout mismatch");

}
