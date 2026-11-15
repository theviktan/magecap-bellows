#version 460
#extension GL_GOOGLE_include_directive : require
// Forward PBR lighting: directional light with soft shadows, point and spot lights,
// image-based lighting, SSAO and emissive. Writes linear HDR color.

#include "SceneBindings.glsl"
#include "PBR.glsl"
#include "Shadows.glsl"

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_TexCoord;
layout(location = 4) in float v_ViewDepth;

layout(location = 0) out vec4 o_Color;

vec3 ComputeNormal(vec2 uv)
{
	vec3 N = normalize(v_Normal);
	if (!gl_FrontFacing)
		N = -N;

	if ((u_Material.Flags.y & TEXTURE_NORMAL) == 0u)
		return N;

	vec3 tangentNormal = texture(sampler2D(u_NormalMap, s_Material), uv).xyz * 2.0 - 1.0;
	tangentNormal.xy *= u_Material.Params.z;

	vec3 T;
	vec3 B;
	if (abs(v_Tangent.w) > 0.5)
	{
		T = normalize(v_Tangent.xyz - N * dot(N, v_Tangent.xyz));
		B = cross(N, T) * v_Tangent.w;
	}
	else
	{
		// No tangents in the mesh: derive a frame from screen-space derivatives.
		vec3 dp1 = dFdx(v_WorldPosition);
		vec3 dp2 = dFdy(v_WorldPosition);
		vec2 duv1 = dFdx(uv);
		vec2 duv2 = dFdy(uv);
		vec3 dp2perp = cross(dp2, N);
		vec3 dp1perp = cross(N, dp1);
		T = dp2perp * duv1.x + dp1perp * duv2.x;
		B = dp2perp * duv1.y + dp1perp * duv2.y;
		float invMax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
		T *= invMax;
		B *= invMax;
	}
	return normalize(mat3(T, B, N) * tangentNormal);
}

