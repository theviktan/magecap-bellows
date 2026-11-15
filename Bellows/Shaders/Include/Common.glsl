// Shared definitions for MageCap Bellows shaders.
// Every struct here mirrors a C++ struct in Bellows/Renderer/ShaderInterop.h (std140 layout).

#ifndef BELLOWS_COMMON_GLSL
#define BELLOWS_COMMON_GLSL

#define PI 3.14159265359
#define MAX_CASCADES 4
#define MAX_POINT_LIGHTS 64
#define MAX_SPOT_LIGHTS 32

struct FrameData
{
	mat4 View;
	mat4 Projection;
	mat4 ViewProjection;
	mat4 InverseView;
	mat4 InverseProjection;
	mat4 InverseViewProjection;
	vec4 CameraPosition;		// xyz = world position, w = time in seconds
	vec4 ViewportSize;			// xy = size, zw = 1 / size
	vec4 CameraParams;			// x = near, y = far, z = exposure, w = unused

	vec4 DirectionalLightDirection;	// xyz = direction the light travels (world), w = intensity
	vec4 DirectionalLightColor;		// rgb = color, a = 1 if shadows are enabled
	mat4 CascadeViewProjection[MAX_CASCADES];
	vec4 CascadeInfo[MAX_CASCADES];	// x = depth range (world units), y = width (world units)
	vec4 CascadeSplits;				// view-space far distance of each cascade
	vec4 ShadowParams;				// x = depth bias, y = normal bias, z = light angular size (softness), w = cascade count
	vec4 ShadowParams2;				// x = shadow map texel size, y = max shadow distance, z = fade range, w = PCF sample count

	vec4 EnvironmentParams;		// x = intensity, y = rotation (radians), z = prefiltered mip count, w = 1 if an environment exists
	vec4 AmbientColor;			// Fallback ambient when no environment is present
	vec4 FeatureFlags;			// x = SSAO enabled, y = SSAO strength, z = point light count, w = spot light count
};

struct PointLight
{
	vec4 PositionRange;		// xyz = position, w = range
	vec4 ColorIntensity;	// rgb = color, a = intensity
};

struct SpotLight
{
	vec4 PositionRange;		// xyz = position, w = range
	vec4 DirectionCosOuter;	// xyz = direction, w = cos(outer angle)
	vec4 ColorIntensity;	// rgb = color, a = intensity
	vec4 Params;			// x = cos(inner angle)
};

vec3 LinearToSRGB(vec3 color)
{
	vec3 low = color * 12.92;
	vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3(0.0031308), color));
}

vec3 SRGBToLinear(vec3 color)
{
	vec3 low = color / 12.92;
	vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
	return mix(low, high, step(vec3(0.04045), color));
}

float Luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Screen UV (top-left origin) <-> clip-space XY (Y up).
vec2 UVToNDC(vec2 uv)
{
	return vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

vec2 NDCToUV(vec2 ndc)
{
	return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

// Reconstructs a view-space position from a depth-buffer value and screen UV.
vec3 ViewPositionFromDepth(vec2 uv, float depth, mat4 inverseProjection)
{
	vec4 clip = vec4(UVToNDC(uv), depth, 1.0);
	vec4 view = inverseProjection * clip;
	return view.xyz / view.w;
}

// Rotates a direction around the Y axis (environment rotation).
vec3 RotateY(vec3 direction, float angle)
{
	float s = sin(angle);
	float c = cos(angle);
	return vec3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
}

// Interleaved gradient noise (Jimenez 2014): cheap per-pixel rotation without a noise texture.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif
