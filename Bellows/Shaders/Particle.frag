#version 460
#extension GL_GOOGLE_include_directive : require
// Particle color in linear HDR, premultiplied by its alpha: the additive pipeline adds it, the alpha
// pipeline lays it over what's behind (One, InvSrcAlpha).

layout(set = 0, binding = 0) uniform texture2D u_Texture;
layout(set = 0, binding = 1) uniform sampler s_Texture;

layout(push_constant) uniform ParticleBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: 1 = sample the texture; y: 0 = a soft round dot, 1 = a strip (soft across V),
					// 2 = an effect surface; zw: effect texture offset
	vec4 Params2;	// Effect: xy texture tiling, z edge fade across V, w dissolve (0 whole to 1 gone)
	vec4 Params3;	// Effect: rgb dissolve edge color, w dissolve edge width
	vec4 Params4;	// Effect: xy noise cells across U (whole, wrapping) and V, z dissolve edge alpha
} u_Particle;

#include "Include/EffectNoise.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

vec4 EffectSurface()
{
	vec2 uv = v_TexCoord;
	vec4 color = v_Color;
	if (u_Particle.Params.x > 0.5)
		color *= texture(sampler2D(u_Texture, s_Texture), uv * u_Particle.Params2.xy + u_Particle.Params.zw);

	// Soft toward both edges across the surface.
	color.a *= EdgeCoverage(uv.y, u_Particle.Params2.z);

	// Dissolve: where the noise is under the threshold the surface is gone; just above it, a glowing
	// edge. The threshold runs past 1 by the edge's width so 1 removes the edge too.
	float dissolve = u_Particle.Params2.w;
	if (dissolve > 0.0)
	{
		float noise = DissolveNoise(uv, u_Particle.Params4.xy);
		float width = u_Particle.Params3.w;
		float threshold = dissolve * (1.0 + width);
		if (noise < threshold - width)
			return vec4(0.0);
		if (width > 0.0 && noise < threshold)
		{
			float glow = 1.0 - (noise - (threshold - width)) / width;
			vec4 edge = vec4(u_Particle.Params3.rgb, u_Particle.Params4.z * color.a);
			color = mix(color, edge, smoothstep(0.0, 1.0, glow));
		}
	}
	return color;
}

void main()
{
	vec4 color;
	if (u_Particle.Params.y > 1.5)
	{
		color = EffectSurface();
	}
	else
	{
		vec4 shape;
		if (u_Particle.Params.x > 0.5)
		{
			shape = texture(sampler2D(u_Texture, s_Texture), v_TexCoord);
		}
		else if (u_Particle.Params.y > 0.5)
		{
			// A strip: full along its middle, fading smoothly to nothing at both edges.
			float distanceFromMiddle = abs(v_TexCoord.y * 2.0 - 1.0);
			shape = vec4(1.0, 1.0, 1.0, 1.0 - smoothstep(0.0, 1.0, distanceFromMiddle));
		}
		else
		{
			// Full at the center, fading smoothly to nothing at the quad's inscribed circle.
			float distanceFromCenter = length(v_TexCoord * 2.0 - 1.0);
			float coverage = 1.0 - smoothstep(0.0, 1.0, distanceFromCenter);
			shape = vec4(1.0, 1.0, 1.0, coverage);
		}
		color = v_Color * shape;
	}
	if (color.a <= 0.0)
		discard;
	o_Color = vec4(color.rgb * color.a, color.a);
}
