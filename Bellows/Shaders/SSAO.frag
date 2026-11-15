#version 460
#extension GL_GOOGLE_include_directive : require
// Screen-space ambient occlusion: normal-oriented hemisphere sampling with range checks.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform SSAOBlock
{
	mat4 Projection;
	mat4 InverseProjection;
	vec4 Params;		// x = radius (world units), y = bias, z = power, w = sample count
	vec4 TargetSize;	// xy = size, zw = 1 / size
} u_SSAO;

layout(set = 0, binding = 1) uniform texture2D u_Depth;
layout(set = 0, binding = 2) uniform texture2D u_Normals;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out float o_Occlusion;

void main()
{
	float depth = textureLod(sampler2D(u_Depth, s_PointClamp), v_UV, 0.0).r;
	if (depth >= 1.0)
	{
		o_Occlusion = 1.0;
		return;
	}

	vec3 P = ViewPositionFromDepth(v_UV, depth, u_SSAO.InverseProjection);
	vec3 N = normalize(textureLod(sampler2D(u_Normals, s_PointClamp), v_UV, 0.0).xyz * 2.0 - 1.0);

	float radius = u_SSAO.Params.x;
	float bias = u_SSAO.Params.y;
	int sampleCount = int(u_SSAO.Params.w);

	// Random rotation around the normal per pixel.
	float angle = InterleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
	vec3 randomVector = vec3(cos(angle), sin(angle), 0.0);
	vec3 T = normalize(randomVector - N * dot(randomVector, N));
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);

	float occlusion = 0.0;
	for (int i = 0; i < sampleCount; i++)
	{
		// Fibonacci hemisphere distribution, denser near the origin.
		float t = (float(i) + 0.5) / float(sampleCount);
		float phi = float(i) * 2.399963;
		float cosTheta = sqrt(1.0 - t);
		float sinTheta = sqrt(t);
		vec3 sampleDirection = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
		float scale = mix(0.1, 1.0, t * t);
		vec3 samplePosition = P + TBN * sampleDirection * radius * scale;

		vec4 offset = u_SSAO.Projection * vec4(samplePosition, 1.0);
		offset.xy /= offset.w;
		vec2 sampleUV = NDCToUV(offset.xy);
		if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
			continue;

		float sceneDepth = textureLod(sampler2D(u_Depth, s_PointClamp), sampleUV, 0.0).r;
		float sceneZ = ViewPositionFromDepth(sampleUV, sceneDepth, u_SSAO.InverseProjection).z;

		float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(P.z - sceneZ), 1e-4));
		occlusion += (sceneZ >= samplePosition.z + bias ? 1.0 : 0.0) * rangeCheck;
	}

	float ao = 1.0 - occlusion / float(max(sampleCount, 1));
	o_Occlusion = pow(clamp(ao, 0.0, 1.0), u_SSAO.Params.z);
}
