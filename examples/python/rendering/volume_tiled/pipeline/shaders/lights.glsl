// lights.glsl - direct light from scene.lights[i], the scene's own lights
// (the sun and up to 128 more), shared by the forward passes. The including
// shader declares the Lights block without an instance name
// (DEFAULT_LIGHTS_BLOCK) and the lights themselves (DEFAULT_SCENE_LIGHTS),
// and includes sk/pbr.glsl. With LOCAL_SHADOWS, spot
// and point lights are shadowed. The many VTFS lights are vtfs_lights.glsl's.

#ifndef DEFAULT_LIGHTS_GLSL
#define DEFAULT_LIGHTS_GLSL

#ifdef LOCAL_SHADOWS
#include "local_shadows.glsl"
#endif

// Light from scene light i. `sunIndex` is the light shadowed by
// `sunVisibility`; -1 when none is.
vec3 lightFrom(uint i, vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
               vec3 F0, int sunIndex, float sunVisibility) {
    float type = sceneLights[i].positionType.w;
    vec3 color = sceneLights[i].colorIntensity.rgb * sceneLights[i].colorIntensity.w;
    vec3 direction = sceneLights[i].directionRange.xyz;
    float range = sceneLights[i].directionRange.w;
    vec4 attenuation = sceneLights[i].attenuation;

    vec3 L;
    float falloff = 1.0;
    if (type < 0.5) {                       // directional
        L = -normalize(direction);
        if (int(i) == sunIndex) falloff = sunVisibility;
    } else {                                // point or spot
        vec3 toLight = sceneLights[i].positionType.xyz - worldPos;
        float dist = length(toLight);
        L = toLight / max(dist, 1e-4);
        falloff = 1.0 / max(attenuation.x + attenuation.y * dist + attenuation.z * dist * dist, 1e-4);
        if (range > 0.0) {
            // Smooth window to zero at the range.
            float r = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
            falloff *= r * r;
        }
        if (type > 1.5) {
            float cosOuter = attenuation.w;
            float theta = dot(L, -normalize(direction));
            falloff *= smoothstep(cosOuter, mix(cosOuter, 1.0, 0.1), theta);
        }
#ifdef LOCAL_SHADOWS
        if (falloff > 1e-4 && dot(N, L) > 0.0) {
            falloff *= localShadowVisibility(int(i), type, worldPos, N, L);
        }
#endif
    }

    float NdotL = dot(N, L);
    if (NdotL <= 0.0 || falloff <= 1e-4) return vec3(0.0);
    return cookTorrance(N, V, L, albedo, metallic, roughness, F0) * color * NdotL * falloff;
}

// Light from every scene light.
vec3 directLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                 vec3 F0, int sunIndex, float sunVisibility) {
    vec3 total = vec3(0.0);
    for (uint i = 0u; i < SCENE_LIGHT_COUNT; ++i) {
        total += lightFrom(i, worldPos, N, V, albedo, metallic, roughness, F0, sunIndex, sunVisibility);
    }
    return total;
}

// Image-based light, diffuse irradiance and split-sum specular apart, for
// occluding them differently.
void ambientLightParts(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                       vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0,
                       out vec3 diffuse, out vec3 specular) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    diffuse = kD * texture(irradianceMap, N).rgb * albedo;

    float maxLod = float(textureQueryLevels(prefilterMap) - 1);
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(prefilterMap, R, roughness * maxLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    specular = prefiltered * (F0 * brdf.x + brdf.y);
}

vec3 ambientLight(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                  vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 diffuse, specular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      diffuse, specular);
    return diffuse + specular;
}

#endif
