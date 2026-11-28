#version 460
#extension GL_GOOGLE_include_directive : require
// Copies a decal's image into one mip of one layer of the decal atlas, reading the image's own mip
// that matches that size, so the atlas's smaller mips stay smooth.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform CopyBlock
{
	vec4 u_Params;	// x = the atlas mip's size in pixels
};
layout(set = 0, binding = 1) uniform texture2D u_Source;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main()
{
	vec2 sourceSize = vec2(textureSize(sampler2D(u_Source, s_LinearClamp), 0));
	float detail = max(log2(max(sourceSize.x, sourceSize.y) / u_Params.x), 0.0);
	o_Color = textureLod(sampler2D(u_Source, s_LinearClamp), v_UV, detail);
}
