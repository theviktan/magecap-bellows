#version 460
// Debug and gizmo lines (colliders, light ranges, camera frustums).

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

layout(push_constant) uniform LineBlock
{
	mat4 ViewProjection;
} u_Line;

layout(location = 0) out vec4 v_Color;

void main()
{
	v_Color = a_Color;
	gl_Position = u_Line.ViewProjection * vec4(a_Position, 1.0);
}
