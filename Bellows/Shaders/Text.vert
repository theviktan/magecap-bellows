#version 460
// Text quads from a font's distance-field atlas (world text; screen text in the UI pass).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_OutlineColor;
layout(location = 4) in float a_OutlineWidth;
layout(location = 5) in uint a_EntityID;

layout(push_constant) uniform TextBlock
{
	mat4 ViewProjection;
	vec4 Params;	// x: distance range in atlas pixels
} u_Text;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;
layout(location = 2) out vec4 v_OutlineColor;
layout(location = 3) out float v_OutlineWidth;
layout(location = 4) flat out uint v_EntityID;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	v_OutlineColor = a_OutlineColor;
	v_OutlineWidth = a_OutlineWidth;
	v_EntityID = a_EntityID;
	gl_Position = u_Text.ViewProjection * vec4(a_Position, 1.0);
}
