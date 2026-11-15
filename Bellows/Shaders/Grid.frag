#version 460
#extension GL_GOOGLE_include_directive : require
// Editor infinite ground grid on the XZ plane. Writes depth so geometry occludes it correctly.

#include "PostProcess.glsl"

layout(set = 0, binding = 0, std140) uniform GridBlock
{
	mat4 ViewProjection;
	mat4 InverseViewProjection;
	vec4 CameraPosition;
	vec4 Params;		// x = cell size, y = fade distance, z = line opacity
	vec4 MinorColor;
	vec4 MajorColor;
} u_Grid;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

float GridLines(vec2 coordinate, float cellSize, float lineWidth)
{
	vec2 scaled = coordinate / cellSize;
	vec2 derivative = fwidth(scaled);
	vec2 grid = abs(fract(scaled - 0.5) - 0.5) / max(derivative * lineWidth, vec2(1e-6));
	return 1.0 - min(min(grid.x, grid.y), 1.0);
}

void main()
{
	vec2 ndc = UVToNDC(v_UV);
	vec4 nearPoint = u_Grid.InverseViewProjection * vec4(ndc, 0.0, 1.0);
	vec4 farPoint = u_Grid.InverseViewProjection * vec4(ndc, 1.0, 1.0);
	nearPoint.xyz /= nearPoint.w;
	farPoint.xyz /= farPoint.w;

	vec3 rayDirection = farPoint.xyz - nearPoint.xyz;
	if (abs(rayDirection.y) < 1e-6)
		discard;
	float t = -nearPoint.y / rayDirection.y;
	if (t <= 0.0 || t >= 1.0)
		discard;

	vec3 worldPosition = nearPoint.xyz + rayDirection * t;
	// Depth is taken slightly towards the camera so ground geometry at y = 0 does not z-fight
	// with the grid (the offset scales with distance, matching depth precision).
	vec3 depthPosition = mix(worldPosition, u_Grid.CameraPosition.xyz, 0.0015);
	vec4 clip = u_Grid.ViewProjection * vec4(depthPosition, 1.0);
	gl_FragDepth = clip.z / clip.w;

	float cellSize = u_Grid.Params.x;
	float minor = GridLines(worldPosition.xz, cellSize, 1.0);
	float major = GridLines(worldPosition.xz, cellSize * 10.0, 1.2);

	vec4 color = mix(u_Grid.MinorColor * minor, u_Grid.MajorColor, major);

	// Axis lines: X in red, Z in blue.
	vec2 axisDerivative = fwidth(worldPosition.xz);
	float xAxis = 1.0 - min(abs(worldPosition.z) / max(axisDerivative.y * 1.5, 1e-6), 1.0);
	float zAxis = 1.0 - min(abs(worldPosition.x) / max(axisDerivative.x * 1.5, 1e-6), 1.0);
	color = mix(color, vec4(0.86, 0.29, 0.29, 0.9), xAxis);
	color = mix(color, vec4(0.29, 0.47, 0.90, 0.9), zAxis);

	float distanceToCamera = length(worldPosition.xz - u_Grid.CameraPosition.xz);
	float fade = 1.0 - smoothstep(u_Grid.Params.y * 0.3, u_Grid.Params.y, distanceToCamera);
	// Fade at grazing angles to avoid moire.
	float viewAngle = abs(normalize(rayDirection).y);
	fade *= smoothstep(0.0, 0.15, viewAngle);

	color.a *= fade * u_Grid.Params.z;
	if (color.a < 0.005)
		discard;
	o_Color = color;
}
