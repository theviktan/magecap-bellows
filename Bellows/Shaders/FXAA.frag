#version 460
#extension GL_GOOGLE_include_directive : require
// Fast approximate anti-aliasing (based on FXAA 3.11 quality preset, Timothy Lottes).

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform FXAABlock
{
	vec4 TexelSize;		// xy = 1 / size
	vec4 Params;		// x = enabled
} u_FXAA;

layout(set = 0, binding = 1) uniform texture2D u_Source;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

float Luma(vec3 color)
{
	return dot(color, vec3(0.299, 0.587, 0.114));
}

vec3 Sample(vec2 uv)
{
	return textureLod(sampler2D(u_Source, s_LinearClamp), uv, 0.0).rgb;
}

void main()
{
	vec3 center = Sample(v_UV);
	if (u_FXAA.Params.x < 0.5)
	{
		o_Color = vec4(center, 1.0);
		return;
	}

	vec2 texel = u_FXAA.TexelSize.xy;
	float lumaCenter = Luma(center);
	float lumaDown = Luma(Sample(v_UV + vec2(0.0, texel.y)));
	float lumaUp = Luma(Sample(v_UV - vec2(0.0, texel.y)));
	float lumaLeft = Luma(Sample(v_UV - vec2(texel.x, 0.0)));
	float lumaRight = Luma(Sample(v_UV + vec2(texel.x, 0.0)));

	float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
	float lumaRange = lumaMax - lumaMin;
	if (lumaRange < max(0.0312, lumaMax * 0.125))
	{
		o_Color = vec4(center, 1.0);
		return;
	}

	float lumaDownLeft = Luma(Sample(v_UV + vec2(-texel.x, texel.y)));
	float lumaUpRight = Luma(Sample(v_UV + vec2(texel.x, -texel.y)));
	float lumaUpLeft = Luma(Sample(v_UV - texel));
	float lumaDownRight = Luma(Sample(v_UV + texel));

	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;
	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;

	float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 + abs(-2.0 * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 + abs(-2.0 * lumaDown + lumaDownCorners);
	bool isHorizontal = edgeHorizontal >= edgeVertical;

	float luma1 = isHorizontal ? lumaUp : lumaLeft;
	float luma2 = isHorizontal ? lumaDown : lumaRight;
	float gradient1 = luma1 - lumaCenter;
	float gradient2 = luma2 - lumaCenter;
	bool is1Steepest = abs(gradient1) >= abs(gradient2);
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	float stepLength = isHorizontal ? texel.y : texel.x;
	float lumaLocalAverage;
	if (is1Steepest)
	{
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
	}
	else
	{
		lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
	}

	vec2 currentUV = v_UV;
	if (isHorizontal)
		currentUV.y += stepLength * 0.5;
	else
		currentUV.x += stepLength * 0.5;

	vec2 offset = isHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
	vec2 uv1 = currentUV - offset;
	vec2 uv2 = currentUV + offset;

	float lumaEnd1 = Luma(Sample(uv1)) - lumaLocalAverage;
	float lumaEnd2 = Luma(Sample(uv2)) - lumaLocalAverage;
	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;

	const float qualitySteps[10] = float[](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);
	for (int i = 0; i < 10 && !(reached1 && reached2); i++)
	{
		if (!reached1)
		{
			uv1 -= offset * qualitySteps[i];
			lumaEnd1 = Luma(Sample(uv1)) - lumaLocalAverage;
			reached1 = abs(lumaEnd1) >= gradientScaled;
		}
		if (!reached2)
		{
			uv2 += offset * qualitySteps[i];
			lumaEnd2 = Luma(Sample(uv2)) - lumaLocalAverage;
			reached2 = abs(lumaEnd2) >= gradientScaled;
		}
	}

	float distance1 = isHorizontal ? (v_UV.x - uv1.x) : (v_UV.y - uv1.y);
	float distance2 = isHorizontal ? (uv2.x - v_UV.x) : (uv2.y - v_UV.y);
	bool isDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);
	float edgeThickness = distance1 + distance2;
	float pixelOffset = -distanceFinal / max(edgeThickness, 1e-6) + 0.5;

	bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
	bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
	float finalOffset = correctVariation ? pixelOffset : 0.0;

	float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
	float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * 0.75;
	finalOffset = max(finalOffset, subPixelOffsetFinal);

	vec2 finalUV = v_UV;
	if (isHorizontal)
		finalUV.y += finalOffset * stepLength;
	else
		finalUV.x += finalOffset * stepLength;

	o_Color = vec4(Sample(finalUV), 1.0);
}
