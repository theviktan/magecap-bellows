#version 460
#extension GL_GOOGLE_include_directive : require
// Selection outline built from the entity ID buffer.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform OutlineBlock
{
	vec4 Color;
	vec4 Params;		// x = thickness in pixels, y = selected ID count
	uvec4 SelectedIDs[8];
} u_Outline;

layout(set = 0, binding = 1) uniform utexture2D u_EntityIDs;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

bool IsSelected(uint id)
{
	if (id == 0u)
		return false;
	int count = int(u_Outline.Params.y);
	for (int i = 0; i < count; i++)
	{
		if (u_Outline.SelectedIDs[i / 4][i % 4] == id)
			return true;
	}
	return false;
}

void main()
{
	ivec2 size = textureSize(usampler2D(u_EntityIDs, s_PointClamp), 0);
	ivec2 pixel = ivec2(v_UV * vec2(size));
	bool centerSelected = IsSelected(texelFetch(usampler2D(u_EntityIDs, s_PointClamp), pixel, 0).r);

	int thickness = int(u_Outline.Params.x);
	bool edge = false;
	for (int y = -thickness; y <= thickness && !edge; y++)
	{
		for (int x = -thickness; x <= thickness; x++)
		{
			if (x * x + y * y > thickness * thickness)
				continue;
			ivec2 neighbor = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
			bool neighborSelected = IsSelected(texelFetch(usampler2D(u_EntityIDs, s_PointClamp), neighbor, 0).r);
			if (neighborSelected != centerSelected)
			{
				edge = true;
				break;
			}
		}
	}

	if (!edge || centerSelected)
		discard;
	o_Color = u_Outline.Color;
}
