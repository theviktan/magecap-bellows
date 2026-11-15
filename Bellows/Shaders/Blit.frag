#version 460
#extension GL_GOOGLE_include_directive : require
// Copies a texture into the current render target.

#include "PostProcess.glsl"

layout(set = 0, binding = 1) uniform texture2D u_Source;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = textureLod(sampler2D(u_Source, s_LinearClamp), v_UV, 0.0);
}
