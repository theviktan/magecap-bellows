#version 460
// Particle quads and strips, already turned toward the camera on the CPU (SceneRenderer::SubmitParticles, SubmitStrip).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;

layout(push_constant) uniform ParticleBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: 1 = sample the texture; y: 1 = a strip (soft across V), 0 = a soft round dot
} u_Particle;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	gl_Position = u_Particle.ViewProjection * vec4(a_Position, 1.0);
}
