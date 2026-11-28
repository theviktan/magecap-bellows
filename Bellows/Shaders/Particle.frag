#version 460
// Particle color in linear HDR, premultiplied by its alpha: the additive pipeline adds it, the alpha
// pipeline lays it over what's behind (One, InvSrcAlpha).

layout(set = 0, binding = 0) uniform texture2D u_Texture;
layout(set = 0, binding = 1) uniform sampler s_Texture;

layout(push_constant) uniform ParticleBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: 1 = sample the texture; y: 1 = a strip (soft across V), 0 = a soft round dot
} u_Particle;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

void main()
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
	vec4 color = v_Color * shape;
	if (color.a <= 0.0)
		discard;
	o_Color = vec4(color.rgb * color.a, color.a);
}
