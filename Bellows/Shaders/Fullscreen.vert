#version 460
// Fullscreen triangle. UV (0,0) is the top-left of the render target.

layout(location = 0) out vec2 v_UV;

void main()
{
	// Clip space is Y-up (NVRHI flips the Vulkan viewport), so UV.y runs opposite to clip Y.
	v_UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(v_UV.x * 2.0 - 1.0, 1.0 - v_UV.y * 2.0, 0.0, 1.0);
}
