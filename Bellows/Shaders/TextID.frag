#version 460
#extension GL_GOOGLE_include_directive : require
// Text in the entity ID image, so clicking a letter (or its outline) selects the text in the editor.

#include "Text.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec4 v_OutlineColor;
layout(location = 3) in float v_OutlineWidth;
layout(location = 4) flat in uint v_EntityID;

layout(location = 0) out uint o_EntityID;

void main()
{
	vec2 coverage = TextCoverage(v_TexCoord, v_OutlineWidth);
	if (max(coverage.x, coverage.y) < 0.5)
		discard;
	o_EntityID = v_EntityID;
}
