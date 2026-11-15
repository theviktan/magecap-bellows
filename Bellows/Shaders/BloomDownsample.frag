#version 460
#extension GL_GOOGLE_include_directive : require
// Bloom downsample with the 13-tap filter from "Next Generation Post Processing in Call of Duty: AW".
// The first pass applies a Karis average to suppress fireflies.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform BloomBlock
{
	vec4 SourceTexelSize;	// xy = 1 / source size
	vec4 Params;			// x = 1 for the first downsample (Karis average), y = threshold, z = knee
} u_Bloom;

layout(set = 0, binding = 1) uniform texture2D u_Source;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

vec3 Tap(vec2 offset)
{
	return textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + offset * u_Bloom.SourceTexelSize.xy, 0.0).rgb;
}

float KarisWeight(vec3 color)
{
	return 1.0 / (1.0 + Luminance(color));
}

vec3 SoftThreshold(vec3 color)
{
	float threshold = u_Bloom.Params.y;
	float knee = max(u_Bloom.Params.z, 1e-4);
	float brightness = max(color.r, max(color.g, color.b));
	float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee);
	float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
	return color * contribution;
}

void main()
{
	vec3 a = Tap(vec2(-2.0, -2.0)), b = Tap(vec2(0.0, -2.0)), c = Tap(vec2(2.0, -2.0));
	vec3 d = Tap(vec2(-2.0, 0.0)), e = Tap(vec2(0.0, 0.0)), f = Tap(vec2(2.0, 0.0));
	vec3 g = Tap(vec2(-2.0, 2.0)), h = Tap(vec2(0.0, 2.0)), i = Tap(vec2(2.0, 2.0));
	vec3 j = Tap(vec2(-1.0, -1.0)), k = Tap(vec2(1.0, -1.0));
	vec3 l = Tap(vec2(-1.0, 1.0)), m = Tap(vec2(1.0, 1.0));

	vec3 result;
	if (u_Bloom.Params.x > 0.5)
	{
		vec3 groups[5];
		groups[0] = (a + b + d + e) * 0.25;
		groups[1] = (b + c + e + f) * 0.25;
		groups[2] = (d + e + g + h) * 0.25;
		groups[3] = (e + f + h + i) * 0.25;
		groups[4] = (j + k + l + m) * 0.25;
		float weights[5] = float[](0.125, 0.125, 0.125, 0.125, 0.5);
		result = vec3(0.0);
		float weightSum = 0.0;
		for (int n = 0; n < 5; n++)
		{
			float weight = weights[n] * KarisWeight(groups[n]);
			result += groups[n] * weight;
			weightSum += weight;
		}
		result = SoftThreshold(result / max(weightSum, 1e-5));
	}
	else
	{
		result = e * 0.125;
		result += (a + c + g + i) * 0.03125;
		result += (b + d + f + h) * 0.0625;
		result += (j + k + l + m) * 0.125;
	}
	o_Color = vec4(max(result, vec3(0.0)), 1.0);
}
