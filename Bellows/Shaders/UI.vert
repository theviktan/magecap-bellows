#version 460
// Screen UI: rounded rectangles, images and distance-field text, positioned in output pixels.

layout(location = 0) in vec2 a_Position;	// Pixels, (0, 0) at the top-left
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in vec4 a_Color2;
layout(location = 4) in vec4 a_Shape;
layout(location = 5) in vec4 a_Params;
layout(location = 6) in uint a_EntityID;

layout(push_constant) uniform TextBlock
{
	mat4 ViewProjection;	// Pixels to clip space
	vec4 Params;			// x: the font's distance range in atlas pixels (text only)
} u_Text;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;
layout(location = 2) out vec4 v_Color2;
layout(location = 3) out vec4 v_Shape;
layout(location = 4) flat out vec4 v_Params;
layout(location = 5) flat out uint v_EntityID;

void main()
{
	v_TexCoord = a_TexCoord;
	v_Color = a_Color;
	v_Color2 = a_Color2;
	v_Shape = a_Shape;
	v_Params = a_Params;
	v_EntityID = a_EntityID;
	gl_Position = u_Text.ViewProjection * vec4(a_Position, 0.0, 1.0);
}
