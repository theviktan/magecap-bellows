// Soft cascaded shadow maps for the directional light.
//
// Percentage-closer soft shadows (PCSS): a blocker search estimates the average occluder depth,
// the penumbra width follows from the light's angular size, and a rotated Poisson-disk PCF
// filters the shadow map with hardware depth comparison.

#ifndef BELLOWS_SHADOWS_GLSL
#define BELLOWS_SHADOWS_GLSL

#include "SceneBindings.glsl"

const vec2 c_PoissonDisk[16] = vec2[](
	vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
	vec2(-0.09418410, -0.92938870), vec2(0.34495938, 0.29387760),
	vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
	vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
	vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
	vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
	vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
	vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
);

int SelectCascade(float viewDepth, int cascadeCount)
{
	for (int i = 0; i < cascadeCount - 1; i++)
	{
		if (viewDepth < u_Frame.CascadeSplits[i])
			return i;
	}
	return cascadeCount - 1;
}

float SampleCascade(int cascade, vec3 worldPosition, vec3 normal, vec2 noiseRotation)
{
	vec4 info = u_Frame.CascadeInfo[cascade];
	float depthRange = max(info.x, 1e-3);
	float width = max(info.y, 1e-3);
	float texelSize = u_Frame.ShadowParams2.x;
	float texelWorld = width * texelSize;

	// Normal-offset bias scaled by the cascade's texel size.
	vec3 offsetPosition = worldPosition + normal * u_Frame.ShadowParams.y * texelWorld;
	vec4 lightClip = u_Frame.CascadeViewProjection[cascade] * vec4(offsetPosition, 1.0);
	vec3 lightNDC = lightClip.xyz / lightClip.w;
	vec2 uv = NDCToUV(lightNDC.xy);
	float receiverDepth = lightNDC.z - u_Frame.ShadowParams.x;

	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || receiverDepth >= 1.0)
		return 1.0;

	float lightSize = u_Frame.ShadowParams.z;
	mat2 rotation = mat2(noiseRotation.x, noiseRotation.y, -noiseRotation.y, noiseRotation.x);

	// 1) Blocker search.
	float searchRadius = clamp(lightSize * depthRange * 0.5 / width, texelSize * 2.0, 0.05);
	float blockerSum = 0.0;
	float blockerCount = 0.0;
	for (int i = 0; i < 16; i++)
	{
		vec2 sampleUV = uv + rotation * c_PoissonDisk[i] * searchRadius;
		float shadowDepth = textureLod(sampler2DArray(u_ShadowMap, s_PointClamp), vec3(sampleUV, float(cascade)), 0.0).r;
		if (shadowDepth < receiverDepth)
		{
			blockerSum += shadowDepth;
			blockerCount += 1.0;
		}
	}
	if (blockerCount < 1.0)
		return 1.0;

	// 2) Penumbra estimation from the receiver/blocker distance in world units.
	float averageBlocker = blockerSum / blockerCount;
	float penumbraWorld = max(receiverDepth - averageBlocker, 0.0) * depthRange * lightSize;
	float filterRadius = clamp(penumbraWorld / width, texelSize * 1.5, 0.03);

	// 3) Filtering with hardware comparison (each tap is itself a 2x2 bilinear PCF).
	int sampleCount = int(u_Frame.ShadowParams2.w);
	float lit = 0.0;
	for (int i = 0; i < sampleCount; i++)
	{
		vec2 sampleUV = uv + rotation * c_PoissonDisk[i] * filterRadius;
		lit += texture(sampler2DArrayShadow(u_ShadowMap, s_ShadowCompare), vec4(sampleUV, float(cascade), receiverDepth));
	}
	return lit / float(max(sampleCount, 1));
}

float ComputeDirectionalShadow(vec3 worldPosition, vec3 normal, float viewDepth, vec2 pixel)
{
	if (u_Frame.DirectionalLightColor.a < 0.5)
		return 1.0;

	float maxDistance = u_Frame.ShadowParams2.y;
	if (viewDepth > maxDistance)
		return 1.0;

	float angle = InterleavedGradientNoise(pixel) * 2.0 * PI;
	vec2 noiseRotation = vec2(cos(angle), sin(angle));

	int cascadeCount = int(u_Frame.ShadowParams.w);
	int cascade = SelectCascade(viewDepth, cascadeCount);
	float shadow = SampleCascade(cascade, worldPosition, normal, noiseRotation);

	// Blend into the next cascade near the split to hide seams.
	float splitFar = u_Frame.CascadeSplits[cascade];
	float splitNear = cascade == 0 ? 0.0 : u_Frame.CascadeSplits[cascade - 1];
	float blendBand = (splitFar - splitNear) * 0.1;
	if (cascade < cascadeCount - 1 && viewDepth > splitFar - blendBand)
	{
		float nextShadow = SampleCascade(cascade + 1, worldPosition, normal, noiseRotation);
		shadow = mix(shadow, nextShadow, smoothstep(splitFar - blendBand, splitFar, viewDepth));
	}

	// Fade shadows out towards the maximum shadow distance.
	float fadeRange = max(u_Frame.ShadowParams2.z, 1e-3);
	float fade = clamp((maxDistance - viewDepth) / fadeRange, 0.0, 1.0);
	return mix(1.0, shadow, fade);
}

#endif
