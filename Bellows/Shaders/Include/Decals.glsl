// Decals: images projected onto opaque surfaces, blended into their albedo before lighting so they
// are lit and shadowed like the surface under them. Each decal is a box; it projects down its -Y
// (from its Up), with the image's top toward its -Z.

#ifndef BELLOWS_DECALS_GLSL
#define BELLOWS_DECALS_GLSL

#include "SceneBindings.glsl"

// 'normal' is the surface's geometric normal; 'dpdx'/'dpdy' are the world position's screen
// derivatives, taken by the caller in uniform control flow so the image's mip can be chosen here.
vec3 ApplyDecals(vec3 albedo, vec3 worldPosition, vec3 normal, vec3 dpdx, vec3 dpdy)
{
	for (uint i = 0u; i < u_DecalCount.x; i++)
	{
		vec3 p = (u_Decals[i].WorldToDecal * vec4(worldPosition, 1.0)).xyz;
		if (any(greaterThan(abs(p), vec3(0.5))))
			continue;

		// Fade out on surfaces turned away from the decal (past its max angle) and toward the ends
		// of its box, so it never ends in a hard line or smears down a wall.
		float cosMax = u_Decals[i].Up.w;
		float facing = dot(normal, u_Decals[i].Up.xyz);
		float weight = smoothstep(cosMax, min(cosMax + 0.15, 1.0), facing) * (1.0 - smoothstep(0.4, 0.5, abs(p.y)));
		if (weight <= 0.0)
			continue;

		vec4 color = u_Decals[i].Color;
		float layer = u_Decals[i].Params.x;
		if (layer >= 0.0)
		{
			mat3 toDecal = mat3(u_Decals[i].WorldToDecal);
			vec2 uv = p.xz + 0.5;
			color *= textureGrad(sampler2DArray(u_DecalAtlas, s_LinearClamp), vec3(uv, layer), (toDecal * dpdx).xz, (toDecal * dpdy).xz);
		}
		albedo = mix(albedo, color.rgb, clamp(color.a * weight, 0.0, 1.0));
	}
	return albedo;
}

#endif
