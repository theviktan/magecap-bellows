#version 460
#extension GL_GOOGLE_include_directive : require
// Draws the environment behind all geometry.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform SkyBlock
{
	mat4 InverseViewProjection;
	vec4 CameraPosition;
	vec4 Params;			// x = intensity, y = rotation, z = blur (mip level), w = 1 if an environment exists
	vec4 FallbackZenith;
	vec4 FallbackHorizon;
} u_Sky;

layout(set = 0, binding = 1) uniform textureCube u_Environment;
layout(set = 0, binding = 2) uniform texture2D u_Depth;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main()
{
	float depth = textureLod(sampler2D(u_Depth, s_PointClamp), v_UV, 0.0).r;
	if (depth < 1.0)
		discard;

	vec4 worldFar = u_Sky.InverseViewProjection * vec4(UVToNDC(v_UV), 1.0, 1.0);
	vec3 direction = normalize(worldFar.xyz / worldFar.w - u_Sky.CameraPosition.xyz);

	vec3 color;
	if (u_Sky.Params.w > 0.5)
	{
		color = textureLod(samplerCube(u_Environment, s_LinearClamp), RotateY(direction, u_Sky.Params.y), u_Sky.Params.z).rgb;
		color *= u_Sky.Params.x;
	}
	else
	{
		float t = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
		color = mix(u_Sky.FallbackHorizon.rgb, u_Sky.FallbackZenith.rgb, smoothstep(0.45, 1.0, t));
	}
	o_Color = vec4(color, 1.0);
}
