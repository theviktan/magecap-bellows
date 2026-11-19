#version 460
#extension GL_GOOGLE_include_directive : require
// Screen UI color, alpha blended over the finished frame. Colors are sRGB, as the frame is.

#include "UI.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec4 v_Color2;
layout(location = 3) in vec4 v_Shape;
layout(location = 4) flat in vec4 v_Params;
layout(location = 5) flat in uint v_EntityID;

layout(location = 0) out vec4 o_Color;

void main()
{
	vec4 color = ShadeUI(v_TexCoord, v_Color, v_Color2, v_Shape, v_Params);
	if (color.a <= 0.0)
		discard;
	o_Color = color;
}
