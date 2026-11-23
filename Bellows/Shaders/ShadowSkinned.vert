#version 460
#extension GL_GOOGLE_include_directive : require
// Shadow.vert for skinned meshes (one draw per cascade layer).

#include "Skinning.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;
layout(location = 4) in uvec4 a_Joints;
layout(location = 5) in vec4 a_Weights;

layout(push_constant) uniform ShadowSkinnedBlock
{
	mat4 LightViewProjection;
	uvec4 Skin;		// x = the first of the draw's joint matrices
	vec4 Padding[3];
} u_Shadow;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
	v_TexCoord = a_TexCoord;
	mat4 skin = SkinMatrix(u_Shadow.Skin.x, a_Joints, a_Weights);
	gl_Position = u_Shadow.LightViewProjection * (skin * vec4(a_Position, 1.0));
}
