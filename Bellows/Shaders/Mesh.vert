#version 460
#extension GL_GOOGLE_include_directive : require
// Shared vertex shader for the depth prepass, the forward lighting pass and the transparent pass.

#include "SceneBindings.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_TexCoord;
layout(location = 4) out float v_ViewDepth;

// The prepass and lighting pass must produce bit-identical depth for EQUAL depth testing.
invariant gl_Position;

void main()
{
	vec4 worldPosition = u_Object.Model * vec4(a_Position, 1.0);
	mat3 normalMatrix = mat3(u_Object.NormalMatrix[0].xyz, u_Object.NormalMatrix[1].xyz, u_Object.NormalMatrix[2].xyz);

	v_WorldPosition = worldPosition.xyz;
	v_Normal = normalize(normalMatrix * a_Normal);
	v_Tangent = vec4(normalize(mat3(u_Object.Model) * a_Tangent.xyz), a_Tangent.w);
	v_TexCoord = a_TexCoord;
	v_ViewDepth = -(u_Frame.View * worldPosition).z;

	gl_Position = u_Frame.ViewProjection * worldPosition;
}
