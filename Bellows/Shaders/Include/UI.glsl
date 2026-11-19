// Screen UI shading, shared by UI.frag and UIID.frag. A vertex is either part of a rectangle or a glyph:
//   v_Color   fill color (rectangles) or text color (glyphs), sRGB with alpha
//   v_Color2  border color (rectangles) or outline color (glyphs)
//   v_Shape   rectangles: xy the pixel's offset from the rectangle's center, zw its half size, in pixels
//   v_Params  x corner radius, y border width (pixels); z the outline width (distance units); w 0 for a
//             rectangle, 1 for a glyph

#ifndef BELLOWS_UI_GLSL
#define BELLOWS_UI_GLSL

#include "Text.glsl"

// Signed distance to a rounded rectangle centered on the origin: negative inside.
float RoundedRectDistance(vec2 position, vec2 halfSize, float radius)
{
	radius = clamp(radius, 0.0, min(halfSize.x, halfSize.y));
	vec2 q = abs(position) - halfSize + radius;
	return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

// Straight (not premultiplied) color and alpha of the UI at this pixel.
vec4 ShadeUI(vec2 texCoord, vec4 color, vec4 color2, vec4 shape, vec4 params)
{
	if (params.w > 0.5)
	{
		vec2 coverage = TextCoverage(texCoord, params.z);
		float outlineAlpha = coverage.y * color2.a;
		float alpha = coverage.x * color.a + (1.0 - coverage.x) * outlineAlpha;
		vec3 result = mix(color2.rgb * outlineAlpha, color.rgb * color.a, coverage.x) / max(alpha, 1e-5);
		return vec4(result, alpha);
	}

	// One pixel of soft edge at the rectangle's outline and at the border's inner edge.
	float distance = RoundedRectDistance(shape.xy, shape.zw, params.x);
	float outer = clamp(0.5 - distance, 0.0, 1.0);
	float inner = params.y > 0.0 ? clamp(0.5 - (distance + params.y), 0.0, 1.0) : outer;
	vec4 fill = color * texture(sampler2D(u_Atlas, s_Atlas), texCoord);
	float fillAlpha = fill.a * inner;
	float borderAlpha = color2.a * max(outer - inner, 0.0);
	float alpha = fillAlpha + borderAlpha;
	vec3 result = (fill.rgb * fillAlpha + color2.rgb * borderAlpha) / max(alpha, 1e-5);
	return vec4(result, alpha);
}

#endif
