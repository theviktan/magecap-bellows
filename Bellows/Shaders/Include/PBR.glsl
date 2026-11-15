// Physically based shading: Cook-Torrance GGX specular + Lambert diffuse (metallic/roughness workflow).

#ifndef BELLOWS_PBR_GLSL
#define BELLOWS_PBR_GLSL

#include "Common.glsl"

float DistributionGGX(float NdotH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
	return alpha2 / max(PI * denominator * denominator, 1e-7);
}

// Height-correlated Smith visibility term (already divided by 4 NdotL NdotV).
float VisibilitySmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2);
	float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2);
	return 0.5 / max(ggxV + ggxL, 1e-7);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Radiance reflected towards the viewer from a single light.
vec3 EvaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness, vec3 F0)
{
	vec3 H = normalize(V + L);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 1e-4);
	float NdotH = max(dot(N, H), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float D = DistributionGGX(NdotH, roughness);
	float Vis = VisibilitySmithGGXCorrelated(NdotV, NdotL, roughness);
	vec3 F = FresnelSchlick(HdotV, F0);

	vec3 specular = D * Vis * F;
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	vec3 diffuse = kD * albedo / PI;

	return (diffuse + specular) * radiance * NdotL;
}

// Smooth inverse-square falloff that reaches zero at 'range' (Karis 2013).
float DistanceAttenuation(float distanceSquared, float range)
{
	float factor = distanceSquared / max(range * range, 1e-4);
	float smoothFactor = clamp(1.0 - factor * factor, 0.0, 1.0);
	return (smoothFactor * smoothFactor) / max(distanceSquared, 1e-4);
}

#endif
