#version 460
#extension GL_GOOGLE_include_directive : require
// Text color: the glyph over its outline, alpha blended. Colors are sRGB, written after tonemapping.

#include "Text.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec4 v_OutlineColor;
layout(location = 3) in float v_OutlineWidth;
layout(location = 4) flat in uint v_EntityID;

layout(location = 0) out vec4 o_Color;

void main()
{
	vec2 coverage = TextCoverage(v_TexCoord, v_OutlineWidth);
	float outlineAlpha = coverage.y * v_OutlineColor.a;
	float alpha = coverage.x * v_Color.a + (1.0 - coverage.x) * outlineAlpha;
	if (alpha <= 0.0)
		discard;
	vec3 color = mix(v_OutlineColor.rgb * outlineAlpha, v_Color.rgb * v_Color.a, coverage.x) / max(alpha, 1e-5);
	o_Color = vec4(color, alpha);
}
