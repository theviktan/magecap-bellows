#version 460
// Shadow map depth pass (one draw per cascade layer).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;

layout(push_constant) uniform ShadowBlock
{
	mat4 Model;
	mat4 LightViewProjection;
} u_Shadow;

layout(location = 0) out vec2 v_TexCoord;

void main()
{
	v_TexCoord = a_TexCoord;
	gl_Position = u_Shadow.LightViewProjection * (u_Shadow.Model * vec4(a_Position, 1.0));
}
