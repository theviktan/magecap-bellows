#version 460
// Alpha-tested shadow casters. Opaque materials skip all work and only write depth.

struct MaterialData
{
	vec4 AlbedoColor;
	vec4 EmissiveColor;
	vec4 Params;
	vec4 UVTransform;
	uvec4 Flags;
};

layout(set = 1, binding = 0, std140) uniform MaterialBlock { MaterialData u_Material; };
layout(set = 1, binding = 1) uniform texture2D u_AlbedoMap;
layout(set = 1, binding = 6) uniform sampler s_Material;

layout(location = 0) in vec2 v_TexCoord;

void main()
{
	if (u_Material.Flags.x != 1u)
		return;

	vec2 uv = v_TexCoord * u_Material.UVTransform.xy + u_Material.UVTransform.zw;
	float alpha = u_Material.AlbedoColor.a;
	if ((u_Material.Flags.y & 1u) != 0u)
		alpha *= texture(sampler2D(u_AlbedoMap, s_Material), uv).a;
	if (alpha < u_Material.Params.w)
		discard;
}
