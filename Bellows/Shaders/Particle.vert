#version 460
// Particle quads, strips and effect surfaces, already placed in the world on the CPU
// (SceneRenderer::SubmitParticles, SubmitStrip, SubmitEffectMesh).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;

layout(push_constant) uniform ParticleBlock
{
	mat4 ViewProjection;
	vec4 Params;	// See Particle.frag
	vec4 Params2;
	vec4 Params3;
	vec4 Params4;
} u_Particle;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	gl_Position = u_Particle.ViewProjection * vec4(a_Position, 1.0);
}
