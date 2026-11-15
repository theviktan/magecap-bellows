#version 460
#extension GL_GOOGLE_include_directive : require
// HDR resolve: exposure, bloom composite, tonemapping, vignette and display encoding (sRGB).

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform TonemapBlock
{
	vec4 Params;	// x = exposure, y = bloom intensity, z = tonemapper (0 ACES, 1 AgX, 2 Reinhard, 3 none), w = vignette strength
	vec4 Params2;	// x = 1 if bloom is enabled
} u_Tonemap;

layout(set = 0, binding = 1) uniform texture2D u_HDR;
layout(set = 0, binding = 2) uniform texture2D u_Bloom;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

// ACES filmic curve fit by Stephen Hill (BakingLab), including the sRGB->ACEScg conversions.
vec3 TonemapACES(vec3 color)
{
	const mat3 inputMatrix = mat3(
		0.59719, 0.07600, 0.02840,
		0.35458, 0.90834, 0.13383,
		0.04823, 0.01566, 0.83777);
	const mat3 outputMatrix = mat3(
		1.60475, -0.10208, -0.00327,
		-0.53108, 1.10813, -0.07276,
		-0.07367, -0.00605, 1.07602);
	color = inputMatrix * color;
	vec3 a = color * (color + 0.0245786) - 0.000090537;
	vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
	color = outputMatrix * (a / b);
	return clamp(color, 0.0, 1.0);
}

// AgX base curve (Troy Sobotka) with the default "base" look, polynomial fit by Benjamin Wrensch.
vec3 AgXContrast(vec3 x)
{
	vec3 x2 = x * x;
	vec3 x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 TonemapAgX(vec3 color)
{
	const mat3 agxMatrix = mat3(
		0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992, 0.878468636469772, 0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104);
	const mat3 agxInverse = mat3(
		1.19687900512017, -0.0528968517574562, -0.0529716355144438,
		-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
		-0.0990297440797205, -0.0989611768448433, 1.15107367264116);
	const float minEv = -12.47393;
	const float maxEv = 4.026069;

	color = agxMatrix * max(color, vec3(1e-10));
	color = clamp(log2(color), minEv, maxEv);
	color = (color - minEv) / (maxEv - minEv);
	color = AgXContrast(color);
	color = agxInverse * color;
	// The AgX curve outputs display-encoded values; convert back to linear for uniform handling.
	return SRGBToLinear(clamp(color, 0.0, 1.0));
}

void main()
{
	vec3 hdr = textureLod(sampler2D(u_HDR, s_PointClamp), v_UV, 0.0).rgb;
	if (u_Tonemap.Params2.x > 0.5)
	{
		vec3 bloom = textureLod(sampler2D(u_Bloom, s_LinearClamp), v_UV, 0.0).rgb;
		hdr = mix(hdr, bloom, clamp(u_Tonemap.Params.y, 0.0, 1.0));
	}
	hdr *= u_Tonemap.Params.x;

	vec3 color;
	int tonemapper = int(u_Tonemap.Params.z);
	if (tonemapper == 0)
		color = TonemapACES(hdr);
	else if (tonemapper == 1)
		color = TonemapAgX(hdr);
	else if (tonemapper == 2)
		color = hdr / (1.0 + hdr);
	else
		color = clamp(hdr, 0.0, 1.0);

	// Subtle vignette
	vec2 centered = v_UV - 0.5;
	float vignette = 1.0 - dot(centered, centered) * u_Tonemap.Params.w;
	color *= clamp(vignette, 0.0, 1.0);

	vec3 encoded = LinearToSRGB(color);
	// Triangular dither removes banding in dark gradients.
	float noise = InterleavedGradientNoise(gl_FragCoord.xy) + InterleavedGradientNoise(gl_FragCoord.yx + 17.0) - 1.0;
	encoded += noise / 255.0;
	o_Color = vec4(clamp(encoded, 0.0, 1.0), 1.0);
}
