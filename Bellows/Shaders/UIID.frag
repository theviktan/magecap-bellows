#version 460
#extension GL_GOOGLE_include_directive : require
// Screen UI in the entity ID image, so clicking a panel or label in the editor selects it. Nearly
// transparent pixels are skipped, so an invisible rectangle never hides what is behind it.

#include "UI.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec4 v_Color2;
layout(location = 3) in vec4 v_Shape;
layout(location = 4) flat in vec4 v_Params;
layout(location = 5) flat in uint v_EntityID;

layout(location = 0) out uint o_EntityID;

void main()
{
	vec4 color = ShadeUI(v_TexCoord, v_Color, v_Color2, v_Shape, v_Params);
	if (color.a < (v_Params.w > 0.5 ? 0.5 : 0.05))
		discard;
	o_EntityID = v_EntityID;
}
