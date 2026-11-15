#version 460
#extension GL_GOOGLE_include_directive : require
// Depth prepass: writes view-space normals (for SSAO) and entity IDs (for editor picking).

#include "SceneBindings.glsl"

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord;
layout(location = 4) in float v_ViewDepth;

layout(location = 0) out vec4 o_Normal;
layout(location = 1) out uint o_EntityID;

void main()
{
	vec2 uv = MaterialUV(v_TexCoord);
	if (u_Material.Flags.x == 1u && SampleAlbedo(uv).a < u_Material.Params.w)
		discard;

	vec3 N = normalize(v_Normal);
	if (!gl_FrontFacing)
		N = -N;
	vec3 viewNormal = normalize(mat3(u_Frame.View) * N);

	o_Normal = vec4(viewNormal * 0.5 + 0.5, 1.0);
	o_EntityID = u_Object.EntityID;
}
