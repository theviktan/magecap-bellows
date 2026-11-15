// Cube map helpers for image-based lighting compute shaders.

#ifndef BELLOWS_CUBE_MAP_GLSL
#define BELLOWS_CUBE_MAP_GLSL

#include "Common.glsl"

// Direction through texel (x, y) of a cube face, following the Vulkan cube map convention.
vec3 CubeDirection(uvec3 id, float size)
{
	vec2 uv = (vec2(id.xy) + 0.5) / size * 2.0 - 1.0;
	vec3 direction;
	switch (id.z)
	{
		case 0u: direction = vec3(1.0, -uv.y, -uv.x); break;
		case 1u: direction = vec3(-1.0, -uv.y, uv.x); break;
		case 2u: direction = vec3(uv.x, 1.0, uv.y); break;
		case 3u: direction = vec3(uv.x, -1.0, -uv.y); break;
		case 4u: direction = vec3(uv.x, -uv.y, 1.0); break;
		default: direction = vec3(-uv.x, -uv.y, -1.0); break;
	}
	return normalize(direction);
}

vec2 DirectionToEquirect(vec3 direction)
{
	float phi = atan(direction.z, direction.x);
	float theta = acos(clamp(direction.y, -1.0, 1.0));
	return vec2(phi / (2.0 * PI) + 0.5, theta / PI);
}

float RadicalInverseVdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint count)
{
	return vec2(float(i) / float(count), RadicalInverseVdC(i));
}

vec3 ImportanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
	float alpha = roughness * roughness;
	float phi = 2.0 * PI * xi.x;
	float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);
	return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

#endif
