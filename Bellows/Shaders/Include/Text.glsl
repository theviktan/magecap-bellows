// Multi-channel signed distance field text: the shared part of Text.frag and TextID.frag.
// The atlas holds the multi-channel distance in RGB and the true distance in A, both 0.5 on the
// glyph's edge and rising inward, across the font's distance range.

#ifndef BELLOWS_TEXT_GLSL
#define BELLOWS_TEXT_GLSL

layout(set = 0, binding = 0) uniform texture2D u_Atlas;
layout(set = 0, binding = 1) uniform sampler s_Atlas;

layout(push_constant) uniform TextBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: distance range in atlas pixels
} u_Text;

float Median(vec3 value)
{
	return max(min(value.r, value.g), min(max(value.r, value.g), value.b));
}

// How many screen pixels one unit of distance spans here, so edges stay one pixel soft at any size.
float ScreenPixelRange(vec2 texCoord)
{
	vec2 unitRange = vec2(u_Text.Params.x) / vec2(textureSize(sampler2D(u_Atlas, s_Atlas), 0));
	vec2 screenTexSize = vec2(1.0) / fwidth(texCoord);
	return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

// x: how much of this pixel the glyph covers; y: how much the glyph grown by its outline covers
// (outlineWidth in distance units, 0-0.5; no outline gives 0).
vec2 TextCoverage(vec2 texCoord, float outlineWidth)
{
	vec4 distances = texture(sampler2D(u_Atlas, s_Atlas), texCoord);
	float pixelRange = ScreenPixelRange(texCoord);
	float fill = clamp(pixelRange * (Median(distances.rgb) - 0.5) + 0.5, 0.0, 1.0);
	float outline = outlineWidth > 0.0 ? max(clamp(pixelRange * (distances.a - 0.5 + outlineWidth) + 0.5, 0.0, 1.0), fill) : 0.0;
	return vec2(fill, outline);
}

#endif
