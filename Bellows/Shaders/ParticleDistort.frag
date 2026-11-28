#version 460
#extension GL_GOOGLE_include_directive : require
// An effect surface's distortion: what's behind it, bent by waves that drift across it (heat haze, a
// shockwave's ripple, a portal's shimmer). It reads a copy of the scene made just before the particles
// and writes it back premultiplied by the surface's coverage, so its edges fade into the unbent scene.

layout(set = 0, binding = 0) uniform texture2D u_SceneCopy;
layout(set = 0, binding = 1) uniform sampler s_SceneCopy;

layout(push_constant) uniform ParticleBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: distortion strength (a fraction of the screen); y: wave phase (0 to 1, wrapping);
					// z: dissolve edge width
	vec4 Params2;	// xy: 1 / the viewport's size in pixels; z: edge fade across V; w: dissolve
	vec4 Params3;	// xy: waves across U (whole, so a ring wraps) and V
	vec4 Params4;	// xy: dissolve noise cells across U and V
} u_Particle;

#include "Include/EffectNoise.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

const float c_TwoPi = 6.28318530718;

void main()
{
	vec2 uv = v_TexCoord;
	float coverage = v_Color.a * EdgeCoverage(uv.y, u_Particle.Params2.z);
	float dissolve = u_Particle.Params2.w;
	// Gone where the surface itself is dissolved away (its glowing edge still bends what's behind).
	if (dissolve > 0.0 && DissolveNoise(uv, u_Particle.Params4.xy) < dissolve * (1.0 + u_Particle.Params.z) - u_Particle.Params.z)
		discard;
	if (coverage <= 0.0)
		discard;

	// Two waves crossing at different rates, each a whole number of times around and across, so they
	// meet themselves where a ring closes and when the phase wraps.
	float phase = u_Particle.Params.y * c_TwoPi;
	vec2 waves = u_Particle.Params3.xy;
	vec2 bend = vec2(
		sin(uv.x * waves.x * c_TwoPi + phase) * cos(uv.y * waves.y * c_TwoPi - phase),
		cos(uv.x * waves.x * c_TwoPi - 2.0 * phase) * sin(uv.y * waves.y * c_TwoPi + phase));

	vec2 screen = gl_FragCoord.xy * u_Particle.Params2.xy;
	vec2 bent = clamp(screen + bend * (u_Particle.Params.x * coverage), vec2(0.0), vec2(1.0));
	vec3 behind = texture(sampler2D(u_SceneCopy, s_SceneCopy), bent).rgb;
	o_Color = vec4(behind * coverage, coverage);
}
