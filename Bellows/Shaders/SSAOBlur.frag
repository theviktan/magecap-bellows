#version 460
#extension GL_GOOGLE_include_directive : require
// Depth-aware separable blur for the SSAO term.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform BlurBlock
{
	mat4 InverseProjection;
	vec4 Direction;		// xy = texel step direction, z = depth sharpness
} u_Blur;

layout(set = 0, binding = 1) uniform texture2D u_Occlusion;
layout(set = 0, binding = 2) uniform texture2D u_Depth;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out float o_Occlusion;

float LinearDepth(vec2 uv)
{
	float depth = textureLod(sampler2D(u_Depth, s_PointClamp), uv, 0.0).r;
	return -ViewPositionFromDepth(uv, depth, u_Blur.InverseProjection).z;
}

void main()
{
	const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

	float centerDepth = LinearDepth(v_UV);
	float sharpness = u_Blur.Direction.z;

	float sum = textureLod(sampler2D(u_Occlusion, s_PointClamp), v_UV, 0.0).r * weights[0];
	float weightSum = weights[0];
	for (int i = 1; i < 5; i++)
	{
		for (int side = -1; side <= 1; side += 2)
		{
			vec2 uv = v_UV + u_Blur.Direction.xy * float(i * side);
			float sampleDepth = LinearDepth(uv);
			float depthWeight = exp(-abs(sampleDepth - centerDepth) * sharpness / max(centerDepth, 1e-3));
			float weight = weights[i] * depthWeight;
			sum += textureLod(sampler2D(u_Occlusion, s_PointClamp), uv, 0.0).r * weight;
			weightSum += weight;
		}
	}
	o_Occlusion = sum / max(weightSum, 1e-5);
}