void main()
{
	vec2 uv = MaterialUV(v_TexCoord);
	vec4 albedoSample = SampleAlbedo(uv);
	uint alphaMode = u_Material.Flags.x;
	if (alphaMode == 1u && albedoSample.a < u_Material.Params.w)
		discard;

	vec3 albedo = albedoSample.rgb;
	float metallic = u_Material.Params.x;
	float roughness = u_Material.Params.y;
	if ((u_Material.Flags.y & TEXTURE_METAL_ROUGH) != 0u)
	{
		vec4 metalRough = texture(sampler2D(u_MetallicRoughnessMap, s_Material), uv);
		roughness *= metalRough.g;
		metallic *= metalRough.b;
	}
	roughness = clamp(roughness, 0.045, 1.0);
	metallic = clamp(metallic, 0.0, 1.0);

	float occlusion = 1.0;
	if ((u_Material.Flags.y & TEXTURE_OCCLUSION) != 0u)
		occlusion = texture(sampler2D(u_OcclusionMap, s_Material), uv).r;

	vec2 screenUV = gl_FragCoord.xy * u_Frame.ViewportSize.zw;
	if (u_Frame.FeatureFlags.x > 0.5 && alphaMode != 2u)
	{
		float ssao = textureLod(sampler2D(u_SSAO, s_LinearClamp), screenUV, 0.0).r;
		occlusion *= mix(1.0, ssao, u_Frame.FeatureFlags.y);
	}

	vec3 N = ComputeNormal(uv);
	vec3 geometricNormal = normalize(v_Normal) * (gl_FrontFacing ? 1.0 : -1.0);
	vec3 V = normalize(u_Frame.CameraPosition.xyz - v_WorldPosition);
	float NdotV = max(dot(N, V), 1e-4);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	vec3 color = vec3(0.0);

	// Directional light
	float sunIntensity = u_Frame.DirectionalLightDirection.w;
	if (sunIntensity > 0.0)
	{
		vec3 L = normalize(-u_Frame.DirectionalLightDirection.xyz);
		float shadow = ComputeDirectionalShadow(v_WorldPosition, geometricNormal, v_ViewDepth, gl_FragCoord.xy);
		vec3 radiance = u_Frame.DirectionalLightColor.rgb * sunIntensity * shadow;
		color += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
	}

	// Point lights
	int pointCount = int(u_Frame.FeatureFlags.z);
	for (int i = 0; i < pointCount; i++)
	{
		vec3 toLight = u_PointLights[i].PositionRange.xyz - v_WorldPosition;
		float distanceSquared = dot(toLight, toLight);
		float range = u_PointLights[i].PositionRange.w;
		if (distanceSquared > range * range)
			continue;
		vec3 L = toLight * inversesqrt(max(distanceSquared, 1e-8));
		vec3 radiance = u_PointLights[i].ColorIntensity.rgb * u_PointLights[i].ColorIntensity.a * DistanceAttenuation(distanceSquared, range);
		color += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
	}

	// Spot lights
	int spotCount = int(u_Frame.FeatureFlags.w);
	for (int i = 0; i < spotCount; i++)
	{
		vec3 toLight = u_SpotLights[i].PositionRange.xyz - v_WorldPosition;
		float distanceSquared = dot(toLight, toLight);
		float range = u_SpotLights[i].PositionRange.w;
		if (distanceSquared > range * range)
			continue;
		vec3 L = toLight * inversesqrt(max(distanceSquared, 1e-8));
		float cosAngle = dot(-L, normalize(u_SpotLights[i].DirectionCosOuter.xyz));
		float cosOuter = u_SpotLights[i].DirectionCosOuter.w;
		float cosInner = u_SpotLights[i].Params.x;
		float cone = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
		cone *= cone;
		if (cone <= 0.0)
			continue;
		vec3 radiance = u_SpotLights[i].ColorIntensity.rgb * u_SpotLights[i].ColorIntensity.a * DistanceAttenuation(distanceSquared, range) * cone;
		color += EvaluateLight(N, V, L, radiance, albedo, metallic, roughness, F0);
	}

	// Image-based lighting
	vec3 ambient;
	if (u_Frame.EnvironmentParams.w > 0.5)
	{
		float envIntensity = u_Frame.EnvironmentParams.x;
		float envRotation = u_Frame.EnvironmentParams.y;
		float maxMip = max(u_Frame.EnvironmentParams.z - 1.0, 0.0);

		vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
		vec3 kD = (1.0 - F) * (1.0 - metallic);

		vec3 irradiance = texture(samplerCube(u_IrradianceMap, s_LinearClamp), RotateY(N, envRotation)).rgb;
		vec3 diffuse = irradiance * albedo;

		vec3 R = reflect(-V, N);
		vec3 prefiltered = textureLod(samplerCube(u_PrefilteredMap, s_LinearClamp), RotateY(R, envRotation), roughness * maxMip).rgb;
		vec2 brdf = textureLod(sampler2D(u_BRDFLut, s_LinearClamp), vec2(NdotV, roughness), 0.0).rg;
		vec3 specular = prefiltered * (F * brdf.x + brdf.y);

		// Specular occlusion approximation (Lagarde 2014).
		float specularOcclusion = clamp(pow(NdotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
		ambient = (kD * diffuse * occlusion + specular * specularOcclusion) * envIntensity;
	}
	else
	{
		ambient = u_Frame.AmbientColor.rgb * albedo * occlusion;
	}
	color += ambient;

	// Emissive
	vec3 emissive = u_Material.EmissiveColor.rgb;
	if ((u_Material.Flags.y & TEXTURE_EMISSIVE) != 0u)
		emissive *= texture(sampler2D(u_EmissiveMap, s_Material), uv).rgb;
	color += emissive;

	float alpha = alphaMode == 2u ? albedoSample.a : 1.0;
	o_Color = vec4(color, alpha);
}
