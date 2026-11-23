#version 460
#extension GL_GOOGLE_include_directive : require
// Mesh.vert for skinned meshes: each vertex follows up to four joints, blended by its weights. Shared
// by the depth prepass, the forward lighting pass and the transparent pass.

#include "SceneBindings.glsl"
#include "Skinning.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;
layout(location = 4) in uvec4 a_Joints;
layout(location = 5) in vec4 a_Weights;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_TexCoord;
layout(location = 4) out float v_ViewDepth;

// The prepass and lighting pass must produce bit-identical depth for EQUAL depth testing.
invariant gl_Position;

void main()
{
	mat4 skin = SkinMatrix(u_Object.JointOffset, a_Joints, a_Weights);
	vec4 worldPosition = skin * vec4(a_Position, 1.0);

	v_WorldPosition = worldPosition.xyz;
	v_Normal = normalize(SkinNormalMatrix(skin) * a_Normal);
	// A mirrored pose flips the bitangent the fragment shader rebuilds from the normal and tangent.
	v_Tangent = vec4(normalize(mat3(skin) * a_Tangent.xyz), determinant(mat3(skin)) < 0.0 ? -a_Tangent.w : a_Tangent.w);
	v_TexCoord = a_TexCoord;
	v_ViewDepth = -(u_Frame.View * worldPosition).z;

	gl_Position = u_Frame.ViewProjection * worldPosition;
}
