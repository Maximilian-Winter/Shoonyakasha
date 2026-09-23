//
// sk/pbr.glsl - Cook-Torrance BRDF pieces for metallic-roughness materials
//
//     #include "sk/pbr.glsl"
//
// Roughness is perceptual roughness (glTF), squared inside the distribution.
// Directions are unit vectors: N normal, V towards the viewer, L towards the
// light.
//

#ifndef SK_PBR_GLSL
#define SK_PBR_GLSL

const float SK_PI = 3.14159265359;

// Fresnel-Schlick approximation.
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with a roughness term, for image-based lighting.
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Base reflectivity: 0.04 for dielectrics, the albedo for metals.
vec3 baseReflectivity(vec3 albedo, float metallic) {
    return mix(vec3(0.04), albedo, metallic);
}

// GGX / Trowbridge-Reitz normal distribution.
float distributionGGX(float NdotH, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (SK_PI * denom * denom);
}

// Smith geometry term with Schlick-GGX, k tuned for direct lighting.
float geometrySmith(float NdotV, float NdotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

// Reflected radiance per unit of incoming radiance for one light, before the
// light's colour, intensity, NdotL and attenuation are applied.
vec3 cookTorrance(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / SK_PI;
    return diffuse + specular;
}

#endif
