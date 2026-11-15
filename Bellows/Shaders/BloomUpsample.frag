#version 460
#extension GL_GOOGLE_include_directive : require
// Bloom upsample with a 3x3 tent filter; the result is additively blended into the next larger mip.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform BloomBlock
{
	vec4 SourceTexelSize;	// xy = 1 / source size
	vec4 Params;			// x = filter radius (in source texels)
} u_Bloom;

layout(set = 0, binding = 1) uniform texture2D u_Source;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main()
{
	vec2 r = u_Bloom.SourceTexelSize.xy * u_Bloom.Params.x;
	vec3 sum = vec3(0.0);
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(-r.x, -r.y), 0.0).rgb * 1.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(0.0, -r.y), 0.0).rgb * 2.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(r.x, -r.y), 0.0).rgb * 1.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(-r.x, 0.0), 0.0).rgb * 2.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV, 0.0).rgb * 4.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(r.x, 0.0), 0.0).rgb * 2.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(-r.x, r.y), 0.0).rgb * 1.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(0.0, r.y), 0.0).rgb * 2.0;
	sum += textureLod(sampler2D(u_Source, s_LinearClamp), v_UV + vec2(r.x, r.y), 0.0).rgb * 1.0;
	o_Color = vec4(sum / 16.0, 1.0);
}
