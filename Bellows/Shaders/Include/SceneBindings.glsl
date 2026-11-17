// Descriptor layout shared by every mesh pass (set 0 = pass data, set 1 = material).

#ifndef BELLOWS_SCENE_BINDINGS_GLSL
#define BELLOWS_SCENE_BINDINGS_GLSL

#include "Common.glsl"

layout(set = 0, binding = 0, std140) uniform FrameBlock { FrameData u_Frame; };
layout(set = 0, binding = 1, std140) uniform LightBlock
{
	PointLight u_PointLights[MAX_POINT_LIGHTS];
	SpotLight u_SpotLights[MAX_SPOT_LIGHTS];
};
layout(set = 0, binding = 2) uniform texture2DArray u_ShadowMap;
layout(set = 0, binding = 3) uniform texture2D u_SSAO;
layout(set = 0, binding = 4) uniform textureCube u_IrradianceMap;
layout(set = 0, binding = 5) uniform textureCube u_PrefilteredMap;
layout(set = 0, binding = 6) uniform texture2D u_BRDFLut;
layout(set = 0, binding = 7) uniform sampler s_LinearClamp;
layout(set = 0, binding = 8) uniform samplerShadow s_ShadowCompare;
layout(set = 0, binding = 9) uniform sampler s_PointClamp;

layout(push_constant) uniform ObjectBlock
{
	mat4 Model;
	vec4 NormalMatrix[3];
	uint EntityID;
	uint ObjectFlags;
	uint Padding0;
	uint Padding1;
} u_Object;

struct MaterialData
{
	vec4 AlbedoColor;
	vec4 EmissiveColor;		// rgb = color * intensity
	vec4 Params;			// x = metallic, y = roughness, z = normal strength, w = alpha cutoff
	vec4 UVTransform;		// xy = tiling, zw = offset
	uvec4 Flags;			// x = alpha mode (0 opaque, 1 mask, 2 blend), y = texture presence bits
};

#define TEXTURE_ALBEDO		1u
#define TEXTURE_NORMAL		2u
#define TEXTURE_METAL_ROUGH	4u
#define TEXTURE_OCCLUSION	8u
#define TEXTURE_EMISSIVE	16u
#define TEXTURE_ROUGHNESS	32u
#define TEXTURE_METALLIC	64u

layout(set = 1, binding = 0, std140) uniform MaterialBlock { MaterialData u_Material; };
layout(set = 1, binding = 1) uniform texture2D u_AlbedoMap;
layout(set = 1, binding = 2) uniform texture2D u_NormalMap;
layout(set = 1, binding = 3) uniform texture2D u_MetallicRoughnessMap;
layout(set = 1, binding = 4) uniform texture2D u_OcclusionMap;
layout(set = 1, binding = 5) uniform texture2D u_EmissiveMap;
layout(set = 1, binding = 6) uniform sampler s_Material;
layout(set = 1, binding = 7) uniform texture2D u_RoughnessMap;	// Grayscale (red channel)
layout(set = 1, binding = 8) uniform texture2D u_MetallicMap;	// Grayscale (red channel)

vec2 MaterialUV(vec2 uv)
{
	return uv * u_Material.UVTransform.xy + u_Material.UVTransform.zw;
}

vec4 SampleAlbedo(vec2 uv)
{
	vec4 albedo = u_Material.AlbedoColor;
	if ((u_Material.Flags.y & TEXTURE_ALBEDO) != 0u)
		albedo *= texture(sampler2D(u_AlbedoMap, s_Material), uv);
	return albedo;
}

#endif
